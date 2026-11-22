#ifdef __linux__
#define _GNU_SOURCE
#endif
#include "storage.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#ifdef G_OS_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <stdlib.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef struct {
    gchar *directory;
    gchar *path;
} Fixture;

static void
setup(Fixture *f, gconstpointer data)
{
    (void)data;
    GError *error = NULL;
    f->directory = g_dir_make_tmp("crumbwriter-test-XXXXXX", &error);
    g_assert_no_error(error);
#ifndef G_OS_WIN32
    /* macOS may advertise /var/... as the temporary directory while the
     * canonical manuscript and backup names start with /private/var/... . */
    char *physical = realpath(f->directory, NULL);
    g_assert_nonnull(physical);
    g_free(f->directory);
    f->directory = g_strdup(physical);
    free(physical);
#endif
    f->path = g_build_filename(f->directory, "Crumbs.txt", NULL);
}

static void
teardown(Fixture *f, gconstpointer data)
{
    (void)data;
    GDir *dir = g_dir_open(f->directory, 0, NULL);
    g_assert_nonnull(dir);
    const gchar *name;
    while ((name = g_dir_read_name(dir))) {
        g_autofree gchar *child = g_build_filename(f->directory, name, NULL);
        g_assert_cmpint(g_remove(child), ==, 0);
    }
    g_dir_close(dir);
    g_assert_cmpint(g_rmdir(f->directory), ==, 0);
    g_free(f->directory);
    g_free(f->path);
}

static void
put_bytes(const gchar *path, const gchar *bytes, gssize size)
{
    GError *error = NULL;
    g_assert_true(g_file_set_contents(path, bytes, size, &error));
    g_assert_no_error(error);
}

static void
expect_bytes(const gchar *path, const gchar *bytes, gsize size)
{
    gchar *got = NULL;
    gsize got_size;
    GError *error = NULL;
    g_assert_true(g_file_get_contents(path, &got, &got_size, &error));
    g_assert_no_error(error);
    g_assert_cmpmem(got, got_size, bytes, size);
    g_free(got);
}

static void
backup_and_format(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar raw[] = "\357\273\277Crumb 1: Té: Body\r\nCrumb 2: : ";
    const gchar edited[] = "Crumb 1: Té: Body\nCrumb 2: : 🐦\n";
    const gchar disk[] = "Crumb 1: Té: Body\r\nCrumb 2: : 🐦\r\n";
    put_bytes(f->path, raw, sizeof raw - 1);
#ifndef G_OS_WIN32
    g_assert_cmpint(chmod(f->path, 0640), ==, 0);
#endif
    g_autofree gchar *one = NULL;
    g_autofree gchar *two = NULL;
    GError *error = NULL;
    CwFile *first = cw_file_open(f->path, TRUE, &one, &error);
    g_assert_no_error(error);
    g_assert_nonnull(first);
    g_assert_cmpstr(one, ==, "Crumb 1: Té: Body\nCrumb 2: :");
    CwFile *second = cw_file_open(f->path, TRUE, &two, &error);
    g_assert_no_error(error);
    g_assert_nonnull(second);
    const gchar *a = cw_file_backup_path(first);
    const gchar *b = cw_file_backup_path(second);
    g_assert_nonnull(a);
    g_assert_nonnull(b);
    g_assert_cmpstr(a, !=, b);
    g_assert_true(g_str_has_suffix(a, ".bak"));
    expect_bytes(a, raw, sizeof raw - 1);
    expect_bytes(b, raw, sizeof raw - 1);

    g_assert_true(cw_file_save(first, edited, &error));
    g_assert_no_error(error);
    expect_bytes(f->path, disk, sizeof disk - 1);
#ifndef G_OS_WIN32
    struct stat st;
    g_assert_cmpint(stat(f->path, &st), ==, 0);
    g_assert_cmpuint(st.st_mode & 0777, ==, 0640);
#endif
    g_assert_false(cw_file_save(second, "other edit", &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG);
    g_clear_error(&error);
    expect_bytes(f->path, disk, sizeof disk - 1);
    expect_bytes(a, raw, sizeof raw - 1);

    g_autofree gchar *copy_path = g_build_filename(f->directory, "copy.txt", NULL);
    CwFile *copy = cw_file_save_as_like(first, copy_path, edited, TRUE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(copy);
    g_assert_null(cw_file_backup_path(copy));
    expect_bytes(copy_path, disk, sizeof disk - 1);
    g_assert_null(cw_file_save_as_like(second, f->path, "another edit", TRUE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_EXISTS);
    g_clear_error(&error);

    cw_file_free(copy);
    cw_file_free(second);
    cw_file_free(first);
}

static void
manuscript_without_bom(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar original[] = "\357\273\277" "apple\r\nAna\357\273\277bell";
    const gchar plain[] = "apple\r\nAna\357\273\277bell";
    put_bytes(f->path, original, sizeof original - 1);
    g_autofree gchar *text = NULL;
    GError *error = NULL;
    CwFile *file = cw_file_open(f->path, TRUE, &text, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(text, ==, "apple\nAna\357\273\277bell");
    g_autofree gchar *path = g_build_filename(f->directory, "first-copy.txt", NULL);
    CwFile *copy = cw_file_save_as_like(file, path, text, TRUE, &error);
    g_assert_no_error(error);
    expect_bytes(path, plain, sizeof plain - 1);
    cw_file_free(copy);
    /* Ctrl+S without an edit must still remove the disk signature. */
    g_assert_true(cw_file_save(file, text, &error));
    g_assert_no_error(error);
    expect_bytes(f->path, plain, sizeof plain - 1);
    expect_bytes(cw_file_backup_path(file), original, sizeof original - 1);
    cw_file_free(file);

    put_bytes(f->path, "\357\273\277", 3);
    g_clear_pointer(&text, g_free);
    file = cw_file_open(f->path, TRUE, &text, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(text, ==, "");
    g_assert_true(cw_file_save(file, text, &error));
    g_assert_no_error(error);
    expect_bytes(f->path, "", 0);
    expect_bytes(cw_file_backup_path(file), "\357\273\277", 3);
    g_assert_true(cw_file_save(file, "\357\273\277\357\273\277" "new", &error));
    g_assert_no_error(error);
    expect_bytes(f->path, "new", 3);
    cw_file_free(file);
}

static void
new_and_mixed(Fixture *f, gconstpointer data)
{
    (void)data;
    GError *error = NULL;
    CwFile *fresh = cw_file_save_as(f->path, "", TRUE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(fresh);
    g_assert_null(cw_file_backup_path(fresh));
    g_assert_true(cw_file_save(fresh, "a\r\nb", &error));
    g_assert_no_error(error);
    expect_bytes(f->path, "a\nb", 3);
    g_assert_true(cw_file_save(fresh, "a\r \nb", &error));
    g_assert_no_error(error);
    expect_bytes(f->path, "a\n\nb", 4);
    cw_file_free(fresh);

    const gchar mixed[] = "alpha\r\nbeta\rgamma\ndelta";
    put_bytes(f->path, mixed, sizeof mixed - 1);
    g_autofree gchar *text = NULL;
    CwFile *file = cw_file_open(f->path, FALSE, &text, &error);
    g_assert_no_error(error);
    g_assert_nonnull(file);
    g_assert_null(cw_file_backup_path(file));
    g_assert_cmpstr(text, ==, "alpha\nbeta\ngamma\ndelta");

    CwTargetStamp before, after;
    g_assert_true(cw_file_stamp_target(f->path, &before, &error));
    g_assert_no_error(error);
    g_assert_true(cw_file_save(file, text, &error));
    g_assert_no_error(error);
    g_assert_true(cw_file_stamp_target(f->path, &after, &error));
    g_assert_no_error(error);
#ifdef G_OS_WIN32
    g_assert_cmpuint(before.identity.file_id, ==, after.identity.file_id);
#else
    g_assert_cmpuint(before.identity.st_ino, ==, after.identity.st_ino);
#endif
    expect_bytes(f->path, mixed, sizeof mixed - 1);

    const gchar inserted[] = "alpha\r\nNEW\r\nbeta\rgamma\ndelta";
    g_assert_true(cw_file_save(file, "alpha\nNEW\nbeta\ngamma\ndelta", &error));
    g_assert_no_error(error);
    expect_bytes(f->path, inserted, sizeof inserted - 1);
    g_assert_true(cw_file_save(file, text, &error));
    g_assert_no_error(error);
    expect_bytes(f->path, mixed, sizeof mixed - 1);
    cw_file_free(file);
}

static void
manuscript_spacing(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar original[] = "\357\273\277Llibre//  Fulla 1: Vers\t\t#1:  Té\tverd /  cafè\u00a0 \t\r\n"
                             "Fulla 2:\t : dos\u202f  \r\t  \nFulla 4: : fi\t";
    const gchar *first = "Llibre// Fulla 1: Vers #1: Té verd / cafè\u00a0";
    const gchar *second = "Fulla 2: : dos\u202f";
    const gchar *last = "Fulla 4: : fi";
    g_autofree gchar *logical = g_strjoin("\n", first, second, "", last, NULL);
    g_autofree gchar *disk = g_strconcat(first, "\r\n", second, "\r\r\n", last, NULL);
    put_bytes(f->path, original, sizeof original - 1);

    g_autofree gchar *text = NULL;
    GError *error = NULL;
    CwFile *file = cw_file_open(f->path, TRUE, &text, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(text, ==, logical);
    expect_bytes(f->path, original, sizeof original - 1);
    expect_bytes(cw_file_backup_path(file), original, sizeof original - 1);

    /* The new line adopts CRLF. The original CR survives separately from the
     * now-empty line's terminator, so reopening still yields four lines. */
    g_autofree gchar *pasted = g_strconcat("Draft \t  line \t\r\n", text, " \t", NULL);
    g_autofree gchar *expected = g_strconcat("Draft line\r\n", disk, NULL);
    g_autofree gchar *path = g_build_filename(f->directory, "copy.txt", NULL);
    CwFile *copy = cw_file_save_as_like(file, path, pasted, FALSE, &error);
    g_assert_no_error(error);
    expect_bytes(path, expected, strlen(expected));
    expect_bytes(f->path, original, sizeof original - 1);
    cw_file_free(copy);

    g_assert_true(cw_file_save(file, text, &error));
    g_assert_no_error(error);
    expect_bytes(f->path, disk, strlen(disk));
    expect_bytes(cw_file_backup_path(file), original, sizeof original - 1);
    cw_file_free(file);
    g_clear_pointer(&text, g_free);
    file = cw_file_open(f->path, FALSE, &text, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(text, ==, logical);
    g_assert_true(cw_file_save(file, text, &error));
    g_assert_no_error(error);
    expect_bytes(f->path, disk, strlen(disk));
    cw_file_free(file);
}

static void
external_edits(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar original[] = "Earl  Grey";
    const gchar external[] = "Earl\t Grey";
    put_bytes(f->path, original, sizeof original - 1);
    g_autofree gchar *text = NULL;
    GError *error = NULL;
    CwFile *file = cw_file_open(f->path, FALSE, &text, &error);
    g_assert_no_error(error);
    g_assert_cmpstr(text, ==, "Earl Grey");

#ifdef G_OS_WIN32
    g_autofree gunichar2 *wide = g_utf8_to_utf16(f->path, -1, NULL, NULL, NULL);
    HANDLE handle = CreateFileW((wchar_t *)wide, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ,
                                NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    g_assert_true(handle != INVALID_HANDLE_VALUE);
#else
    gint fd = open(f->path, O_RDWR);
    g_assert_cmpint(fd, >=, 0);
    g_assert_cmpint(flock(fd, LOCK_EX | LOCK_NB), ==, 0);
#endif
    g_assert_false(cw_file_save(file, "Acorn", &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    expect_bytes(f->path, original, sizeof original - 1);

#ifdef G_OS_WIN32
    FILETIME written;
    DWORD count;
    g_assert_true(GetFileTime(handle, NULL, NULL, &written));
    g_assert_true(WriteFile(handle, external, sizeof external - 1, &count, NULL));
    g_assert_cmpuint(count, ==, sizeof external - 1);
    g_assert_true(SetFileTime(handle, NULL, NULL, &written));
    g_assert_true(CloseHandle(handle));
#else
    struct stat st;
    g_assert_cmpint(fstat(fd, &st), ==, 0);
    g_assert_cmpint(write(fd, external, sizeof external - 1), ==, sizeof external - 1);
    /* Same length, timestamp, inode and normalized text. Only the raw disk
     * snapshot distinguishes this external edit from the original. */
#ifdef __APPLE__
    struct timespec times[] = { st.st_atimespec, st.st_mtimespec };
#else
    struct timespec times[] = { st.st_atim, st.st_mtim };
#endif
    g_assert_cmpint(futimens(fd, times), ==, 0);
    g_assert_cmpint(close(fd), ==, 0);
#endif
    g_assert_false(cw_file_save(file, "Acorn", &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG);
    g_clear_error(&error);
    expect_bytes(f->path, external, sizeof external - 1);
    g_assert_cmpint(g_remove(f->path), ==, 0);
    g_assert_false(cw_file_save(file, "Acorn", &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG);
    g_clear_error(&error);
    g_assert_false(g_file_test(f->path, G_FILE_TEST_EXISTS));
    cw_file_free(file);
}

#ifndef G_OS_WIN32
static void
symlinks(Fixture *f, gconstpointer data)
{
    (void)data;
    put_bytes(f->path, "target", 6);
    g_autofree gchar *alias = g_build_filename(f->directory, "alias", NULL);
    g_assert_cmpint(symlink("Crumbs.txt", alias), ==, 0);
    g_autofree gchar *text = NULL;
    GError *error = NULL;
    CwFile *file = cw_file_open(alias, TRUE, &text, &error);
    g_assert_no_error(error);
    g_assert_nonnull(file);
    g_assert_cmpstr(cw_file_path(file), ==, f->path);
    g_assert_true(cw_file_save(file, "saved", &error));
    g_assert_no_error(error);
    expect_bytes(f->path, "saved", 5);
    g_assert_true(g_file_test(alias, G_FILE_TEST_IS_SYMLINK));

    g_autofree gchar *other = g_build_filename(f->directory, "other.txt", NULL);
    put_bytes(other, "untouched", 9);
    g_assert_cmpint(g_remove(f->path), ==, 0);
    g_assert_cmpint(symlink("other.txt", f->path), ==, 0);
    g_assert_false(cw_file_save(file, "stale", &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG);
    g_clear_error(&error);
    expect_bytes(other, "untouched", 9);

    g_assert_cmpint(g_remove(other), ==, 0);
    g_assert_null(cw_file_save_as(f->path, "new", TRUE, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME);
    g_clear_error(&error);
    g_assert_true(g_file_test(f->path, G_FILE_TEST_IS_SYMLINK));
    cw_file_free(file);
}

#endif

static void
save_again_after_copy(Fixture *f, gconstpointer data)
{
    (void)data;
    GError *error = NULL;
    g_autofree gchar *copy_path = g_build_filename(f->directory, "Crumbs.txt.copy", NULL);
    put_bytes(f->path, "source", -1);
    put_bytes(copy_path, "previous destination", -1);
    g_autofree gchar *original = NULL;
    CwFile *source = cw_file_open(f->path, TRUE, &original, &error);
    g_assert_no_error(error);
    CwFile *copy = cw_file_save_as_like(source, copy_path, "first revision", TRUE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(copy);
    cw_file_free(source);
    expect_bytes(cw_file_backup_path(copy), "previous destination", 20);
    g_assert_true(cw_file_save(copy, "second revision", &error));
    g_assert_no_error(error);
    expect_bytes(copy_path, "second revision", 15);
    expect_bytes(f->path, "source", 6);

    g_autofree gchar *replacement = g_build_filename(f->directory, "replacement", NULL);
    put_bytes(replacement, "second revision", -1);
#ifdef G_OS_WIN32
    g_autofree gunichar2 *from = g_utf8_to_utf16(replacement, -1, NULL, NULL, NULL);
    g_autofree gunichar2 *to = g_utf8_to_utf16(copy_path, -1, NULL, NULL, NULL);
    g_assert_true(MoveFileExW((wchar_t *)from, (wchar_t *)to, MOVEFILE_REPLACE_EXISTING));
#else
    g_assert_cmpint(g_rename(replacement, copy_path), ==, 0);
#endif
    g_assert_false(cw_file_save(copy, "third revision", &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG);
    g_clear_error(&error);
    expect_bytes(copy_path, "second revision", 15);
    cw_file_free(copy);
}

static void
invalid_input(Fixture *f, gconstpointer data)
{
    (void)data;
    GError *error = NULL;
    const gchar bytes[] = { 'x', 0, 'y' };
    put_bytes(f->path, bytes, sizeof bytes);
    gchar *text = NULL;
    g_assert_null(cw_file_open(f->path, TRUE, &text, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
    g_clear_error(&error);
    g_assert_null(text);
    expect_bytes(f->path, bytes, sizeof bytes);
    CwFile *replacement = cw_file_save_as(f->path, "new text", TRUE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(replacement);
    expect_bytes(cw_file_backup_path(replacement), bytes, sizeof bytes);
    expect_bytes(f->path, "new text", 8);
    cw_file_free(replacement);
    g_assert_cmpint(g_remove(f->path), ==, 0);
#ifdef G_OS_WIN32
    g_assert_cmpint(g_mkdir(f->path, 0700), ==, 0);
#else
    g_assert_cmpint(mkfifo(f->path, 0600), ==, 0);
#endif
    g_assert_null(cw_file_open(f->path, FALSE, &text, &error));
    g_assert_error(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE);
    g_clear_error(&error);
#ifdef G_OS_WIN32
    g_assert_cmpint(g_rmdir(f->path), ==, 0);
#endif
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add("/storage/backup-and-format", Fixture, NULL, setup, backup_and_format, teardown);
    g_test_add("/storage/manuscript-without-bom", Fixture, NULL, setup, manuscript_without_bom, teardown);
    g_test_add("/storage/new-and-mixed", Fixture, NULL, setup, new_and_mixed, teardown);
    g_test_add("/storage/manuscript-spacing", Fixture, NULL, setup, manuscript_spacing, teardown);
    g_test_add("/storage/external-edits", Fixture, NULL, setup, external_edits, teardown);
    g_test_add("/storage/save-again-after-copy", Fixture, NULL, setup, save_again_after_copy, teardown);
#ifndef G_OS_WIN32
    g_test_add("/storage/symlinks", Fixture, NULL, setup, symlinks, teardown);
#endif
    g_test_add("/storage/invalid-input", Fixture, NULL, setup, invalid_input, teardown);
    return g_test_run();
}

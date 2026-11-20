#ifdef __linux__
#define _GNU_SOURCE
#endif
#include "storage.h"
#include "spacing.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum { EOL_LF, EOL_CRLF, EOL_CR } LineEnding;

typedef struct {
    LineEnding usual;
    gchar *text;
    GByteArray *endings;
} TextFormat;

struct CwFile {
    gchar *path;
    gchar *backup;
    gchar *disk_bytes;
    gsize disk_length;
    struct stat identity;
    TextFormat format;
};

static void
io_error(GError **error, const gchar *action, const gchar *path, gint code)
{
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(code),
                "%s “%s” (%s).", action, path, g_strerror(code));
}

static void
changed_error(GError **error, const gchar *path)
{
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG,
                "“%s” changed on disk. The edited text is still open.", path);
}

static gboolean
same_file(const struct stat *a, const struct stat *b)
{
    return a->st_ino == b->st_ino && a->st_dev == b->st_dev;
}

static gboolean
same_version(const struct stat *a, const struct stat *b)
{
    return same_file(a, b) && a->st_size == b->st_size &&
           a->st_mtim.tv_sec == b->st_mtim.tv_sec &&
           a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
           a->st_ctim.tv_sec == b->st_ctim.tv_sec &&
           a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}

/* Resolve existing symlinks once. A later symlink installed in place of the
 * resolved target is a conflict. New files need a resolved parent directory. */
static gchar *
resolve_path(const gchar *path, gboolean allow_new, gboolean *exists, GError **error)
{
    *exists = FALSE;
    char *physical = realpath(path, NULL);
    if (physical) {
        gchar *resolved = g_strdup(physical);
        free(physical);
        *exists = TRUE;
        return resolved;
    }
    gint code = errno;
    if (!allow_new || code != ENOENT) {
        io_error(error, "Cannot resolve", path, code);
        return NULL;
    }

    struct stat st;
    if (lstat(path, &st) == 0) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                    "“%s” is a dangling symbolic link.", path);
        return NULL;
    }
    g_autofree gchar *parent = g_path_get_dirname(path);
    physical = realpath(parent, NULL);
    if (!physical) {
        io_error(error, "Cannot open directory", parent, errno);
        return NULL;
    }
    g_autofree gchar *name = g_path_get_basename(path);
    gchar *resolved = g_build_filename(physical, name, NULL);
    free(physical);
    return resolved;
}

static gint
open_regular(const gchar *path, gint flags, struct stat *st, GError **error)
{
    gint fd = open(path, flags | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        const gint code = errno;
        /* FreeBSD returns EMLINK for O_NOFOLLOW on a symlink. */
        if (flags == O_RDWR &&
            (code == ENOENT || code == ELOOP || code == EMLINK || code == EISDIR))
            changed_error(error, path);
        else
            io_error(error, "Cannot open", path, code);
        return -1;
    }
    if (fstat(fd, st) != 0) {
        io_error(error, "Cannot inspect", path, errno);
        close(fd);
        return -1;
    }
    if (!S_ISREG(st->st_mode)) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                    "“%s” is not a regular file.", path);
        close(fd);
        return -1;
    }
    return fd;
}

static gboolean
still_current(const gchar *path, const struct stat *snapshot, GError **error)
{
    struct stat current;
    if (lstat(path, &current) != 0 || !same_version(snapshot, &current)) {
        changed_error(error, path);
        return FALSE;
    }
    return TRUE;
}

static gchar *
read_snapshot(gint fd, const gchar *path, const struct stat *st, gsize *size,
              GError **error)
{
    if (st->st_size > G_MAXINT) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "“%s” exceeds the text buffer size limit.", path);
        return NULL;
    }
    GString *raw = g_string_sized_new((gsize)st->st_size);
    gchar block[65536];
    for (;;) {
        ssize_t count = read(fd, block, sizeof block);
        if (count == 0)
            break;
        if (count < 0) {
            if (errno == EINTR)
                continue;
            io_error(error, "Cannot read", path, errno);
            g_string_free(raw, TRUE);
            return NULL;
        }
        if (raw->len + (gsize)count > G_MAXINT) {
            g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                        "“%s” exceeds the text buffer size limit.", path);
            g_string_free(raw, TRUE);
            return NULL;
        }
        g_string_append_len(raw, block, count);
    }
    if (!still_current(path, st, error)) {
        g_string_free(raw, TRUE);
        return NULL;
    }
    *size = raw->len;
    return g_string_free(raw, FALSE);
}

static void
clear_format(TextFormat *format)
{
    g_clear_pointer(&format->text, g_free);
    g_clear_pointer(&format->endings, g_byte_array_unref);
}

/* Treat a leading U+FEFF as a signature, including text pasted into the
 * beginning of the editor. U+FEFF elsewhere remains part of the manuscript. */
static const gchar *
skip_signatures(const gchar *text)
{
    while (g_str_has_prefix(text, "\357\273\277"))
        text += 3;
    return text;
}

/* Store one byte per physical separator so even mixed-ending documents can
 * round-trip without adding a final newline or normalizing existing lines. */
static gboolean
decode_text(const gchar *raw, gsize size, TextFormat *format, GError **error)
{
    if (!g_utf8_validate(raw, (gssize)size, NULL)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "The file contains invalid UTF-8 or a NUL byte.");
        return FALSE;
    }
    const gchar *p = skip_signatures(raw);
    GString *normalized = g_string_sized_new(size);
    format->endings = g_byte_array_new();
    guint counts[3] = { 0 };
    while (*p) {
        gsize span = strcspn(p, "\r\n");
        g_string_append_len(normalized, p, span);
        p += span;
        if (*p) {
            guint8 ending = EOL_LF;
            if (*p == '\r') {
                ending = p[1] == '\n' ? EOL_CRLF : EOL_CR;
                if (ending == EOL_CRLF)
                    ++p;
            }
            ++p;
            counts[ending]++;
            g_byte_array_append(format->endings, &ending, 1);
            g_string_append_c(normalized, '\n');
        }
    }
    format->usual = format->endings->len ? format->endings->data[0] : EOL_LF;
    for (guint i = 0; i < G_N_ELEMENTS(counts); ++i)
        if (counts[i] > counts[format->usual])
            format->usual = (LineEnding)i;
    format->text = g_string_free(normalized, FALSE);
    cw_spacing_normalize(format->text);
    return TRUE;
}

static guint
count_lf(const gchar *text)
{
    guint count = 0;
    while ((text = strchr(text, '\n')) != NULL) {
        ++count;
        ++text;
    }
    return count;
}

static gchar *
encode_text(const TextFormat *format, const gchar *text, gsize *length, GError **error)
{
    if (!g_utf8_validate(text, -1, NULL)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                            "The document contains invalid UTF-8.");
        return NULL;
    }
    /* Compare clean logical lines when matching original separators. The raw
     * disk snapshot remains available for backups and conflict detection. */
    g_autofree gchar *clean = g_strdup(skip_signatures(text));
    gsize text_length = cw_spacing_normalize(clean);
    text = clean;

    guint old_lines = format && format->endings ? format->endings->len : 0;
    guint new_lines = count_lf(text);
    guint suffix_lines = 0;
    if (old_lines && old_lines != new_lines) {
        const gchar *old = format->text;
        gsize common = 0;
        while (old[common] && old[common] == text[common])
            ++common;
        const gchar *old_end = old + strlen(old);
        const gchar *new_end = text + text_length;
        while (old_end > old + common && new_end > text + common &&
               old_end[-1] == new_end[-1]) {
            --old_end;
            --new_end;
            if (*old_end == '\n')
                ++suffix_lines;
        }
    }

    GString *out = g_string_sized_new(text_length);
    guint index = 0;
    const gchar *p = text;
    const gchar *next;
    while ((next = strchr(p, '\n')) != NULL) {
        g_string_append_len(out, p, next - p);
        LineEnding ending = format ? format->usual : EOL_LF;
        if (index >= new_lines - suffix_lines)
            ending = format->endings->data[old_lines - (new_lines - index)];
        else if (index < old_lines - suffix_lines)
            ending = format->endings->data[index];
        /* A bare CR followed immediately by LF would reread as one line
         * break. Give the empty line a CRLF so both breaks survive. */
        if (ending == EOL_LF && out->len && out->str[out->len - 1] == '\r')
            ending = EOL_CRLF;
        if (ending != EOL_LF)
            g_string_append_c(out, '\r');
        if (ending != EOL_CR)
            g_string_append_c(out, '\n');
        ++index;
        p = next + 1;
    }
    g_string_append(out, p);
    *length = out->len;
    return g_string_free(out, FALSE);
}

static gboolean
write_all(gint fd, const gchar *bytes, gsize length)
{
    while (length) {
        ssize_t written = write(fd, bytes, MIN(length, (gsize)G_MAXINT));
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return FALSE;
        }
        if (written == 0) {
            errno = EIO;
            return FALSE;
        }
        bytes += written;
        length -= (gsize)written;
    }
    return TRUE;
}

/* Stage and flush in the target directory. Rename/link is the commit point. */
static gchar *
stage_bytes(const gchar *path, const gchar *bytes, gsize size, mode_t mode,
            gboolean preserve_mode, struct stat *st, GError **error)
{
    g_autofree gchar *directory = g_path_get_dirname(path);
    gchar *stage = g_build_filename(directory, ".crumbwriter-XXXXXX", NULL);
    gint fd = g_mkstemp_full(stage, O_RDWR | O_CLOEXEC, preserve_mode ? 0600 : mode);
    if (fd < 0) {
        io_error(error, "Cannot create a temporary file in", directory, errno);
        g_free(stage);
        return NULL;
    }
    gboolean ok = write_all(fd, bytes, size) &&
                  (!preserve_mode || fchmod(fd, mode) == 0) &&
                  fsync(fd) == 0 && fstat(fd, st) == 0;
    gint code = errno;
    if (close(fd) != 0 && ok) {
        ok = FALSE;
        code = errno;
    }
    if (!ok) {
        io_error(error, "Cannot write", path, code);
        unlink(stage);
        g_free(stage);
        return NULL;
    }
    return stage;
}

static void
sync_parent(const gchar *path)
{
    g_autofree gchar *dir = g_path_get_dirname(path);
    gint fd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        /* Some filesystems do not implement directory fsync. The completed
         * rename is already visible and cannot safely be rolled back here. */
        fsync(fd);
        close(fd);
    }
}

static gchar *
make_backup(const CwFile *file, GError **error)
{
    struct stat st;
    g_autofree gchar *stage = stage_bytes(file->path, file->disk_bytes,
                                         file->disk_length, 0600, FALSE, &st, error);
    if (!stage)
        return NULL;
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree gchar *date = g_date_time_format(now, "%Y%m%d-%H%M%S");
    gchar *backup;
    for (;;) {
        g_autofree gchar *uuid = g_uuid_string_random();
        backup = g_strdup_printf("%s.%s-%.8s.bak", file->path, date, uuid);
        if (link(stage, backup) == 0)
            break;
        gint code = errno;
        g_clear_pointer(&backup, g_free);
        if (code != EEXIST) {
            io_error(error, "Cannot create a backup for", file->path, code);
            break;
        }
    }
    unlink(stage);
    if (backup)
        sync_parent(backup);
    return backup;
}

/* Opening and Save As both take their baseline from the exact disk bytes. */
gboolean
cw_file_stamp_target(const gchar *path, CwTargetStamp *stamp, GError **error)
{
    if (stat(path, &stamp->identity) == 0) {
        stamp->exists = TRUE;
        return TRUE;
    }
    if (errno == ENOENT) {
        stamp->exists = FALSE;
        return TRUE;
    }
    io_error(error, "Cannot inspect", path, errno);
    return FALSE;
}

static CwFile *
load_target(const gchar *path, gboolean allow_new, gboolean backup, const CwTargetStamp *stamp,
            GError **error)
{
    gboolean exists;
    gchar *resolved = resolve_path(path, allow_new, &exists, error);
    if (!resolved)
        return NULL;
    CwFile *file = g_new0(CwFile, 1);
    file->path = resolved;
    if (stamp && exists != stamp->exists) {
        changed_error(error, resolved);
        cw_file_free(file);
        return NULL;
    }
    if (!exists)
        return file;

    gint fd = open_regular(file->path, O_RDONLY, &file->identity, error);
    if (fd >= 0) {
        if (stamp && !same_version(&stamp->identity, &file->identity))
            changed_error(error, resolved);
        else
            file->disk_bytes = read_snapshot(fd, file->path, &file->identity,
                                            &file->disk_length, error);
        close(fd);
    }
    /* Save As may replace a confirmed destination of another format. Its
     * backup and conflict check still use every original byte. */
    if (file->disk_bytes &&
        (decode_text(file->disk_bytes, file->disk_length, &file->format,
                     allow_new ? NULL : error) || allow_new)) {
        if (!backup || (file->backup = make_backup(file, error)) != NULL)
            return file;
    }
    cw_file_free(file);
    return NULL;
}

CwFile *
cw_file_open(const gchar *path, gboolean backup, gchar **text, GError **error)
{
    g_return_val_if_fail(path != NULL && text != NULL, NULL);
    *text = NULL;
    CwFile *file = load_target(path, FALSE, backup, NULL, error);
    if (file)
        *text = g_strdup(file->format.text);
    return file;
}

/* Serialize cooperating writers on the old inode and check the pathname again
 * just before rename. A second process holding the old inode cannot displace a
 * replacement it never read. POSIX locks are advisory for unrelated editors. */
static gint
lock_original(const CwFile *file, struct stat *current, GError **error)
{
    gint fd = open_regular(file->path, O_RDWR, current, error);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        io_error(error, "Cannot lock", file->path, errno);
        close(fd);
        return -1;
    }
    gboolean matches = same_file(current, &file->identity);
    if (matches) {
        gsize size = 0;
        g_autofree gchar *actual = read_snapshot(fd, file->path, current, &size, error);
        if (!actual) {
            close(fd);
            return -1;
        }
        matches = size == file->disk_length && !memcmp(actual, file->disk_bytes, size);
    }
    if (!matches) {
        changed_error(error, file->path);
        close(fd);
        return -1;
    }
    return fd;
}

static gboolean
can_write(CwWriteGate *gate, gboolean publish, GError **error)
{
    if (!gate || (publish ? g_atomic_int_compare_and_exchange(&gate->state, 0, 2) :
                            g_atomic_int_get(&gate->state) == 0))
        return TRUE;
    g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Save canceled.");
    return FALSE;
}

void
cw_write_gate_cancel(CwWriteGate *gate)
{
    if (gate)
        g_atomic_int_compare_and_exchange(&gate->state, 0, 1);
}

static gboolean
commit_bytes(CwFile *file, const TextFormat *format, const gchar *text, CwWriteGate *gate,
             GError **error)
{
    if (!can_write(gate, FALSE, error))
        return FALSE;
    gsize size;
    LineEnding preferred = format->usual;
    g_autofree gchar *raw = encode_text(format, text, &size, error);
    if (!raw)
        return FALSE;
    struct stat current = { 0 };
    struct stat next;
    gint fd = -1;
    gboolean existing = file->disk_bytes != NULL;
    if (existing) {
        fd = lock_original(file, &current, error);
        if (fd < 0)
            return FALSE;
        if (size == file->disk_length && !memcmp(raw, file->disk_bytes, size)) {
            close(fd);
            return can_write(gate, TRUE, error);
        }
    }

    mode_t mode = existing ? current.st_mode & 07777 : 0666;
    g_autofree gchar *stage = stage_bytes(file->path, raw, size, mode, existing, &next, error);
    gboolean ok = FALSE;
    if (stage && (!existing || still_current(file->path, &current, error)) &&
        can_write(gate, TRUE, error)) {
        if ((existing ? rename(stage, file->path) : link(stage, file->path)) == 0) {
            ok = TRUE;
        } else {
            io_error(error, "Cannot publish", file->path, errno);
        }
    }
    if (stage && (!ok || !existing))
        unlink(stage);
    if (fd >= 0)
        close(fd);
    if (!ok)
        return FALSE;

    sync_parent(file->path);
    clear_format(&file->format);
    /* encode_text produced validated UTF-8 without NUL bytes. */
    decode_text(raw, size, &file->format, NULL);
    if (file->format.endings->len == 0)
        file->format.usual = preferred;
    g_free(file->disk_bytes);
    file->disk_bytes = g_steal_pointer(&raw);
    file->disk_length = size;
    file->identity = next;
    return TRUE;
}

gboolean
cw_file_save(CwFile *file, const gchar *text, GError **error)
{
    return cw_file_save_guarded(file, text, NULL, error);
}

gboolean
cw_file_save_guarded(CwFile *file, const gchar *text, CwWriteGate *gate, GError **error)
{
    g_return_val_if_fail(file != NULL && text != NULL, FALSE);
    return commit_bytes(file, &file->format, text, gate, error);
}

CwFile *
cw_file_save_as_like(const CwFile *source, const gchar *path, const gchar *text,
                     gboolean backup, GError **error)
{
    return cw_file_save_as_guarded(source, path, text, backup, NULL, NULL, error);
}

CwFile *
cw_file_save_as_guarded(const CwFile *source, const gchar *path, const gchar *text,
                        gboolean backup, const CwTargetStamp *stamp, CwWriteGate *gate,
                        GError **error)
{
    g_return_val_if_fail(path != NULL && text != NULL, NULL);
    if (!can_write(gate, FALSE, error))
        return NULL;
    /* A Save As back onto the open path must still pass its original conflict
     * check. The caller can keep its original CwFile via cw_file_save(). */
    if (source) {
        gboolean exists;
        g_autofree gchar *resolved = resolve_path(path, TRUE, &exists, error);
        if (!resolved)
            return NULL;
        if (g_str_equal(source->path, resolved)) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_EXISTS,
                                "Choose another filename for this copy, or use Save.");
            return NULL;
        }
    }
    CwFile *file = load_target(path, TRUE, backup, stamp, error);
    if (file && !commit_bytes(file, source ? &source->format : &file->format, text, gate, error)) {
        cw_file_free(file);
        return NULL;
    }
    return file;
}

CwFile *
cw_file_save_as(const gchar *path, const gchar *text, gboolean backup, GError **error)
{
    return cw_file_save_as_like(NULL, path, text, backup, error);
}

const gchar *
cw_file_path(const CwFile *file)
{
    return file ? file->path : NULL;
}

const gchar *
cw_file_backup_path(const CwFile *file)
{
    return file ? file->backup : NULL;
}

void
cw_file_free(CwFile *file)
{
    if (file) {
        g_free(file->path);
        g_free(file->backup);
        g_free(file->disk_bytes);
        clear_format(&file->format);
        g_free(file);
    }
}

#ifdef __linux__
#define _GNU_SOURCE
#endif
#include "storage-platform.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static void
io_error(GError **error, const gchar *action, const gchar *path, gint code)
{
    g_set_error(error, G_IO_ERROR, g_io_error_from_errno(code),
                "%s “%s” (%s).", action, path, g_strerror(code));
}

static gboolean
same_file(const struct stat *a, const struct stat *b)
{
    return a->st_ino == b->st_ino && a->st_dev == b->st_dev;
}

#ifdef __APPLE__
#define CW_MTIME(st) ((st)->st_mtimespec)
#define CW_CTIME(st) ((st)->st_ctimespec)
#else
#define CW_MTIME(st) ((st)->st_mtim)
#define CW_CTIME(st) ((st)->st_ctim)
#endif

static gboolean
same_data_attributes(const struct stat *a, const struct stat *b)
{
    return same_file(a, b) && a->st_size == b->st_size &&
           a->st_mode == b->st_mode && a->st_uid == b->st_uid && a->st_gid == b->st_gid &&
           CW_MTIME(a).tv_sec == CW_MTIME(b).tv_sec &&
           CW_MTIME(a).tv_nsec == CW_MTIME(b).tv_nsec;
}

static gboolean
same_version(const struct stat *a, const struct stat *b)
{
    return same_data_attributes(a, b) &&
           CW_CTIME(a).tv_sec == CW_CTIME(b).tv_sec &&
           CW_CTIME(a).tv_nsec == CW_CTIME(b).tv_nsec;
}

/* Resolve existing symlinks once. A later symlink installed in place of the
 * resolved target is a conflict. New files need a resolved parent directory. */
gchar *
cw_disk_resolve(const gchar *path, gboolean allow_new, gboolean *exists, GError **error)
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
            cw_disk_changed(error, path);
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
still_current(gint fd, const gchar *path, const struct stat *snapshot,
              const gchar *expected, gsize length, GError **error)
{
    struct stat current;
    if (lstat(path, &current) != 0 || !same_data_attributes(snapshot, &current) ||
        (guint64)current.st_size != length) {
        cw_disk_changed(error, path);
        return FALSE;
    }
    if (same_version(snapshot, &current))
        return TRUE;

    /* FreeBSD UFS can update ctime after our earlier rename when it finishes
     * a link-count update. Verify the exact bytes through the open inode and
     * then recheck the pathname. pread preserves the caller's read offset. */
    gchar block[65536];
    for (gsize offset = 0; offset < length;) {
        gsize wanted = MIN(sizeof block, length - offset);
        ssize_t count = pread(fd, block, wanted, (off_t)offset);
        if (count < 0) {
            if (errno == EINTR)
                continue;
            io_error(error, "Cannot verify", path, errno);
            return FALSE;
        }
        if (count == 0 || memcmp(block, expected + offset, (gsize)count) != 0) {
            cw_disk_changed(error, path);
            return FALSE;
        }
        offset += (gsize)count;
    }
    struct stat verified;
    if (lstat(path, &verified) != 0 || !same_version(&current, &verified)) {
        cw_disk_changed(error, path);
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
    if (!still_current(fd, path, st, raw->str, raw->len, error)) {
        g_string_free(raw, TRUE);
        return NULL;
    }
    *size = raw->len;
    return g_string_free(raw, FALSE);
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

gchar *
cw_disk_backup(const gchar *path, const CwDiskSnapshot *disk, GError **error)
{
    struct stat st;
    g_autofree gchar *stage = stage_bytes(path, disk->bytes,
                                         disk->size, 0600, FALSE, &st, error);
    if (!stage)
        return NULL;
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree gchar *date = g_date_time_format(now, "%Y%m%d-%H%M%S");
    gchar *backup;
    for (;;) {
        g_autofree gchar *uuid = g_uuid_string_random();
        backup = g_strdup_printf("%s.%s-%.8s.bak", path, date, uuid);
        if (link(stage, backup) == 0)
            break;
        gint code = errno;
        g_clear_pointer(&backup, g_free);
        if (code != EEXIST) {
            io_error(error, "Cannot create a backup for", path, code);
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


gboolean
cw_disk_read(const gchar *path, const CwTargetStamp *stamp, CwDiskSnapshot *disk, GError **error)
{
    gint fd = open_regular(path, O_RDONLY, &disk->identity, error);
    if (fd < 0)
        return FALSE;
    if (stamp && !same_version(&stamp->identity, &disk->identity))
        cw_disk_changed(error, path);
    else
        disk->bytes = read_snapshot(fd, path, &disk->identity, &disk->size, error);
    close(fd);
    return disk->bytes != NULL;
}

/* Serialize cooperating writers on the old inode and check the pathname again
 * just before rename. A second process holding the old inode cannot displace a
 * replacement it never read. POSIX locks are advisory for unrelated editors. */
static gint
lock_original(const gchar *path, const CwDiskSnapshot *disk, struct stat *current, GError **error)
{
    gint fd = open_regular(path, O_RDWR, current, error);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        io_error(error, "Cannot lock", path, errno);
        close(fd);
        return -1;
    }
    gboolean matches = same_file(current, &disk->identity);
    if (matches) {
        gsize size = 0;
        g_autofree gchar *actual = read_snapshot(fd, path, current, &size, error);
        if (!actual) {
            close(fd);
            return -1;
        }
        matches = size == disk->size && !memcmp(actual, disk->bytes, size);
    }
    if (!matches) {
        cw_disk_changed(error, path);
        close(fd);
        return -1;
    }
    return fd;
}

gboolean
cw_disk_write(const gchar *path, const CwDiskSnapshot *disk, const gchar *raw, gsize size,
              CwWriteGate *gate, CwFileIdentity *next, GError **error)
{
    struct stat current = { 0 };
    gint fd = -1;
    gboolean existing = disk->bytes != NULL;
    if (existing) {
        fd = lock_original(path, disk, &current, error);
        if (fd < 0)
            return FALSE;
        if (size == disk->size && !memcmp(raw, disk->bytes, size)) {
            *next = current;
            close(fd);
            return cw_disk_may_write(gate, TRUE, error);
        }
    }

    mode_t mode = existing ? current.st_mode & 07777 : 0666;
    g_autofree gchar *stage = stage_bytes(path, raw, size, mode, existing, next, error);
    gboolean ok = FALSE;
    if (stage && (!existing || still_current(fd, path, &current,
                                            disk->bytes, disk->size, error)) &&
        cw_disk_may_write(gate, TRUE, error)) {
        if ((existing ? rename(stage, path) : link(stage, path)) == 0) {
            ok = TRUE;
        } else {
            io_error(error, "Cannot publish", path, errno);
        }
    }
    if (stage && (!ok || !existing))
        unlink(stage);
    if (fd >= 0)
        close(fd);
    if (!ok)
        return FALSE;

    sync_parent(path);
    return TRUE;
}

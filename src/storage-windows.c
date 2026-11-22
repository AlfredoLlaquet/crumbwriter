/* Wide Win32 file calls keep names independent of the user's code page. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#include "storage-platform.h"

#include <string.h>

#define ALL_SHARES (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)

static void
win_error(GError **error, const gchar *action, const gchar *path, DWORD code)
{
    g_autofree gchar *reason = g_win32_error_message(code);
    g_set_error(error, G_IO_ERROR, g_io_error_from_win32_error(code),
                "%s “%s” (%s).", action, path, reason);
}

static gboolean
missing(DWORD code)
{
    return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
}

/* A long-path prefix also works on machines with the legacy MAX_PATH policy.
 * Slashes must already be native before Windows receives a prefixed path. */
static wchar_t *
native_path(const gchar *path)
{
    g_autofree gchar *absolute = g_canonicalize_filename(path, NULL);
    g_strdelimit(absolute, "/", '\\');
    g_autofree gchar *prefixed = NULL;
    if (!g_str_has_prefix(absolute, "\\\\?\\")) {
        if (g_str_has_prefix(absolute, "\\\\"))
            prefixed = g_strconcat("\\\\?\\UNC\\", absolute + 2, NULL);
        else
            prefixed = g_strconcat("\\\\?\\", absolute, NULL);
    }
    return (wchar_t *)g_utf8_to_utf16(prefixed ? prefixed : absolute, -1, NULL, NULL, NULL);
}

static gchar *
final_name(HANDLE handle, const gchar *path, GError **error)
{
    DWORD size = GetFinalPathNameByHandleW(handle, NULL, 0, FILE_NAME_NORMALIZED);
    if (!size) {
        win_error(error, "Cannot resolve", path, GetLastError());
        return NULL;
    }
    g_autofree wchar_t *buffer = g_new(wchar_t, size);
    DWORD used = GetFinalPathNameByHandleW(handle, buffer, size, FILE_NAME_NORMALIZED);
    if (!used || used >= size) {
        win_error(error, "Cannot resolve", path, used ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
        return NULL;
    }
    g_autofree gchar *utf8 = g_utf16_to_utf8((gunichar2 *)buffer, used, NULL, NULL, error);
    if (!utf8)
        return NULL;
    gchar *resolved;
    if (g_str_has_prefix(utf8, "\\\\?\\UNC\\"))
        resolved = g_strconcat("//", utf8 + 8, NULL);
    else
        resolved = g_strdup(g_str_has_prefix(utf8, "\\\\?\\") ? utf8 + 4 : utf8);
    return g_strdelimit(resolved, "\\", '/');
}

static HANDLE
open_target(const wchar_t *path, DWORD access, DWORD shares, DWORD flags)
{
    return CreateFileW(path, access, shares, NULL, OPEN_EXISTING,
                       FILE_FLAG_BACKUP_SEMANTICS | flags, NULL);
}

gchar *
cw_disk_resolve(const gchar *path, gboolean allow_new, gboolean *exists, GError **error)
{
    *exists = FALSE;
    g_autofree wchar_t *wide = native_path(path);
    HANDLE handle = open_target(wide, FILE_READ_ATTRIBUTES, ALL_SHARES, 0);
    if (handle != INVALID_HANDLE_VALUE) {
        gchar *result = final_name(handle, path, error);
        CloseHandle(handle);
        *exists = TRUE;
        return result;
    }
    DWORD code = GetLastError();
    if (!allow_new || !missing(code)) {
        win_error(error, "Cannot open", path, code);
        return NULL;
    }
    if (GetFileAttributesW(wide) != INVALID_FILE_ATTRIBUTES) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME,
                    "“%s” is a dangling symbolic link.", path);
        return NULL;
    }
    g_autofree gchar *parent = g_path_get_dirname(path);
    g_autofree wchar_t *directory = native_path(parent);
    handle = open_target(directory, FILE_READ_ATTRIBUTES, ALL_SHARES, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        win_error(error, "Cannot open directory", parent, GetLastError());
        return NULL;
    }
    g_autofree gchar *resolved = final_name(handle, parent, error);
    CloseHandle(handle);
    if (!resolved)
        return NULL;
    g_autofree gchar *name = g_path_get_basename(path);
    return g_build_path("/", resolved, name, NULL);
}

static gboolean
identity(HANDLE handle, const gchar *path, CwFileIdentity *id, GError **error)
{
    BY_HANDLE_FILE_INFORMATION info;
    FILE_BASIC_INFO basic;
    if (!GetFileInformationByHandle(handle, &info) ||
        !GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof basic)) {
        win_error(error, "Cannot inspect", path, GetLastError());
        return FALSE;
    }
    FILE_ATTRIBUTE_TAG_INFO tag;
    gboolean link = (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
        (!GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &tag, sizeof tag) ||
         IsReparseTagNameSurrogate(tag.ReparseTag));
    if (GetFileType(handle) != FILE_TYPE_DISK || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || link) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_REGULAR_FILE,
                    "“%s” is not a regular file.", path);
        return FALSE;
    }
    id->volume = info.dwVolumeSerialNumber;
    id->file_id = ((guint64)info.nFileIndexHigh << 32) | info.nFileIndexLow;
    id->size = ((guint64)info.nFileSizeHigh << 32) | info.nFileSizeLow;
    id->written = basic.LastWriteTime.QuadPart;
    id->changed = basic.ChangeTime.QuadPart;
    return TRUE;
}

static gboolean
same_file(const CwFileIdentity *a, const CwFileIdentity *b)
{
    return a->volume == b->volume && a->file_id == b->file_id;
}

static gboolean
same_version(const CwFileIdentity *a, const CwFileIdentity *b)
{
    return same_file(a, b) && a->size == b->size && a->written == b->written &&
           a->changed == b->changed;
}

gboolean
cw_file_stamp_target(const gchar *path, CwTargetStamp *stamp, GError **error)
{
    g_autofree wchar_t *wide = native_path(path);
    HANDLE h = open_target(wide, FILE_READ_ATTRIBUTES, ALL_SHARES, 0);
    stamp->exists = h != INVALID_HANDLE_VALUE;
    if (!stamp->exists) {
        DWORD code = GetLastError();
        if (missing(code))
            return TRUE;
        win_error(error, "Cannot inspect", path, code);
        return FALSE;
    }
    gboolean ok = identity(h, path, &stamp->identity, error);
    CloseHandle(h);
    return ok;
}

/* A handle denying writes and deletes gives the reader one stable snapshot.
 * An empty file still owns a non-null, terminated buffer. */
gboolean
cw_disk_read(const gchar *path, const CwTargetStamp *stamp, CwDiskSnapshot *disk, GError **error)
{
    g_autofree wchar_t *wide = native_path(path);
    HANDLE h = open_target(wide, GENERIC_READ, FILE_SHARE_READ, 0);
    if (h == INVALID_HANDLE_VALUE) {
        win_error(error, "Cannot open", path, GetLastError());
        return FALSE;
    }
    gboolean ok = identity(h, path, &disk->identity, error);
    if (ok && stamp && !same_version(&disk->identity, &stamp->identity)) {
        cw_disk_changed(error, path);
        ok = FALSE;
    }
    if (ok && disk->identity.size > G_MAXINT) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA,
                    "“%s” exceeds the text buffer size limit.", path);
        ok = FALSE;
    }
    if (ok) {
        disk->size = (gsize)disk->identity.size;
        disk->bytes = g_malloc(disk->size + 1);
        for (gsize pos = 0; pos < disk->size;) {
            DWORD count;
            if (!ReadFile(h, disk->bytes + pos, (DWORD)MIN(disk->size - pos, 65536), &count, NULL)) {
                win_error(error, "Cannot read", path, GetLastError());
                ok = FALSE;
                break;
            }
            if (!count) {
                cw_disk_changed(error, path);
                ok = FALSE;
                break;
            }
            pos += count;
        }
        disk->bytes[disk->size] = '\0';
    }
    CloseHandle(h);
    return ok;
}

/* Closed, flushed sibling of the destination. CREATE_NEW protects even an
 * unlikely collision. Neither backups nor new manuscripts overwrite a name
 * that appeared while the file was being written. */
static wchar_t *
stage_bytes(const gchar *path, const gchar *bytes, gsize size, CwFileIdentity *id, GError **error)
{
    g_autofree gchar *dir = g_path_get_dirname(path);
    g_autofree wchar_t *wide = NULL;
    HANDLE h;
    for (;;) {
        g_autofree gchar *uuid = g_uuid_string_random();
        g_autofree gchar *base = g_strconcat(".crumbwriter-", uuid, ".tmp", NULL);
        g_autofree gchar *sibling = g_build_filename(dir, base, NULL);
        g_free(wide);
        wide = native_path(sibling);
        h = CreateFileW(wide, GENERIC_WRITE | FILE_READ_ATTRIBUTES, 0, NULL,
                        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h != INVALID_HANDLE_VALUE)
            break;
        DWORD code = GetLastError();
        if (code != ERROR_FILE_EXISTS) {
            win_error(error, "Cannot create a temporary file in", dir, code);
            return NULL;
        }
    }
    gboolean ok = TRUE;
    for (gsize pos = 0; pos < size;) {
        DWORD count;
        if (!WriteFile(h, bytes + pos, (DWORD)MIN(size - pos, 65536), &count, NULL)) {
            win_error(error, "Cannot write", path, GetLastError());
            ok = FALSE;
            break;
        }
        if (!count) {
            win_error(error, "Cannot write", path, ERROR_WRITE_FAULT);
            ok = FALSE;
            break;
        }
        pos += count;
    }
    if (ok && !FlushFileBuffers(h)) {
        win_error(error, "Cannot flush", path, GetLastError());
        ok = FALSE;
    }
    if (ok && id)
        ok = identity(h, path, id, error);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(wide);
        return NULL;
    }
    return g_steal_pointer(&wide);
}

gchar *
cw_disk_backup(const gchar *path, const CwDiskSnapshot *disk, GError **error)
{
    g_autofree wchar_t *stage = stage_bytes(path, disk->bytes, disk->size, NULL, error);
    if (!stage)
        return NULL;
    g_autoptr(GDateTime) now = g_date_time_new_now_local();
    g_autofree gchar *date = g_date_time_format(now, "%Y%m%d-%H%M%S");
    for (;;) {
        g_autofree gchar *uuid = g_uuid_string_random();
        g_autofree gchar *backup = g_strdup_printf("%s.%s-%.8s.bak", path, date, uuid);
        g_autofree wchar_t *wide = native_path(backup);
        if (MoveFileExW(stage, wide, MOVEFILE_WRITE_THROUGH))
            return g_steal_pointer(&backup);
        DWORD code = GetLastError();
        if (code != ERROR_ALREADY_EXISTS && code != ERROR_FILE_EXISTS) {
            win_error(error, "Cannot create a backup for", path, code);
            DeleteFileW(stage);
            return NULL;
        }
    }
}

static gboolean
matches_baseline(HANDLE h, const gchar *path, const CwDiskSnapshot *disk, GError **error)
{
    CwFileIdentity current;
    if (!identity(h, path, &current, error))
        return FALSE;
    if (same_file(&current, &disk->identity) && current.size == disk->size) {
        char buffer[65536];
        gsize pos = 0;
        while (pos < disk->size) {
            DWORD count;
            if (!ReadFile(h, buffer, (DWORD)MIN(disk->size - pos, sizeof buffer), &count, NULL)) {
                win_error(error, "Cannot verify", path, GetLastError());
                return FALSE;
            }
            if (!count || memcmp(disk->bytes + pos, buffer, count))
                break;
            pos += count;
        }
        if (pos == disk->size)
            return TRUE;
    }
    cw_disk_changed(error, path);
    return FALSE;
}

/* Keep the old file read-only to other writers while its exact bytes are
 * verified. Share delete to permit ReplaceFileW (which preserves destination
 * ACLs and streams). Reopen the name just before commit to catch replacement. */
static HANDLE
lock_baseline(const wchar_t *wide, const gchar *path, const CwDiskSnapshot *disk, GError **error)
{
    HANDLE h = open_target(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, 0);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD code = GetLastError();
        if (missing(code))
            cw_disk_changed(error, path);
        else
            win_error(error, "Cannot open", path, code);
        return INVALID_HANDLE_VALUE;
    }
    OVERLAPPED lock = { 0 };
    if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                    0, MAXDWORD, MAXDWORD, &lock)) {
        win_error(error, "Cannot lock", path, GetLastError());
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    if (!matches_baseline(h, path, disk, error)) {
        CloseHandle(h);
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

static gboolean
still_named(const wchar_t *wide, const gchar *path, const CwFileIdentity *original, GError **error)
{
    HANDLE h = open_target(wide, FILE_READ_ATTRIBUTES, ALL_SHARES, FILE_FLAG_OPEN_REPARSE_POINT);
    CwFileIdentity named;
    gboolean same = h != INVALID_HANDLE_VALUE && identity(h, path, &named, NULL) &&
                    same_file(original, &named);
    if (h != INVALID_HANDLE_VALUE)
        CloseHandle(h);
    if (!same)
        cw_disk_changed(error, path);
    return same;
}

gboolean
cw_disk_write(const gchar *path, const CwDiskSnapshot *disk, const gchar *bytes, gsize size,
              CwWriteGate *gate, CwFileIdentity *next, GError **error)
{
    g_autofree wchar_t *wide = native_path(path);
    HANDLE old = INVALID_HANDLE_VALUE;
    if (disk->bytes) {
        old = lock_baseline(wide, path, disk, error);
        if (old == INVALID_HANDLE_VALUE)
            return FALSE;
        if (size == disk->size && !memcmp(bytes, disk->bytes, size)) {
            *next = disk->identity;
            gboolean ok = still_named(wide, path, next, error) && cw_disk_may_write(gate, TRUE, error);
            CloseHandle(old);
            return ok;
        }
    }
    g_autofree wchar_t *stage = stage_bytes(path, bytes, size, next, error);
    gboolean ok = FALSE;
    if (stage && (!disk->bytes || still_named(wide, path, &disk->identity, error)) &&
        cw_disk_may_write(gate, TRUE, error)) {
        if (disk->bytes)
            ok = ReplaceFileW(wide, stage, NULL, 0, NULL, NULL);
        else
            ok = MoveFileExW(stage, wide, MOVEFILE_WRITE_THROUGH);
        if (!ok)
            win_error(error, "Cannot publish", path, GetLastError());
    }
    if (old != INVALID_HANDLE_VALUE)
        CloseHandle(old);
    if (stage && !ok)
        DeleteFileW(stage);
    return ok;
}

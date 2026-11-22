#include "storage-platform.h"
#include "spacing.h"

#include <string.h>

typedef enum { EOL_LF, EOL_CRLF, EOL_CR } LineEnding;

typedef struct {
    LineEnding usual;
    gchar *text;
    GByteArray *endings;
} TextFormat;

struct CwFile {
    gchar *path;
    gchar *backup;
    CwDiskSnapshot disk;
    TextFormat format;
};

void
cw_disk_changed(GError **error, const gchar *path)
{
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_WRONG_ETAG,
                "“%s” changed on disk. The edited text is still open.", path);
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

static CwFile *
load_target(const gchar *path, gboolean allow_new, gboolean backup, const CwTargetStamp *stamp,
            GError **error)
{
    gboolean exists;
    gchar *resolved = cw_disk_resolve(path, allow_new, &exists, error);
    if (!resolved)
        return NULL;
    CwFile *file = g_new0(CwFile, 1);
    file->path = resolved;
    if (stamp && exists != stamp->exists) {
        cw_disk_changed(error, resolved);
        cw_file_free(file);
        return NULL;
    }
    if (!exists)
        return file;

    if (!cw_disk_read(file->path, stamp, &file->disk, error)) {
        cw_file_free(file);
        return NULL;
    }
    /* Save As may replace a confirmed destination of another format. Its
     * backup and conflict check still use every original byte. */
    if (decode_text(file->disk.bytes, file->disk.size, &file->format,
                     allow_new ? NULL : error) || allow_new) {
        if (!backup || (file->backup = cw_disk_backup(file->path, &file->disk, error)) != NULL)
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

gboolean
cw_disk_may_write(CwWriteGate *gate, gboolean publish, GError **error)
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
    if (!cw_disk_may_write(gate, FALSE, error))
        return FALSE;
    gsize size;
    LineEnding preferred = format->usual;
    g_autofree gchar *raw = encode_text(format, text, &size, error);
    if (!raw)
        return FALSE;
    CwFileIdentity next;
    if (!cw_disk_write(file->path, &file->disk, raw, size, gate, &next, error))
        return FALSE;
    if (file->disk.bytes && file->disk.size == size && !memcmp(raw, file->disk.bytes, size))
        return TRUE;
    clear_format(&file->format);
    /* encode_text produced validated UTF-8 without NUL bytes. */
    decode_text(raw, size, &file->format, NULL);
    if (file->format.endings->len == 0)
        file->format.usual = preferred;
    g_free(file->disk.bytes);
    file->disk.bytes = g_steal_pointer(&raw);
    file->disk.size = size;
    file->disk.identity = next;
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
    if (!cw_disk_may_write(gate, FALSE, error))
        return NULL;
    /* A Save As back onto the open path must still pass its original conflict
     * check. The caller can keep its original CwFile via cw_file_save(). */
    if (source) {
        gboolean exists;
        g_autofree gchar *resolved = cw_disk_resolve(path, TRUE, &exists, error);
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
        g_free(file->disk.bytes);
        clear_format(&file->format);
        g_free(file);
    }
}

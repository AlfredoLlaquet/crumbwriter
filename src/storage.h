#ifndef CW_STORAGE_H
#define CW_STORAGE_H

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct CwFile CwFile;

/* All manuscript writes use UTF-8 without a BOM. The editor receives text with
 * LF line endings and no encoding signature. Its trailing newline, if present,
 * is part of the text. A startup backup preserves every original byte including
 * a BOM in a uniquely named sibling. */
CwFile *cw_file_open(const gchar *path, gboolean backup, gchar **text, GError **error);
gboolean cw_file_save(CwFile *file, const gchar *text, GError **error);
CwFile *cw_file_save_as(const gchar *path, const gchar *text, gboolean backup, GError **error);
CwFile *cw_file_save_as_like(const CwFile *source, const gchar *path, const gchar *text,
                             gboolean backup, GError **error);

/* Paths remain owned by the CwFile. The backup path may be NULL. */
const gchar *cw_file_path(const CwFile *file);
const gchar *cw_file_backup_path(const CwFile *file);
void cw_file_free(CwFile *file);

G_END_DECLS

#endif

#ifndef CW_STORAGE_H
#define CW_STORAGE_H

#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct CwFile CwFile;

/* The editor receives clean spacing and LF line endings. All manuscript writes
 * clean horizontal spacing and use UTF-8 without a BOM. Physical line endings
 * and any terminal newline survive. A startup backup preserves every original
 * byte, including a BOM, in a uniquely named sibling. */
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

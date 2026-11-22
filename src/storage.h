#ifndef CW_STORAGE_H
#define CW_STORAGE_H

#include <gio/gio.h>
#ifndef G_OS_WIN32
#include <sys/stat.h>
#endif

G_BEGIN_DECLS

typedef struct CwFile CwFile;

/* Initialize one gate to zero for each background save. A discard can retire
 * the operation until it claims the atomic publication of its staged bytes. */
typedef struct { gint state; } CwWriteGate;
void cw_write_gate_cancel(CwWriteGate *gate);
gboolean cw_file_save_guarded(CwFile *file, const gchar *text, CwWriteGate *gate,
                              GError **error);
/* Save As may queue behind an active write. Preserve the destination approved
 * in the file chooser until the worker can acquire its actual disk snapshot. */
#ifdef G_OS_WIN32
typedef struct {
    guint32 volume;
    guint64 file_id, size, written, changed;
} CwFileIdentity;
#else
typedef struct stat CwFileIdentity;
#endif
typedef struct {
    gboolean exists;
    CwFileIdentity identity;
} CwTargetStamp;
gboolean cw_file_stamp_target(const gchar *path, CwTargetStamp *stamp, GError **error);
CwFile *cw_file_save_as_guarded(const CwFile *source, const gchar *path,
                               const gchar *text, gboolean backup, const CwTargetStamp *stamp,
                               CwWriteGate *gate, GError **error);

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

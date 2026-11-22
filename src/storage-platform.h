#ifndef CW_STORAGE_PLATFORM_H
#define CW_STORAGE_PLATFORM_H

#include "storage.h"

/* Keep all text rules in storage.c. These calls handle native file identity,
 * conflicts, backup creation, and the atomic publication of exact bytes. */
typedef struct {
    CwFileIdentity identity;
    gchar *bytes;
    gsize size;
} CwDiskSnapshot;

gchar *cw_disk_resolve(const gchar *path, gboolean allow_new, gboolean *exists, GError **error);
gboolean cw_disk_read(const gchar *path, const CwTargetStamp *stamp, CwDiskSnapshot *snapshot,
                      GError **error);
gchar *cw_disk_backup(const gchar *path, const CwDiskSnapshot *snapshot, GError **error);
gboolean cw_disk_write(const gchar *path, const CwDiskSnapshot *snapshot, const gchar *bytes,
                       gsize size, CwWriteGate *gate, CwFileIdentity *next, GError **error);

void cw_disk_changed(GError **error, const gchar *path);
gboolean cw_disk_may_write(CwWriteGate *gate, gboolean publish, GError **error);

#endif

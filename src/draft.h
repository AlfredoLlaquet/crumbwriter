#ifndef CW_DRAFT_H
#define CW_DRAFT_H

#include <glib.h>

/* A draft is private UTF-8 text. Its name survives a crash until an explicit
 * discard or a successful manuscript save. */
gboolean cw_draft_write(gchar **path, const gchar *text, GError **error);
void cw_draft_remove(gchar **path);
GPtrArray *cw_draft_list(void);

#endif

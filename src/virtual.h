#ifndef CW_VIRTUAL_H
#define CW_VIRTUAL_H

#include <gtksourceview/gtksource.h>

typedef struct CwVirtual CwVirtual;

CwVirtual *cw_virtual_new(GtkSourceView *view, void (*changed)(gpointer), gpointer data);
void cw_virtual_free(CwVirtual *space);
guint cw_virtual_rows(const CwVirtual *space);
void cw_virtual_clear(CwVirtual *space);
gboolean cw_virtual_key(CwVirtual *space, GdkEventKey *key);

#endif

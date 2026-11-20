#ifndef CW_EDITOR_H
#define CW_EDITOR_H

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

typedef struct _CwEditor CwEditor;

CwEditor *cw_editor_new(GtkSourceView *view);
GtkWidget *cw_editor_sidebar(CwEditor *editor);
void cw_editor_load(CwEditor *editor, const gchar *text);
/* Coordinates count GTK lines and Unicode characters, both starting at zero.
 * A negative line opens at the end of the final recognized crumb. */
void cw_editor_load_at(CwEditor *editor, const gchar *text, gint line, gint column);
void cw_editor_append(CwEditor *editor);
void cw_editor_focus_history(CwEditor *editor);
void cw_editor_focus_genres(CwEditor *editor);
void cw_editor_free(CwEditor *editor);

G_END_DECLS

#endif

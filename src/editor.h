#ifndef CW_EDITOR_H
#define CW_EDITOR_H

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

typedef struct _CwEditor CwEditor;

CwEditor *cw_editor_new(GtkSourceView *view);
void cw_editor_set_window_active(CwEditor *editor, gboolean active);
GtkWidget *cw_editor_sidebar(CwEditor *editor);
void cw_editor_load(CwEditor *editor, const gchar *text);
/* Coordinates count GTK lines and Unicode characters, both starting at zero.
 * A negative line chooses the document end, just before a terminal line break. */
void cw_editor_load_at(CwEditor *editor, const gchar *text, gint line, gint column);
void cw_editor_append(CwEditor *editor);
void cw_editor_go_to_end(CwEditor *editor);
guint cw_editor_virtual_rows(CwEditor *editor);
void cw_editor_leave_virtual(CwEditor *editor);
gboolean cw_editor_copy_crumb(CwEditor *editor);
gboolean cw_editor_delete_crumb(CwEditor *editor);
void cw_editor_focus_history(CwEditor *editor);
void cw_editor_focus_genres(CwEditor *editor);
void cw_editor_free(CwEditor *editor);

G_END_DECLS

#endif

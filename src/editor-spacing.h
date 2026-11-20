#ifndef CW_EDITOR_SPACING_H
#define CW_EDITOR_SPACING_H

#include <gtksourceview/gtksource.h>

typedef struct CwEditorSpacing CwEditorSpacing;

CwEditorSpacing *cw_editor_spacing_new(GtkTextBuffer *buffer);
void cw_editor_spacing_free(CwEditorSpacing *spacing);
void cw_editor_spacing_reset(CwEditorSpacing *spacing);
void cw_editor_spacing_depart(CwEditorSpacing *spacing);
/* The buffer need not belong to a CwEditor. */
void cw_editor_spacing_finish_input(GtkTextBuffer *buffer);
gboolean cw_editor_spacing_is_cleaning(GtkTextBuffer *buffer);

#endif

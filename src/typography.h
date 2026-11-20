#ifndef CW_TYPOGRAPHY_H
#define CW_TYPOGRAPHY_H

#include <gtksourceview/gtksource.h>

typedef struct CwTypography CwTypography;

/* Watches committed characters during keyboard events delivered to this view.
 * Programmatic changes to its buffer, including undo and paste, are literal. */
CwTypography *cw_typography_new(GtkSourceView *view);
void cw_typography_enable(CwTypography *type, gboolean enabled);
/* Resolve the punctuation currently being typed before explicit save or close. */
void cw_typography_finish(CwTypography *type);
void cw_typography_free(CwTypography *type);

#endif

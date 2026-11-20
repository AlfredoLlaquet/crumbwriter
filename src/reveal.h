#ifndef CW_REVEAL_H
#define CW_REVEAL_H

#include <gtk/gtk.h>

/* Configure the manuscript after adding its view to a GtkScrolledWindow. */
void cw_reveal_configure(GtkTextView *view);

/* Bring the insertion caret into view after a document or field jump. */
void cw_reveal_cursor(GtkTextView *view);

#endif

#ifndef CW_REVEAL_H
#define CW_REVEAL_H

#include <gtk/gtk.h>

/* Configure the manuscript after adding its view to a GtkScrolledWindow. */
void cw_reveal_configure(GtkTextView *view);

/* Bring the insertion caret into view after a document or field jump. */
void cw_reveal_cursor(GtkTextView *view);

/* Place a wrapped visual line in the middle of the viewport or a crumb
 * header at its top. Neither operation moves the buffer's insertion mark. */
void cw_reveal_center_line(GtkTextView *view, const GtkTextIter *base, guint virtual_rows);
void cw_reveal_top_line(GtkTextView *view, const GtkTextIter *header, guint virtual_rows);

/* Virtual row 1 follows the last physical display line; 0 ends virtual mode.
 * Geometry is returned in the same buffer coordinates as get_iter_location. */
void cw_reveal_virtual_cursor(GtkTextView *view, guint virtual_rows);
void cw_reveal_virtual_rect(GtkTextView *view, guint virtual_rows, GdkRectangle *rect);

#endif

#ifndef CW_CRUMB_H
#define CW_CRUMB_H

#include <glib.h>

typedef struct {
    gboolean valid;
    guint64 number;
    gchar *label;
    gchar *title;
    gint header_start;
    gint header_end;
    gint title_start;
    gint title_end;
    gint body_start;
} CwCrumb;

/*
 * Parse a single UTF-8 line. Offsets count characters from its beginning,
 * as GtkTextIter does. A known field starts after its leading whitespace.
 * Ends are exclusive and omit trailing whitespace and the separator colon.
 * An empty title starts at its closing colon. When that colon is still
 * missing, the end of the line is also the tentative start of the body.
 *
 * A recognizable label, positive number and first colon establish a header.
 * The optional work title before // on the first line is outside the header.
 * Clear a previous result before reusing it. An invalid result can be cleared.
 */
gboolean cw_crumb_parse(const gchar *line, gboolean first_line, CwCrumb *out);
void cw_crumb_clear(CwCrumb *crumb);

/* Counts header, title and body using the process locale's wc conventions. */
guint cw_crumb_words(const gchar *line, gboolean first_line);

#endif

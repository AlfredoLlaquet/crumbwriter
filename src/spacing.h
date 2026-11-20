#ifndef CW_SPACING_H
#define CW_SPACING_H

#include <glib.h>

G_BEGIN_DECLS

static inline gboolean
cw_spacing_is_horizontal(gunichar character)
{
    return character == ' ' || character == '\t';
}

/* Collapse ASCII spaces and tabs to one space, dropping them at line ends.
 * Emit one logical LF per CRLF, CR or LF. Operates in place and returns the
 * new byte length without the terminating NUL. */
gsize cw_spacing_normalize(gchar *text);

G_END_DECLS

#endif

#include "spacing.h"

gsize
cw_spacing_normalize(gchar *text)
{
    const gchar *source = text;
    gchar *dest = text;
    while (*source) {
        if (!cw_spacing_is_horizontal((guchar)*source)) {
            gchar character = *source++;
            if (character == '\r') {
                if (*source == '\n')
                    ++source;
                character = '\n';
            }
            *dest++ = character;
            continue;
        }
        do {
            ++source;
        } while (cw_spacing_is_horizontal((guchar)*source));
        if (*source && *source != '\r' && *source != '\n')
            *dest++ = ' ';
    }
    *dest = '\0';
    return (gsize)(dest - text);
}

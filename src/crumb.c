#include "crumb.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>

static const gchar *
skip_space(const gchar *p, const gchar *end)
{
    while (p < end && g_unichar_isspace(g_utf8_get_char(p)))
        p = g_utf8_next_char(p);
    return p;
}

static const gchar *
trim_end(const gchar *start, const gchar *end)
{
    while (end > start) {
        const gchar *prev = g_utf8_prev_char(end);
        if (!g_unichar_isspace(g_utf8_get_char(prev)))
            break;
        end = prev;
    }
    return end;
}

/* Return the beginning of the ordinal if the heading has the form "label N". */
static const gchar *
number_start(const gchar *start, const gchar *end)
{
    const gchar *p = end;
    while (p > start && g_ascii_isdigit(p[-1]))
        --p;
    if (p == end || p == start || !g_unichar_isspace(g_utf8_get_char(g_utf8_prev_char(p))))
        return NULL;
    return p;
}

static const gchar *
record_start(const gchar *line, gboolean first_line)
{
    if (!first_line)
        return line;
    const gchar *separator = strstr(line, "//");
    if (!separator)
        return line;

    /* Without a work title the body can still contain a URL or literal //. */
    const gchar *colon = strchr(line, ':');
    if (colon && colon < separator) {
        const gchar *start = skip_space(line, colon);
        if (number_start(start, trim_end(start, colon))) {
            const gchar *next_colon = strchr(colon + 1, ':');
            if (!next_colon || next_colon < separator)
                return line;
            /* A work title can itself start with something like "Volume 2:". */
            start = skip_space(separator + 2, next_colon);
            if (!number_start(start, trim_end(start, next_colon)))
                return line;
        }
    }
    return separator + 2;
}

gboolean
cw_crumb_parse(const gchar *line, gboolean first_line, CwCrumb *out)
{
    *out = (CwCrumb){0};
    const gchar *record = record_start(line, first_line);
    const gchar *colon = strchr(record, ':');
    if (!colon)
        return FALSE;
    const gchar *header = skip_space(record, colon);
    const gchar *header_end = trim_end(header, colon);
    const gchar *digits = number_start(header, header_end);
    if (!digits)
        return FALSE;

    out->record_start = (gint)g_utf8_pointer_to_offset(line, record);
    out->header_start = out->record_start + (gint)g_utf8_pointer_to_offset(record, header);
    out->digits_start = out->header_start + (gint)g_utf8_pointer_to_offset(header, digits);
    out->header_end = out->digits_start + (gint)(header_end - digits);
    errno = 0;
    guint64 number = g_ascii_strtoull(digits, NULL, 10);
    if (errno == ERANGE || number == 0)
        return FALSE;

    const gchar *label_end = trim_end(header, digits);
    const gchar *end = colon + strlen(colon);
    const gchar *second_colon = strchr(colon + 1, ':');
    const gchar *title_limit = second_colon ? second_colon : end;
    const gchar *title = skip_space(colon + 1, title_limit);
    const gchar *title_end = trim_end(title, title_limit);
    const gchar *body = second_colon ? skip_space(second_colon + 1, end) : end;

    out->valid = TRUE;
    out->number = number;
    out->label = g_strndup(header, label_end - header);
    out->title = g_strndup(title, title_end - title);

    /* Walk successive slices instead of decoding the same prefix five times. */
    out->title_start = out->header_end + (gint)g_utf8_pointer_to_offset(header_end, title);
    out->title_end = out->title_start + (gint)g_utf8_pointer_to_offset(title, title_end);
    out->body_start = out->title_end + (gint)g_utf8_pointer_to_offset(title_end, body);
    return TRUE;
}

void
cw_crumb_clear(CwCrumb *crumb)
{
    g_free(crumb->label);
    g_free(crumb->title);
    *crumb = (CwCrumb){0};
}

/* The first sed stage turns an em dash into a literal ASCII space. */
static gsize
sed_space_width(const gchar *p)
{
    if (*p == ' ')
        return 1;
    if ((guchar)*p == 0xe2 && g_str_has_prefix(p, "—"))
        return sizeof("—") - 1;
    return 0;
}

static gboolean
wc_space(gunichar c)
{
    if (iswspace((wint_t)c))
        return TRUE;
#ifdef __GLIBC__
    /* GNU wc also treats these four characters as word separators by default. */
    if (c == 0x00a0 || c == 0x2007 || c == 0x202f || c == 0x2060)
        return getenv("POSIXLY_CORRECT") == NULL;
#endif
    return FALSE;
}

guint
cw_crumb_words(const gchar *line, gboolean first_line)
{
    const gchar *p = record_start(line, first_line);
#ifdef __FreeBSD__
    /* FreeBSD wc -w reads bytes. Its multibyte decoding is enabled by -m. */
    const gboolean bytewise = TRUE;
#else
    const gboolean bytewise = MB_CUR_MAX == 1;
#endif
    gboolean in_word = FALSE;
    guint count = 0;
    while (*p) {
        gsize space = sed_space_width(p);
        if (space) {
            p += space;
            /* sed s@ / @ @g consumes both spaces, even for adjacent slashes. */
            if (*p == '/' && (space = sed_space_width(p + 1)) != 0)
                p += 1 + space;
            in_word = FALSE;
        } else {
            gunichar c = (guchar)*p;
            if (bytewise || c < 0x80) {
                ++p;
            } else {
                c = g_utf8_get_char(p);
                p = g_utf8_next_char(p);
            }
            gboolean word = !wc_space(c);
            if (word && !in_word)
                ++count;
            in_word = word;
        }
    }
    return count;
}

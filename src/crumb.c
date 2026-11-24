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

/* Enclosing punctuation in prose can immediately precede the work-title
   separator. Balanced brackets and parentheses inside a URL belong to it
   (e.g. an IPv6 host or a Wikipedia article with a parenthetical title). */
static const gchar *
url_end(const gchar *p)
{
    const gchar *begin = p;
    guint parentheses = 0, brackets = 0;
    while (*p) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isspace(c) || c == '<' || c == '>' || c == '"' ||
            c == '{' || c == '}' || c == 0xab || c == 0xbb ||
            c == 0x2018 || (c >= 0x201a && c <= 0x201f) || c == 0x2039 || c == 0x203a)
            break;
        if ((c == '\'' || c == 0x2019) &&
            (p == begin || !g_unichar_isalnum(g_utf8_get_char(g_utf8_prev_char(p))) ||
             !g_unichar_isalnum(g_utf8_get_char(g_utf8_next_char(p)))))
            break;
        if (c == '(') ++parentheses;
        if (c == '[') ++brackets;
        if (c == ')' && !parentheses) break;
        if (c == ']' && !brackets) break;
        if (c == ')') --parentheses;
        if (c == ']') --brackets;
        p = g_utf8_next_char(p);
    }
    return p;
}

gboolean
cw_next_web_link(const gchar *line, gint length, gint from, gint *start, gint *end)
{
    static gsize pattern;
    if (g_once_init_enter(&pattern)) {
        GRegex *regex = g_regex_new("(?<![\\w.@/-])(?:https?://|www\\.)",
                                   G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, NULL);
        g_once_init_leave(&pattern, (gsize)regex);
    }
    while (from < length) {
        GMatchInfo *match;
        if (!g_regex_match_full((GRegex *)pattern, line, length, from, 0, &match, NULL)) {
            g_match_info_free(match);
            return FALSE;
        }
        gint prefix_end;
        g_match_info_fetch_pos(match, 0, start, &prefix_end);
        g_match_info_free(match);
        const gchar *limit = url_end(line + prefix_end);
        /* A closing sentence mark is prose. The same character in a path,
         * query or fragment stays intact when another URL character follows. */
        while (limit > line + prefix_end) {
            const gchar *last = g_utf8_prev_char(limit);
            gunichar c = g_utf8_get_char(last);
            if (c != '.' && c != ',' && c != ';' && c != ':' &&
                c != '?' && c != '!' && c != 0x2026)
                break;
            limit = last;
        }
        *end = (gint)(limit - line);
        if (*end > prefix_end)
            return TRUE;
        from = prefix_end;
    }
    return FALSE;
}

static const gchar *
record_start(const gchar *line, gboolean first_line)
{
    if (!first_line || !strstr(line, "//"))
        return line;

    /* Match a whole URL before testing for a separator. This also skips //
       later in its path, query or fragment. Prose delimiters close the URL. */
    static gsize pattern;
    if (g_once_init_enter(&pattern)) {
        const gchar *url_or_separator =
            "\\b[a-z][a-z0-9.+-]*://|(?<![\\w.-])"
            "(?://(?=(?:[\\p{L}\\d-]+\\.)+[\\p{L}]{2,}\\b)|www\\.|"
            "(?:[\\p{L}\\d-]+\\.)+[\\p{L}]{2,}(?=[/:?#]))|(?<separator>//)";
        GRegex *regex = g_regex_new(url_or_separator, G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, NULL);
        g_once_init_leave(&pattern, (gsize)regex);
    }
    const gchar *scan = line;
    while (*scan) {
        GMatchInfo *match;
        if (!g_regex_match((GRegex *)pattern, scan, 0, &match)) {
            g_match_info_free(match);
            return line;
        }
        gint end, separator = -1;
        g_match_info_fetch_pos(match, 0, NULL, &end);
        g_match_info_fetch_named_pos(match, "separator", NULL, &separator);
        g_match_info_free(match);
        if (separator >= 0) return scan + separator;
        scan = url_end(scan + end);
    }
    return line;
}

gboolean
cw_crumb_parse(const gchar *line, gboolean first_line, CwCrumb *out)
{
    *out = (CwCrumb){0};
    const gchar *record = record_start(line, first_line);
    out->record_start = (gint)g_utf8_pointer_to_offset(line, record);
    const gchar *colon = strchr(record, ':');
    if (!colon)
        return FALSE;
    const gchar *header = skip_space(record, colon);
    const gchar *header_end = trim_end(header, colon);
    const gchar *digits = number_start(header, header_end);
    if (!digits)
        return FALSE;

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
#ifdef G_OS_WIN32
    /* wc -w tests unsigned bytes. In FreeBSD 15 C.UTF-8 the space-class
     * values in that range are ASCII whitespace and 0xa0. */
    return c == ' ' || (c >= '\t' && c <= '\r') || c == 0xa0;
#else
    if (iswspace((wint_t)c))
        return TRUE;
#ifdef __GLIBC__
    /* GNU wc also treats these four characters as word separators by default. */
    if (c == 0x00a0 || c == 0x2007 || c == 0x202f || c == 0x2060)
        return getenv("POSIXLY_CORRECT") == NULL;
#endif
    return FALSE;
#endif
}

guint
cw_crumb_words(const gchar *line, gboolean first_line)
{
    const gchar *p = record_start(line, first_line);
#if defined(__FreeBSD__) || defined(__APPLE__) || defined(G_OS_WIN32)
    /* The BSD and macOS wc decode multibyte characters only with -m. Windows
     * follows the FreeBSD command in a UTF-8 locale, without shelling out. */
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

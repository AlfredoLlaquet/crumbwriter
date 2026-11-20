#include "typography.h"

#include <string.h>

struct CwTypography {
    GtkSourceView *view;
    GtkTextBuffer *buffer;
    gboolean enabled;
    gboolean in_key;
    gboolean converting;
    gint typed_at;
    gunichar typed;
    gint pending_at;
    gunichar pending;
    gint length;
    gint elision_at;
    gint elision_end;
    guint departure;
};

static void
forget(CwTypography *type)
{
    type->length = 0;
    type->elision_at = -1;
    if (type->departure) {
        g_source_remove(type->departure);
        type->departure = 0;
    }
}

static gboolean
word_character(gunichar c)
{
    return g_unichar_isalnum(c) || g_unichar_ismark(c);
}

static gunichar
before(GtkTextIter it)
{
    return gtk_text_iter_backward_char(&it) ? gtk_text_iter_get_char(&it) : 0;
}

/* Only read the current physical line, and only for ambiguous closing quotes. */
static gboolean
has_open_quote(GtkTextIter end, gboolean single)
{
    gunichar open = single ? 0x2018 : 0x201c;
    gunichar close = single ? 0x2019 : 0x201d;
    GtkTextIter it = end;
    gtk_text_iter_set_line_offset(&it, 0);
    guint depth = 0;
    while (gtk_text_iter_compare(&it, &end) < 0) {
        gunichar c = gtk_text_iter_get_char(&it);
        if (c == open)
            ++depth;
        else if (depth && c == close) {
            GtkTextIter next = it;
            gtk_text_iter_forward_char(&next);
            if (!single || !word_character(before(it)) ||
                !word_character(gtk_text_iter_get_char(&next)))
                --depth;
        }
        gtk_text_iter_forward_char(&it);
    }
    return depth != 0;
}

static gboolean
closing_quote(GtkTextIter start, GtkTextIter end, gboolean single)
{
    gunichar left = before(start);
    gunichar right = gtk_text_iter_get_char(&end);
    if (word_character(left) || (single && g_unichar_isdigit(right)))
        return TRUE;
    GUnicodeType category = g_unichar_type(left);
    if (category == G_UNICODE_CLOSE_PUNCTUATION || category == G_UNICODE_FINAL_PUNCTUATION)
        return TRUE;
    if (!right || g_unichar_isspace(right) || g_unichar_ispunct(right)) {
        if (left && left < 128 && strchr(".,!?:;", (gchar)left))
            return TRUE;
        return has_open_quote(start, single);
    }
    return FALSE;
}

static void
replace(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, const gchar *text)
{
    gtk_text_buffer_delete(buffer, start, end);
    gtk_text_buffer_insert(buffer, start, text, -1);
}

/*
 * A leading single quote before a word initially opens a quotation. Wait for
 * the word boundary to distinguish, for example, ’tis from ‘tissue’. Only
 * this keyboard-created quote remains eligible for that second decision.
 */
static void
resolve_elision(CwTypography *type, gboolean at_boundary)
{
    static const gchar *const words[] = {
        "tis", "twas", "twere", "twill", "twould", "tween", "twixt",
        "em", "til", "cause", "bout", "cept", "round", "neath", "nuff", "n"
    };
    if (type->elision_at < 0)
        return;
    GtkTextIter start, word, end;
    gtk_text_buffer_get_iter_at_offset(type->buffer, &start, type->elision_at);
    word = start;
    gtk_text_iter_forward_char(&word);
    end = word;
    while (word_character(gtk_text_iter_get_char(&end)) &&
           gtk_text_iter_get_offset(&end) - type->elision_at <= 8)
        gtk_text_iter_forward_char(&end);

    gchar *token = gtk_text_iter_get_text(&word, &end);
    gboolean possible = FALSE;
    if (*token && gtk_text_iter_get_char(&start) == 0x2018) {
        gsize length = strlen(token);
        for (guint i = 0; i < G_N_ELEMENTS(words); ++i) {
            if (g_ascii_strncasecmp(words[i], token, length) != 0)
                continue;
            if (at_boundary && strlen(words[i]) != length)
                continue;
            if (at_boundary)
                replace(type->buffer, &start, &word, "’");
            possible = !at_boundary;
            break;
        }
    }
    if (!possible)
        type->elision_at = -1;
    g_free(token);
}

/* Returns the character-count change before the newly typed lookahead. */
static gint
resolve_pending(CwTypography *type)
{
    gint length = type->length;
    type->length = 0;
    if (length == 0)
        return 0;
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_offset(type->buffer, &start, type->pending_at);
    end = start;
    for (gint i = 0; i < length; ++i) {
        if (gtk_text_iter_get_char(&end) != type->pending)
            return 0;
        gtk_text_iter_forward_char(&end);
    }

    const gchar *replacement = NULL;
    if (type->pending == '\'' || type->pending == '"') {
        gboolean single = type->pending == '\'';
        gboolean close = closing_quote(start, end, single);
        if (single && !close && g_unichar_isalpha(gtk_text_iter_get_char(&end))) {
            type->elision_at = type->pending_at;
            type->elision_end = type->pending_at + 2;
        }
        replacement = single ? (close ? "’" : "‘") : (close ? "”" : "“");
    } else if (type->pending == '-' && length == 2) {
        replacement = "—";
    } else if (type->pending == '.' && length == 3) {
        replacement = "…";
    }
    if (!replacement)
        return 0;
    replace(type->buffer, &start, &end, replacement);
    return 1 - length;
}

void
cw_typography_finish(CwTypography *type)
{
    if ((!type->length && type->elision_at < 0) || type->converting)
        return;
    type->converting = TRUE;
    gtk_text_buffer_begin_user_action(type->buffer);
    resolve_pending(type);
    resolve_elision(type, TRUE);
    forget(type);
    gtk_text_buffer_end_user_action(type->buffer);
    type->converting = FALSE;
}

static gboolean
after_departure(gpointer data)
{
    CwTypography *type = data;
    type->departure = 0;
    cw_typography_finish(type);
    return G_SOURCE_REMOVE;
}

static void
mark_set(GtkTextBuffer *buffer, GtkTextIter *location, GtkTextMark *mark, CwTypography *type)
{
    if (type->in_key || type->converting || type->departure ||
        mark != gtk_text_buffer_get_insert(buffer))
        return;
    gint offset = gtk_text_iter_get_offset(location);
    if ((type->length && offset != type->pending_at + type->length) ||
        (type->elision_at >= 0 && offset != type->elision_end)) {
        /* GTK still owns live iterators in its mark-set emission. */
        type->departure = g_idle_add(after_departure, type);
    }
}

static gboolean
key_press(GtkWidget *view, GdkEventKey *key, CwTypography *type)
{
    if (!type->enabled || type->in_key || !gtk_text_view_get_editable(GTK_TEXT_VIEW(view)))
        return FALSE;
    if (type->departure)
        cw_typography_finish(type);
    GdkModifierType consumed = 0;
    GdkKeymap *map = gdk_keymap_get_for_display(gtk_widget_get_display(view));
    gdk_keymap_translate_keyboard_state(map, key->hardware_keycode, key->state, key->group,
                                        NULL, NULL, NULL, &consumed);
    GdkModifierType command = gtk_accelerator_get_default_mod_mask() & ~GDK_SHIFT_MASK;
    if (key->state & command & ~consumed)
        return FALSE;
    gunichar c = gdk_keyval_to_unicode(key->keyval);
    if ((!c || g_unichar_iscntrl(c)) && key->keyval != GDK_KEY_Return &&
        key->keyval != GDK_KEY_KP_Enter && key->keyval != GDK_KEY_ISO_Enter)
        return FALSE;

    type->in_key = TRUE;
    type->typed_at = -1;
    type->typed = 0;
    gtk_text_buffer_begin_user_action(type->buffer);
    return FALSE;  /* GTK's input method, selection and overwrite handling run normally. */
}

static void
inserted(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint bytes,
         CwTypography *type)
{
    if (type->converting)
        return;
    if (!type->in_key || type->typed || bytes <= 0 || g_utf8_next_char(text) != text + bytes) {
        forget(type);
        type->typed_at = -1;
        type->typed = G_MAXUINT32;   /* Multiple/bulk insertions during this event. */
        return;
    }
    GtkTextIter caret;
    gtk_text_buffer_get_iter_at_mark(buffer, &caret, gtk_text_buffer_get_insert(buffer));
    if (!gtk_text_iter_equal(&caret, location)) {
        forget(type);
        return;
    }
    type->typed = g_utf8_get_char(text);
    type->typed_at = gtk_text_iter_get_offset(location);
}

static void
deleted(GtkTextBuffer *buffer, GtkTextIter *start, GtkTextIter *end, CwTypography *type)
{
    (void)buffer;
    (void)end;
    /* Overwrite mode removes the character ahead of the caret before typing. */
    gint at = gtk_text_iter_get_offset(start);
    if (!type->converting &&
        !(type->in_key && ((type->length && at == type->pending_at + type->length) ||
                           (type->elision_at >= 0 && at == type->elision_end))))
        forget(type);
}

static void
interpret_key(CwTypography *type)
{
    gint at = type->typed_at;
    gunichar c = type->typed;
    if (type->elision_at >= 0 && at != type->elision_end)
        type->elision_at = -1;
    if (type->length && at != type->pending_at + type->length)
        type->length = 0;
    if (type->length && c == type->pending &&
        ((c == '-' && type->length < 2) || (c == '.' && type->length < 3))) {
        ++type->length;
        return;
    }
    at += resolve_pending(type);
    if (type->elision_at >= 0) {
        type->elision_end = at + 1;
        resolve_elision(type, !word_character(c));
    }
    if (c == '\'' || c == '"' || c == '-' || c == '.') {
        type->pending_at = at;
        type->pending = c;
        type->length = 1;
    }
}

static void
event_after(GtkWidget *view, GdkEvent *event, CwTypography *type)
{
    (void)view;
    if (event->type == GDK_KEY_PRESS && type->in_key) {
        type->converting = TRUE;
        if (type->typed_at >= 0)
            interpret_key(type);
        gtk_text_buffer_end_user_action(type->buffer);
        type->in_key = FALSE;
        type->converting = FALSE;
    }
}

static gboolean
focus_out(GtkWidget *view, GdkEventFocus *event, CwTypography *type)
{
    (void)view;
    (void)event;
    cw_typography_finish(type);
    return FALSE;
}

CwTypography *
cw_typography_new(GtkSourceView *view)
{
    CwTypography *type = g_new0(CwTypography, 1);
    type->view = g_object_ref(view);
    type->buffer = g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)));
    type->enabled = TRUE;
    type->elision_at = -1;
    g_signal_connect(view, "key-press-event", G_CALLBACK(key_press), type);
    g_signal_connect(view, "event-after", G_CALLBACK(event_after), type);
    g_signal_connect(view, "focus-out-event", G_CALLBACK(focus_out), type);
    g_signal_connect_swapped(view, "paste-clipboard", G_CALLBACK(cw_typography_finish), type);
    g_signal_connect(type->buffer, "insert-text", G_CALLBACK(inserted), type);
    g_signal_connect(type->buffer, "delete-range", G_CALLBACK(deleted), type);
    g_signal_connect(type->buffer, "mark-set", G_CALLBACK(mark_set), type);
    return type;
}

void
cw_typography_enable(CwTypography *type, gboolean enabled)
{
    if (!enabled)
        forget(type);
    type->enabled = enabled;
}

void
cw_typography_free(CwTypography *type)
{
    if (!type)
        return;
    forget(type);
    if (type->in_key)
        gtk_text_buffer_end_user_action(type->buffer);
    g_signal_handlers_disconnect_by_data(type->buffer, type);
    g_signal_handlers_disconnect_by_data(type->view, type);
    g_object_unref(type->buffer);
    g_object_unref(type->view);
    g_free(type);
}

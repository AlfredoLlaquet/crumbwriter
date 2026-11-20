#include "editor-spacing.h"
#include "spacing.h"
#include <string.h>

#define SPACING_CONTROLLER "cw-editor-spacing"

/* Native GtkTextBuffer edits, including IM commits, replacements and paste,
 * end a user action after the caller has placed the caret. Horizontal space
 * can then be cleaned without splitting the operation's undo group. */
struct CwEditorSpacing {
    GtkTextBuffer *buffer;
    GtkTextMark *first, *last;       /* Edited interval, with outward gravity. */
    GtkTextMark *incoming, *pending; /* End of a possible new or existing separator. */
    gboolean busy, action;
    gint replay;               /* -1 undo, +1 redo, 0 ordinary edit. */
    gint64 history, replay_from;
    GArray *trims;              /* History positions of navigation-only deletions. */
    guint idle, end_signal;
    gulong end_hook;
};

static gboolean
line_break(const GtkTextIter *at)
{
    gunichar c = gtk_text_iter_get_char(at);
    return c == 0 || c == '\n' || c == '\r';
}

static void
drop(CwEditorSpacing *s, GtkTextMark **mark)
{
    if (*mark)
        gtk_text_buffer_delete_mark(s->buffer, *mark);
    *mark = NULL;
}

static void
set_mark(CwEditorSpacing *s, GtkTextMark **mark, const GtkTextIter *pos, gboolean left)
{
    if (*mark)
        gtk_text_buffer_move_mark(s->buffer, *mark, pos);
    else
        *mark = gtk_text_buffer_create_mark(s->buffer, NULL, pos, left);
}

static void
include(CwEditorSpacing *s, const GtkTextIter *lo, const GtkTextIter *hi)
{
    GtkTextIter pos;
    if (!s->first) {
        set_mark(s, &s->first, lo, TRUE);
        set_mark(s, &s->last, hi, FALSE);
        return;
    }
    gtk_text_buffer_get_iter_at_mark(s->buffer, &pos, s->first);
    if (gtk_text_iter_compare(lo, &pos) < 0)
        gtk_text_buffer_move_mark(s->buffer, s->first, lo);
    gtk_text_buffer_get_iter_at_mark(s->buffer, &pos, s->last);
    if (gtk_text_iter_compare(hi, &pos) > 0)
        gtk_text_buffer_move_mark(s->buffer, s->last, hi);
}

static gboolean
at_mark(CwEditorSpacing *s, const GtkTextIter *it, GtkTextMark *mark)
{
    GtkTextIter pos;
    if (!mark)
        return FALSE;
    gtk_text_buffer_get_iter_at_mark(s->buffer, &pos, mark);
    return gtk_text_iter_equal(it, &pos);
}

static gboolean
pending_here(CwEditorSpacing *s, GtkTextIter *at)
{
    return line_break(at) && at_mark(s, at, gtk_text_buffer_get_insert(s->buffer)) &&
           !gtk_text_buffer_get_has_selection(s->buffer) &&
           (at_mark(s, at, s->incoming) || at_mark(s, at, s->pending));
}

/* Keep cursor/selection marks at the beginning of a replacement on their own
 * side of it. Marks inside the old run land after the first surviving space. */
static void
replace_run(GtkTextBuffer *buffer, GtkTextIter *first, GtkTextIter *end,
            const gchar *replacement)
{
    gint offset = gtk_text_iter_get_offset(first);
    gtk_text_buffer_insert(buffer, end, replacement, 1);
    gtk_text_iter_backward_char(end);
    gtk_text_buffer_get_iter_at_offset(buffer, first, offset);
    gtk_text_buffer_delete(buffer, first, end);
    gtk_text_iter_forward_char(first);
}

static void
clean_interval(CwEditorSpacing *s, GtkTextIter pos)
{
    GtkTextIter prev = pos;
    while (gtk_text_iter_backward_char(&prev) && cw_spacing_is_horizontal(gtk_text_iter_get_char(&prev)))
        pos = prev;
    GtkTextIter limit;
    gtk_text_buffer_get_iter_at_mark(s->buffer, &limit, s->last);
    while (gtk_text_iter_compare(&pos, &limit) <= 0 && !gtk_text_iter_is_end(&pos)) {
        gunichar c = gtk_text_iter_get_char(&pos);
        GtkTextIter end = pos;
        gtk_text_iter_forward_char(&end);
        if (c == '\r') {
            if (gtk_text_iter_get_char(&end) == '\n')
                gtk_text_buffer_delete(s->buffer, &pos, &end);
            else
                replace_run(s->buffer, &pos, &end, "\n");
        } else if (cw_spacing_is_horizontal(c)) {
            while (cw_spacing_is_horizontal(gtk_text_iter_get_char(&end)))
                gtk_text_iter_forward_char(&end);
            if (line_break(&end) && !pending_here(s, &end)) {
                gtk_text_buffer_delete(s->buffer, &pos, &end);
            } else if (c == '\t') {
                replace_run(s->buffer, &pos, &end, " ");
            } else {
                gtk_text_iter_forward_char(&pos);
                if (!gtk_text_iter_equal(&pos, &end))
                    gtk_text_buffer_delete(s->buffer, &pos, &end);
            }
        } else {
            pos = end;
            continue;
        }
        gtk_text_buffer_get_iter_at_mark(s->buffer, &limit, s->last);
    }
}

static void
finish(CwEditorSpacing *s, gboolean depart)
{
    if (s->busy || s->replay)
        return;
    s->busy = TRUE;
    if (s->idle) {
        g_source_remove(s->idle);
        s->idle = 0;
    }
    GtkTextIter pos;
    if (s->pending) {
        gtk_text_buffer_get_iter_at_mark(s->buffer, &pos, s->pending);
        GtkTextIter before = pos;
        if (line_break(&pos) && (depart || !pending_here(s, &pos)) &&
            gtk_text_iter_backward_char(&before) && cw_spacing_is_horizontal(gtk_text_iter_get_char(&before))) {
            gtk_text_buffer_delete(s->buffer, &before, &pos);
            if (!s->action)
                g_array_append_val(s->trims, s->history);
        }
    }
    if (s->first) {
        gtk_text_buffer_get_iter_at_mark(s->buffer, &pos, s->first);
        clean_interval(s, pos);
    }
    gtk_text_buffer_get_iter_at_mark(s->buffer, &pos, gtk_text_buffer_get_insert(s->buffer));
    GtkTextIter before = pos;
    if (!depart && pending_here(s, &pos) && gtk_text_iter_backward_char(&before) &&
        gtk_text_iter_get_char(&before) == ' ')
        set_mark(s, &s->pending, &pos, TRUE);
    else
        drop(s, &s->pending);
    drop(s, &s->incoming);
    drop(s, &s->first);
    drop(s, &s->last);
    s->busy = FALSE;
}

static gboolean
on_idle(gpointer data)
{
    CwEditorSpacing *s = data;
    s->idle = 0;
    if (!s->action)
        finish(s, FALSE);
    return G_SOURCE_REMOVE;
}

static void
queue(CwEditorSpacing *s)
{
    if (!s->action && !s->idle)
        s->idle = g_idle_add_full(G_PRIORITY_HIGH_IDLE, on_idle, s, NULL);
}

static void
count_change(CwEditorSpacing *s, gint characters)
{
    if (!s->replay) {
        while (s->trims->len && g_array_index(s->trims, gint64, s->trims->len - 1) > s->history)
            g_array_set_size(s->trims, s->trims->len - 1);
    }
    /* The number of characters inserted plus deleted is additive even when
     * GtkSourceView merges several keystrokes into one saved undo action. */
    s->history += s->replay < 0 ? -characters : characters;
}

static void
before_insert(GtkTextBuffer *buffer, GtkTextIter *at, const gchar *text, gint len,
              CwEditorSpacing *s)
{
    (void)buffer;
    count_change(s, (gint)g_utf8_strlen(text, len));
    if (s->busy || !len)
        return;
    if (!s->replay && cw_spacing_is_horizontal((guchar)text[len - 1]))
        set_mark(s, &s->incoming, at, FALSE);
    if (s->pending || text[0] == '\n' || memchr(text, ' ', len) || memchr(text, '\t', len) || memchr(text, '\r', len) ||
        gtk_text_iter_get_char(at) == ' ') {
        include(s, at, at);
        queue(s);
    }
}

static void
before_delete(GtkTextBuffer *buffer, GtkTextIter *lo, GtkTextIter *hi, CwEditorSpacing *s)
{
    (void)buffer;
    count_change(s, gtk_text_iter_get_offset(hi) - gtk_text_iter_get_offset(lo));
    if (s->busy)
        return;
    GtkTextIter prev = *lo;
    if (s->pending || cw_spacing_is_horizontal(gtk_text_iter_get_char(hi)) ||
        (gtk_text_iter_backward_char(&prev) && cw_spacing_is_horizontal(gtk_text_iter_get_char(&prev)))) {
        include(s, lo, hi);
        queue(s);
    }
}

static void
mark_set(GtkTextBuffer *buffer, GtkTextIter *pos, GtkTextMark *mark, CwEditorSpacing *s)
{
    if (!s->busy && !s->replay && s->pending && mark == gtk_text_buffer_get_insert(buffer) &&
        !at_mark(s, pos, s->pending))
        queue(s);
}

static void
begin_action(GtkTextBuffer *buffer, CwEditorSpacing *s)
{
    (void)buffer;
    s->action = TRUE;
}

/* GtkSourceView's undo manager commits on its normal end-user-action handler.
 * The emission hook runs before that handler, on the same main-loop turn. */
static gboolean
end_action(GSignalInvocationHint *hint, guint count, const GValue *args, gpointer data)
{
    (void)hint;
    (void)count;
    CwEditorSpacing *s = data;
    if (g_value_get_object(args) == (gpointer)s->buffer) {
        if (s->first || s->pending)
            finish(s, FALSE);
        s->action = FALSE;
    }
    return TRUE;
}

static void
before_replay(GtkSourceBuffer *buffer, CwEditorSpacing *s)
{
    GSignalInvocationHint *hint = g_signal_get_invocation_hint(buffer);
    s->replay = hint->signal_id == g_signal_lookup("undo", GTK_SOURCE_TYPE_BUFFER) ? -1 : 1;
    s->replay_from = s->history;
    if (s->pending) {
        GtkTextIter at;
        gtk_text_buffer_get_iter_at_mark(s->buffer, &at, s->pending);
        include(s, &at, &at);
        drop(s, &s->pending);
    }
    drop(s, &s->incoming);
}

static gboolean
navigation_trim(CwEditorSpacing *s, gint64 at)
{
    for (guint i = s->trims->len; i > 0; i--) {
        gint64 value = g_array_index(s->trims, gint64, i - 1);
        if (value <= at)
            return value == at;
    }
    return FALSE;
}

static void
after_replay(GtkSourceBuffer *buffer, CwEditorSpacing *s)
{
    GtkSourceUndoManager *history = gtk_source_buffer_get_undo_manager(buffer);
    if (s->replay < 0 && s->replay_from - s->history == 1 &&
        navigation_trim(s, s->replay_from) && gtk_source_buffer_can_undo(buffer))
        gtk_source_undo_manager_undo(history);
    else if (s->replay > 0 && navigation_trim(s, s->history + 1) && gtk_source_buffer_can_redo(buffer))
        gtk_source_undo_manager_redo(history);

    /* Undoing a multi-location operation can restore the provisional space
     * in its former crumb while GTK puts the cursor at its first edit (e.g.
     * an appended header at EOF). Resume writing at that restored separator. */
    if (s->first) {
        GtkTextIter pos, end;
        gtk_text_buffer_get_iter_at_mark(s->buffer, &pos, s->first);
        gtk_text_buffer_get_iter_at_mark(s->buffer, &end, s->last);
        do {
            if (!gtk_text_iter_ends_line(&pos))
                gtk_text_iter_forward_to_line_end(&pos);
            GtkTextIter prev = pos;
            if (gtk_text_iter_backward_char(&prev) && gtk_text_iter_get_char(&prev) == ' ') {
                set_mark(s, &s->pending, &pos, TRUE);
                if (!at_mark(s, &pos, gtk_text_buffer_get_insert(s->buffer)))
                    gtk_text_buffer_place_cursor(s->buffer, &pos);
                break;
            }
        } while (gtk_text_iter_forward_line(&pos) && gtk_text_iter_compare(&pos, &end) <= 0);
    }
    drop(s, &s->first);
    drop(s, &s->last);
    s->replay = 0;
}

void
cw_editor_spacing_reset(CwEditorSpacing *s)
{
    if (s->idle)
        g_source_remove(s->idle);
    s->idle = 0;
    drop(s, &s->first);
    drop(s, &s->last);
    drop(s, &s->incoming);
    drop(s, &s->pending);
    s->history = 0;
    g_array_set_size(s->trims, 0);
}

void
cw_editor_spacing_depart(CwEditorSpacing *s)
{
    if (s->pending)
        finish(s, TRUE);
}

void
cw_editor_spacing_finish_input(GtkTextBuffer *buffer)
{
    CwEditorSpacing *s = g_object_get_data(G_OBJECT(buffer), SPACING_CONTROLLER);
    if (s)
        cw_editor_spacing_depart(s);
}

gboolean
cw_editor_spacing_is_cleaning(GtkTextBuffer *buffer)
{
    CwEditorSpacing *s = g_object_get_data(G_OBJECT(buffer), SPACING_CONTROLLER);
    return s && s->busy;
}

CwEditorSpacing *
cw_editor_spacing_new(GtkTextBuffer *buffer)
{
    CwEditorSpacing *s = g_new0(CwEditorSpacing, 1);
    s->buffer = buffer;
    g_object_set_data(G_OBJECT(buffer), SPACING_CONTROLLER, s);
    s->trims = g_array_new(FALSE, FALSE, sizeof(gint64));
    s->end_signal = g_signal_lookup("end-user-action", GTK_TYPE_TEXT_BUFFER);
    s->end_hook = g_signal_add_emission_hook(s->end_signal, 0, end_action, s, NULL);
    g_signal_connect(buffer, "begin-user-action", G_CALLBACK(begin_action), s);
    g_signal_connect(buffer, "insert-text", G_CALLBACK(before_insert), s);
    g_signal_connect(buffer, "delete-range", G_CALLBACK(before_delete), s);
    g_signal_connect(buffer, "mark-set", G_CALLBACK(mark_set), s);
    g_signal_connect(buffer, "undo", G_CALLBACK(before_replay), s);
    g_signal_connect(buffer, "redo", G_CALLBACK(before_replay), s);
    g_signal_connect_after(buffer, "undo", G_CALLBACK(after_replay), s);
    g_signal_connect_after(buffer, "redo", G_CALLBACK(after_replay), s);
    return s;
}

void
cw_editor_spacing_free(CwEditorSpacing *s)
{
    g_object_set_data(G_OBJECT(s->buffer), SPACING_CONTROLLER, NULL);
    g_signal_remove_emission_hook(s->end_signal, s->end_hook);
    g_signal_handlers_disconnect_by_data(s->buffer, s);
    cw_editor_spacing_reset(s);
    g_array_unref(s->trims);
    g_free(s);
}

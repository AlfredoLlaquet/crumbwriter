#include "virtual.h"

#include "reveal.h"

/* Row zero is the actual buffer. Other rows are relative to the display line
 * that contains its end iterator. They have one insertion position, column 0. */
struct CwVirtual {
    GtkTextView *view;
    GtkTextBuffer *buffer;
    void (*changed)(gpointer);
    gpointer data;
    guint rows;
    gboolean placing;
    gboolean native_visible;
    gboolean user_action;
    gboolean materializing;
};

guint
cw_virtual_rows(const CwVirtual *space)
{
    return space ? space->rows : 0;
}

static void
set_rows(CwVirtual *space, guint rows)
{
    if (space->rows == rows)
        return;
    if (rows && !space->rows) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(space->buffer, &end);
        space->native_visible = gtk_text_view_get_cursor_visible(space->view);
        gtk_text_view_set_cursor_visible(space->view, FALSE);
        space->rows = rows;
        space->placing = TRUE;
        gtk_text_buffer_place_cursor(space->buffer, &end);
        space->placing = FALSE;
    } else {
        space->rows = rows;
        if (!rows)
            gtk_text_view_set_cursor_visible(space->view, space->native_visible);
    }
    cw_reveal_virtual_cursor(space->view, rows);
    gtk_widget_queue_draw(GTK_WIDGET(space->view));
    if (space->changed && !space->materializing)
        space->changed(space->data);
}

void
cw_virtual_clear(CwVirtual *space)
{
    if (space && space->rows)
        set_rows(space, 0);
}

static gboolean
on_last_display_line(CwVirtual *space)
{
    GtkTextIter here, end;
    gtk_text_buffer_get_iter_at_mark(space->buffer, &here, gtk_text_buffer_get_insert(space->buffer));
    gtk_text_buffer_get_end_iter(space->buffer, &end);
    if (gtk_text_iter_get_line(&here) != gtk_text_iter_get_line(&end))
        return FALSE;
    if (!gtk_text_view_starts_display_line(space->view, &end))
        gtk_text_view_backward_display_line_start(space->view, &end);
    return gtk_text_iter_compare(&here, &end) >= 0;
}

static void
up(CwVirtual *space)
{
    set_rows(space, space->rows - 1);
    if (!space->rows)
        cw_reveal_cursor(space->view);
}

gboolean
cw_virtual_key(CwVirtual *space, GdkEventKey *key)
{
    GdkModifierType mods = key->state & gtk_accelerator_get_default_mod_mask();
    switch (key->keyval) {
    case GDK_KEY_Down: case GDK_KEY_KP_Down:
        if (mods == 0 && !gtk_text_buffer_get_has_selection(space->buffer) &&
            (space->rows || on_last_display_line(space))) {
            /* GtkTextBuffer character offsets and pixel coordinates are gint. */
            if (space->rows < (guint)(G_MAXINT - gtk_text_buffer_get_char_count(space->buffer)))
                set_rows(space, space->rows + 1);
            return TRUE;
        }
        break;
    case GDK_KEY_Up: case GDK_KEY_KP_Up:
    case GDK_KEY_Left: case GDK_KEY_KP_Left: case GDK_KEY_BackSpace:
        if (space->rows && mods == 0) {
            up(space);
            return TRUE;
        }
        if (space->rows && key->keyval == GDK_KEY_BackSpace) {
            cw_virtual_clear(space);
            cw_reveal_cursor(space->view);
            return TRUE;
        }
        break;
    case GDK_KEY_Delete: case GDK_KEY_KP_Delete:
        if (space->rows)
            return TRUE;
        break;
    case GDK_KEY_Right: case GDK_KEY_KP_Right:
    case GDK_KEY_Home: case GDK_KEY_KP_Home:
    case GDK_KEY_Begin: case GDK_KEY_KP_Begin:
    case GDK_KEY_End: case GDK_KEY_KP_End:
        if (space->rows && mods == 0)
            return TRUE;
        break;
    case GDK_KEY_Escape:
        if (space->rows && !mods) {
            cw_virtual_clear(space);
            cw_reveal_cursor(space->view);
            return TRUE;
        }
        return FALSE;
    case GDK_KEY_Tab: case GDK_KEY_ISO_Left_Tab: case GDK_KEY_KP_Tab:
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down:
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up:
        break;
    default:
        /* Typing, compose keys and paste retain the logical insertion point. */
        return FALSE;
    }
    cw_virtual_clear(space);
    return FALSE;
}

static void
mark_set(GtkTextBuffer *buffer, GtkTextIter *location, GtkTextMark *mark, CwVirtual *space)
{
    (void)location;
    if (!space->placing && mark == gtk_text_buffer_get_insert(buffer))
        cw_virtual_clear(space);
}

static void
begin_action(GtkTextBuffer *buffer, CwVirtual *space)
{
    (void)buffer;
    space->user_action = TRUE;
}

static void
end_action(GtkTextBuffer *buffer, CwVirtual *space)
{
    (void)buffer;
    space->user_action = FALSE;
}

static void
deleted(GtkTextBuffer *buffer, GtkTextIter *a, GtkTextIter *b, CwVirtual *space)
{
    (void)buffer;
    (void)a;
    (void)b;
    cw_virtual_clear(space);
}

static void
after_insert(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint length,
             CwVirtual *space)
{
    if (!space->rows)
        return;
    guint rows = space->rows;
    GtkTextIter caret;
    gtk_text_buffer_get_iter_at_mark(buffer, &caret, gtk_text_buffer_get_insert(buffer));
    gboolean committed = space->user_action && length > 0 && gtk_text_iter_is_end(location) &&
                         gtk_text_iter_equal(location, &caret);
    space->materializing = committed;
    cw_virtual_clear(space);
    if (!committed)
        return;

    /* GTK has already inserted the committed text and recorded its undo.
     * Insert the empty lines before it, within the SAME user action. This
     * works for IM commits and asynchronous clipboard delivery, and leaves
     * every GtkSourceBuffer undo offset valid in both directions. */
    GtkTextIter start = *location;
    gtk_text_iter_backward_chars(&start, (gint)g_utf8_strlen(text, length));
    g_autofree gchar *breaks = g_strnfill(rows, '\n');
    gtk_text_buffer_insert(buffer, &start, breaks, (gint)rows);
    space->materializing = FALSE;
    gtk_text_buffer_get_end_iter(buffer, location);
    cw_reveal_cursor(space->view);
}

static gboolean
draw_caret(GtkWidget *widget, cairo_t *cr, CwVirtual *space)
{
    if (!space->rows || !gtk_widget_has_focus(widget))
        return FALSE;
    GdkWindow *window = gtk_text_view_get_window(space->view, GTK_TEXT_WINDOW_TEXT);
    if (!window || !gtk_cairo_should_draw_window(cr, window))
        return FALSE;
    GdkRectangle rect;
    cw_reveal_virtual_rect(space->view, space->rows, &rect);
    gint x, y;
    gtk_text_view_buffer_to_window_coords(space->view, GTK_TEXT_WINDOW_TEXT, rect.x, rect.y, &x, &y);
    GtkStyleContext *style = gtk_widget_get_style_context(widget);
    GdkRGBA color;
    gtk_style_context_get_color(style, gtk_style_context_get_state(style), &color);
    cairo_save(cr);
    gtk_cairo_transform_to_window(cr, widget, window);
    gdk_cairo_set_source_rgba(cr, &color);
    cairo_rectangle(cr, x, y, 2, MAX(rect.height, 1));
    cairo_fill(cr);
    cairo_restore(cr);
    return FALSE;
}

static gboolean
mouse_press(GtkWidget *widget, GdkEventButton *event, CwVirtual *space)
{
    if (event->button != GDK_BUTTON_PRIMARY || event->type != GDK_BUTTON_PRESS ||
        (event->state & gtk_accelerator_get_default_mod_mask()) ||
        event->window != gtk_text_view_get_window(space->view, GTK_TEXT_WINDOW_TEXT))
        return FALSE;

    gint by;
    gtk_text_view_window_to_buffer_coords(space->view, GTK_TEXT_WINDOW_TEXT,
                                          (gint)event->x, (gint)event->y, NULL, &by);
    GtkTextIter nearby;
    gtk_text_view_get_line_at_y(space->view, &nearby, by, NULL);
    if (gtk_text_iter_get_line(&nearby) + 1 < gtk_text_buffer_get_line_count(space->buffer)) {
        cw_virtual_clear(space);
        return FALSE;
    }
    GdkRectangle first, second;
    cw_reveal_virtual_rect(space->view, 1, &first);
    cw_reveal_virtual_rect(space->view, 2, &second);
    gint pitch = MAX(1, second.y - first.y);
    gint from = first.y - MAX(0, pitch - first.height) / 2;
    if (by < from) {
        cw_virtual_clear(space);
        return FALSE;
    }
    gtk_widget_grab_focus(widget);
    set_rows(space, 1 + (guint)(by - from) / (guint)pitch);
    return TRUE;
}

CwVirtual *
cw_virtual_new(GtkSourceView *view, void (*changed)(gpointer), gpointer data)
{
    CwVirtual *space = g_new0(CwVirtual, 1);
    space->view = g_object_ref(GTK_TEXT_VIEW(view));
    space->buffer = g_object_ref(gtk_text_view_get_buffer(space->view));
    space->changed = changed;
    space->data = data;
    g_signal_connect_after(view, "draw", G_CALLBACK(draw_caret), space);
    g_signal_connect(view, "button-press-event", G_CALLBACK(mouse_press), space);
    g_signal_connect(space->buffer, "mark-set", G_CALLBACK(mark_set), space);
    g_signal_connect(space->buffer, "begin-user-action", G_CALLBACK(begin_action), space);
    g_signal_connect(space->buffer, "end-user-action", G_CALLBACK(end_action), space);
    g_signal_connect(space->buffer, "delete-range", G_CALLBACK(deleted), space);
    g_signal_connect_after(space->buffer, "insert-text", G_CALLBACK(after_insert), space);
    g_signal_connect_swapped(space->buffer, "undo", G_CALLBACK(cw_virtual_clear), space);
    g_signal_connect_swapped(space->buffer, "redo", G_CALLBACK(cw_virtual_clear), space);
    return space;
}

void
cw_virtual_free(CwVirtual *space)
{
    if (!space)
        return;
    g_signal_handlers_disconnect_by_data(space->buffer, space);
    g_signal_handlers_disconnect_by_data(space->view, space);
    space->changed = NULL;
    GtkWidget *view = GTK_WIDGET(space->view);
    if (space->rows && gtk_widget_get_realized(view) &&
        !gtk_widget_in_destruction(view) && !gtk_widget_in_destruction(gtk_widget_get_toplevel(view)))
        cw_virtual_clear(space);
    g_object_unref(space->buffer);
    g_object_unref(space->view);
    g_free(space);
}

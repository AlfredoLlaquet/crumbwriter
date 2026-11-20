#include "reveal.h"

#define REVEAL_IDLE "cw-reveal-idle"
#define ADJUSTING "cw-reveal-adjusting"

static void
cancel_idle(gpointer data)
{
    g_source_remove(GPOINTER_TO_UINT(data));
}

static void
adjust_to_cursor(GtkTextView *view, gboolean measuring)
{
    if (!gtk_widget_get_mapped(GTK_WIDGET(view)))
        return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    if (measuring) {
        /* GTK assigns zero height to a paragraph it has yet to measure. */
        gint height;
        gtk_text_view_get_line_yrange(view, &cursor, NULL, &height);
        if (height == 0)
            return;
    }

    GdkRectangle visible, caret;
    gtk_text_view_get_visible_rect(view, &visible);
    gtk_text_view_get_iter_location(view, &cursor, &caret);
    GtkAdjustment *adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    gdouble value = gtk_adjustment_get_value(adjustment);
    gint margin = measuring ? visible.height / 2 : MIN(visible.height / 8, caret.height * 2);
    if (caret.y < visible.y)
        value += caret.y - visible.y - margin;
    else if (caret.y + caret.height > visible.y + visible.height)
        value += caret.y + caret.height - visible.y - visible.height + margin;

    /* Set even when already in view to stop a pending animation to an estimated
     * pixel. GTK can now keep this visible paragraph anchored as wrapping moves
     * the preceding paragraphs. */
    gtk_adjustment_set_value(adjustment, value);
}

static void
layout_changed(GtkAdjustment *adjustment, GtkTextView *view)
{
    (void)adjustment;
    /* When another group of paragraphs acquires its wrapped height, position
     * the caret within the new bounds before GTK paints the viewport. Changing
     * its position can in turn cause GTK to measure more visible paragraphs. */
    if (!g_object_get_data(G_OBJECT(view), REVEAL_IDLE) ||
        g_object_get_data(G_OBJECT(view), ADJUSTING))
        return;
    g_object_set_data(G_OBJECT(view), ADJUSTING, GINT_TO_POINTER(1));
    adjust_to_cursor(view, TRUE);
    g_object_set_data(G_OBJECT(view), ADJUSTING, NULL);
}

static gboolean
finish_scroll(gpointer data)
{
    g_object_steal_data(G_OBJECT(data), REVEAL_IDLE);
    adjust_to_cursor(GTK_TEXT_VIEW(data), FALSE);
    return G_SOURCE_REMOVE;
}

static gboolean
browse_manually(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)widget;
    (void)event;
    /* A wheel or scrollbar gesture after a jump takes precedence over a
     * pending correction, even while a newly opened document is being laid out. */
    g_object_set_data(G_OBJECT(data), REVEAL_IDLE, NULL);
    return FALSE;
}

static void
watch_scrolling(GtkTextView *view)
{
    if (g_object_get_data(G_OBJECT(view), "cw-watches-scroll"))
        return;
    GtkWidget *scroll = gtk_widget_get_ancestor(GTK_WIDGET(view), GTK_TYPE_SCROLLED_WINDOW);
    if (!scroll)
        return;
    GtkAdjustment *adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    g_signal_connect_object(adjustment, "changed", G_CALLBACK(layout_changed), view, G_CONNECT_AFTER);
    GtkWidget *bar = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(scroll));
    g_signal_connect_object(scroll, "scroll-event", G_CALLBACK(browse_manually), view, 0);
    g_signal_connect_object(bar, "button-press-event", G_CALLBACK(browse_manually), view, 0);
    g_object_set_data(G_OBJECT(view), "cw-watches-scroll", GINT_TO_POINTER(1));
}

void
cw_reveal_cursor(GtkTextView *view)
{
    watch_scrolling(view);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
    gtk_text_view_scroll_mark_onscreen(view, gtk_text_buffer_get_insert(buffer));
    if (!g_object_get_data(G_OBJECT(view), REVEAL_IDLE)) {
        /* Settle once GTK has measured the preceding wrapped paragraphs. */
        guint id = g_idle_add_full(GTK_TEXT_VIEW_PRIORITY_VALIDATE + 1, finish_scroll, view, NULL);
        g_object_set_data_full(G_OBJECT(view), REVEAL_IDLE, GUINT_TO_POINTER(id), cancel_idle);
    }
}

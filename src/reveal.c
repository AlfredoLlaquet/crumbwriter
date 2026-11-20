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

static void
copy_scroll_bounds(GtkAdjustment *document, GtkAdjustment *scrollbar)
{
    gtk_adjustment_configure(scrollbar, gtk_adjustment_get_value(document),
                             gtk_adjustment_get_lower(document), gtk_adjustment_get_upper(document),
                             gtk_adjustment_get_step_increment(document),
                             gtk_adjustment_get_page_increment(document),
                             gtk_adjustment_get_page_size(document));
}

static gboolean
scrollbar_change(GtkRange *range, GtkScrollType step, gdouble value, gpointer data)
{
    (void)step;
    (void)data;
    gtk_range_set_value(range, value);
    return TRUE;
}

static void
connect_scrollbar(GtkAdjustment *document, GtkWidget *bar)
{
    GtkAdjustment *scrollbar = gtk_range_get_adjustment(GTK_RANGE(bar));
    copy_scroll_bounds(document, scrollbar);
    g_signal_connect_object(document, "changed", G_CALLBACK(copy_scroll_bounds), scrollbar, 0);
    g_object_bind_property(document, "value", scrollbar, "value", G_BINDING_BIDIRECTIONAL);
    g_signal_connect(bar, "change-value", G_CALLBACK(scrollbar_change), NULL);
}

static gboolean
scroll_child(GtkScrolledWindow *scroll, GtkScrollType step, gboolean horizontal, gpointer data)
{
    (void)data;
    if (step == GTK_SCROLL_STEP_UP || step == GTK_SCROLL_STEP_DOWN ||
        step == GTK_SCROLL_PAGE_UP || step == GTK_SCROLL_PAGE_DOWN)
        horizontal = FALSE;
    else if (step == GTK_SCROLL_STEP_LEFT || step == GTK_SCROLL_STEP_RIGHT ||
             step == GTK_SCROLL_PAGE_LEFT || step == GTK_SCROLL_PAGE_RIGHT)
        horizontal = TRUE;
    GtkWidget *bar = horizontal ? gtk_scrolled_window_get_hscrollbar(scroll) :
                                  gtk_scrolled_window_get_vscrollbar(scroll);
    if (!gtk_widget_get_child_visible(bar))
        return FALSE;
    g_signal_emit_by_name(bar, "move-slider", step);
    return TRUE;
}

static gboolean
browse_manually(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)widget;
    GtkTextView *view = GTK_TEXT_VIEW(data);
    g_object_set_data(G_OBJECT(view), REVEAL_IDLE, NULL);
    gdouble dx, dy;
    if (event->type != GDK_SCROLL || !gdk_event_get_scroll_deltas(event, &dx, &dy))
        return GDK_EVENT_PROPAGATE;

    /* Apply each touchpad delta immediately. Consuming its final zero-delta
     * event prevents GtkScrolledWindow from starting a deceleration animation. */
    GtkAdjustment *doc = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    gdouble delta = event->scroll.state & GDK_SHIFT_MASK ? dx : dy;
    gtk_adjustment_set_value(doc, gtk_adjustment_get_value(doc) +
                                  delta * gtk_adjustment_get_step_increment(doc));
    return GDK_EVENT_STOP;
}

typedef struct {
    GtkTextView *view;
    GtkGesture *pan, *hold;
    gdouble start;
    gboolean moved;
} TouchScroll;

static void
free_touch_scroll(gpointer data)
{
    TouchScroll *touch = data;
    g_object_unref(touch->pan);
    g_object_unref(touch->hold);
    g_free(touch);
}

static void
pan_begin(GtkGestureDrag *gesture, gdouble x, gdouble y, TouchScroll *touch)
{
    (void)x;
    (void)y;
    GtkAdjustment *doc = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(touch->view));
    GdkEventSequence *sequence = gtk_gesture_get_last_updated_sequence(GTK_GESTURE(gesture));
    const GdkEvent *event = gtk_gesture_get_last_event(GTK_GESTURE(gesture), sequence);
    if (!event || gtk_get_event_widget((GdkEvent *)event) != GTK_WIDGET(touch->view) ||
        gtk_adjustment_get_upper(doc) - gtk_adjustment_get_lower(doc) <= gtk_adjustment_get_page_size(doc)) {
        gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_DENIED);
        return;
    }
    /* Hold the touch until it becomes a pan, tap or long press. A pan must
     * leave the document caret and selection where the writer put them. */
    touch->moved = FALSE;
    touch->start = gtk_adjustment_get_value(doc);
    gtk_gesture_set_sequence_state(GTK_GESTURE(gesture), sequence, GTK_EVENT_SEQUENCE_CLAIMED);
}

static void
pan_motion(GtkGesturePan *gesture, GtkPanDirection direction, gdouble offset, TouchScroll *touch)
{
    (void)gesture;
    if (!touch->moved && !gtk_drag_check_threshold(GTK_WIDGET(touch->view), 0, 0, 0, (gint)offset))
        return;
    touch->moved = TRUE;
    g_object_set_data(G_OBJECT(touch->view), REVEAL_IDLE, NULL);
    GtkAdjustment *doc = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(touch->view));
    gtk_adjustment_set_value(doc, touch->start + (direction == GTK_PAN_DIRECTION_UP ? offset : -offset));
}

static void
pan_end(GtkGesture *gesture, GdkEventSequence *sequence, TouchScroll *touch)
{
    if (!touch->moved)
        gtk_gesture_set_sequence_state(gesture, sequence, GTK_EVENT_SEQUENCE_DENIED);
}

static void
release_long_press(GtkGesture *gesture, gdouble x, gdouble y, gpointer data)
{
    (void)x;
    (void)y;
    (void)data;
    gtk_gesture_set_state(gesture, GTK_EVENT_SEQUENCE_DENIED);
}

void
cw_reveal_configure(GtkTextView *view)
{
    if (g_object_get_data(G_OBJECT(view), "cw-document-scrolling"))
        return;
    GtkWidget *parent = gtk_widget_get_ancestor(GTK_WIDGET(view), GTK_TYPE_SCROLLED_WINDOW);
    if (!parent)
        return;
    GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW(parent);
    gtk_scrolled_window_set_kinetic_scrolling(scroll, FALSE);

    /* A mapped GtkScrolledWindow animates its own adjustments. Give the text
     * view separate adjustments so its native cursor, selection and page
     * actions land immediately. The standard scrollbars follow both ways. */
    gtk_scrollable_set_vadjustment(GTK_SCROLLABLE(view), NULL);
    gtk_scrollable_set_hadjustment(GTK_SCROLLABLE(view), NULL);
    GtkAdjustment *doc = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    GtkWidget *bar = gtk_scrolled_window_get_vscrollbar(scroll);
    connect_scrollbar(doc, bar);
    connect_scrollbar(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(view)),
                      gtk_scrolled_window_get_hscrollbar(scroll));
    g_signal_connect_object(doc, "changed", G_CALLBACK(layout_changed), view, G_CONNECT_AFTER);
    g_signal_connect_object(scroll, "scroll-child", G_CALLBACK(scroll_child), view, 0);
    g_signal_connect_object(scroll, "scroll-event", G_CALLBACK(browse_manually), view, 0);
    g_signal_connect_object(bar, "button-press-event", G_CALLBACK(browse_manually), view, 0);

    TouchScroll *touch = g_new0(TouchScroll, 1);
    touch->view = view;
    touch->pan = gtk_gesture_pan_new(parent, GTK_ORIENTATION_VERTICAL);
    touch->hold = gtk_gesture_long_press_new(parent);
    gtk_gesture_group(touch->hold, touch->pan);
    gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(touch->pan), TRUE);
    gtk_gesture_single_set_touch_only(GTK_GESTURE_SINGLE(touch->hold), TRUE);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(touch->pan), GTK_PHASE_CAPTURE);
    gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(touch->hold), GTK_PHASE_CAPTURE);
    g_signal_connect(touch->pan, "drag-begin", G_CALLBACK(pan_begin), touch);
    g_signal_connect(touch->pan, "pan", G_CALLBACK(pan_motion), touch);
    g_signal_connect(touch->pan, "end", G_CALLBACK(pan_end), touch);
    g_signal_connect(touch->hold, "pressed", G_CALLBACK(release_long_press), NULL);
    g_object_set_data_full(G_OBJECT(view), "cw-document-scrolling", touch, free_touch_scroll);
}

void
cw_reveal_cursor(GtkTextView *view)
{
    cw_reveal_configure(view);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
    gtk_text_view_scroll_mark_onscreen(view, gtk_text_buffer_get_insert(buffer));
    if (!g_object_get_data(G_OBJECT(view), REVEAL_IDLE)) {
        /* Settle once GTK has measured the preceding wrapped paragraphs. */
        guint id = g_idle_add_full(GTK_TEXT_VIEW_PRIORITY_VALIDATE + 1, finish_scroll, view, NULL);
        g_object_set_data_full(G_OBJECT(view), REVEAL_IDLE, GUINT_TO_POINTER(id), cancel_idle);
    }
}

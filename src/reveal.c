#include "reveal.h"

#define REVEAL_IDLE "cw-reveal-idle"
#define SCROLL_STATE "cw-document-scrolling"

typedef enum { KEEP_VISIBLE, CENTER_LINE, TOP_LINE } Placement;
typedef enum { DURING_LAYOUT, REQUESTED_LANDING, FINAL_LANDING } LandingPass;

typedef struct {
    GtkTextView *view;
    GtkGesture *pan, *hold;
    gdouble start;
    gboolean moved, adjusting;
    Placement placement;
    gint anchor_offset, original_top, original_bottom, line_height;
    gint viewport_width, viewport_height, normal_bottom;
    gint normal_left, normal_right;
    gboolean compact;
    guint virtual_rows;
} DocumentScroll;

static void
cancel_idle(gpointer data)
{
    g_source_remove(GPOINTER_TO_UINT(data));
}

static gint
blank_height(GtkTextView *view, DocumentScroll *scroll)
{
    if (scroll && scroll->line_height)
        return scroll->line_height;
    GtkTextAttributes *style = gtk_text_view_get_default_attributes(view);
    PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(view), "");
    pango_layout_set_font_description(layout, style->font);
    gint height;
    pango_layout_get_pixel_size(layout, NULL, &height);
    g_object_unref(layout);
    gtk_text_attributes_unref(style);
    height = MAX(1, height);
    if (scroll)
        scroll->line_height = height;
    return height;
}

static gint64
blank_pitch(GtkTextView *view, DocumentScroll *scroll)
{
    return (gint64)blank_height(view, scroll) + gtk_text_view_get_pixels_above_lines(view) +
           gtk_text_view_get_pixels_below_lines(view);
}

void
cw_reveal_virtual_rect(GtkTextView *view, guint rows, GdkRectangle *rect)
{
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(gtk_text_view_get_buffer(view), &end);
    if (rows == 0) {
        gtk_text_view_get_iter_location(view, &end, rect);
        return;
    }

    DocumentScroll *scroll = g_object_get_data(G_OBJECT(view), SCROLL_STATE);
    gint y, height;
    gtk_text_view_get_line_yrange(view, &end, &y, &height);
    gint64 first = (gint64)y + height;
    if (height == 0) {
        gtk_text_view_get_iter_location(view, &end, rect);
        first = (gint64)rect->y + rect->height + gtk_text_view_get_pixels_below_lines(view);
    }
    rect->height = blank_height(view, scroll);
    rect->width = 1;
    rect->x = gtk_text_view_get_left_margin(view);
    gint64 last = G_MAXINT - rect->height;
    gint64 pitch = blank_pitch(view, scroll);
    gint64 offset = MIN((guint64)rows - 1, (guint64)(last / pitch)) * pitch;
    rect->y = (gint)MIN(last, first + gtk_text_view_get_pixels_above_lines(view) + offset);
}

static void
update_bottom_space(DocumentScroll *scroll, gint page)
{
    if (page <= 1)
        return;
    GtkTextView *view = scroll->view;
    /* Changing a GTK3 text margin invalidates every wrapped paragraph. Keep
     * the initial page of blank space through height changes. Grow it only
     * when an actual placement needs more. */
    if (!scroll->normal_bottom)
        scroll->normal_bottom = MAX(page, scroll->original_bottom);
    gint64 padding = scroll->normal_bottom;
    if (scroll->virtual_rows) {
        GtkTextIter end;
        gint y, height;
        gtk_text_buffer_get_end_iter(gtk_text_view_get_buffer(view), &end);
        gtk_text_view_get_line_yrange(view, &end, &y, &height);
        gint64 limit = MAX((gint64)page, (gint64)G_MAXINT - y - height -
                            gtk_text_view_get_top_margin(view) - page);
        gint64 pitch = blank_pitch(view, scroll);
        gint64 rows = MIN((guint64)scroll->virtual_rows, (guint64)(limit / pitch));
        gint64 required = rows * pitch + (scroll->placement == TOP_LINE ? page : page / 2) + pitch;
        gint64 pages = (required + page - 1) / page;
        gint64 previous = gtk_text_view_get_bottom_margin(view);
        if (required > previous)
            previous = MAX(2 * previous, pages * page);
        padding = MIN(limit, MAX(padding, previous));
    }
    if (gtk_text_view_get_bottom_margin(view) != padding)
        gtk_text_view_set_bottom_margin(view, (gint)padding);
}

static void
ensure_landing_space(DocumentScroll *scroll, gint64 destination, gint page)
{
    /* A real line needs at most one viewport of space below EOF. Most
     * positions already have enough manuscript below them, even when the
     * window has grown since opening. GTK stores these paragraph bounds. */
    if (scroll->virtual_rows || scroll->normal_bottom >= page)
        return;
    GtkTextView *view = scroll->view;
    GtkTextIter end;
    gint y, height;
    gtk_text_buffer_get_end_iter(gtk_text_view_get_buffer(view), &end);
    gtk_text_view_get_line_yrange(view, &end, &y, &height);
    gint64 needed = destination + page - ((gint64)y + height);
    if (needed <= scroll->normal_bottom)
        return;
    gint64 reserve = MAX(needed, (gint64)scroll->normal_bottom + page / 2);
    gint64 limit = (gint64)G_MAXINT - y - height - gtk_text_view_get_top_margin(view);
    if (limit > scroll->normal_bottom) {
        scroll->normal_bottom = (gint)MIN(reserve, limit);
        gtk_text_view_set_bottom_margin(view, scroll->normal_bottom);
    }
}

static void
cancel_alignment(GtkTextView *view)
{
    DocumentScroll *scroll = g_object_get_data(G_OBJECT(view), SCROLL_STATE);
    if (scroll)
        scroll->placement = KEEP_VISIBLE;
    g_object_set_data(G_OBJECT(view), REVEAL_IDLE, NULL);
}

static void
restore_top_space(DocumentScroll *scroll)
{
    gint extra = gtk_text_view_get_top_margin(scroll->view) - scroll->original_top;
    if (!extra)
        return;
    GtkAdjustment *doc = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(scroll->view));
    gdouble value = gtk_adjustment_get_value(doc) - extra;
    gtk_text_view_set_top_margin(scroll->view, scroll->original_top);
    gtk_adjustment_set_value(doc, value);
}

static void
viewport_scrolled(GtkAdjustment *doc, GtkTextView *view)
{
    (void)doc;
    DocumentScroll *scroll = g_object_get_data(G_OBJECT(view), SCROLL_STATE);
    if (!scroll || scroll->adjusting || scroll->placement != KEEP_VISIBLE ||
        gtk_text_view_get_top_margin(view) == scroll->original_top)
        return;
    /* Keep the first wheel step its normal size when leaving a centered
     * first line. Remove the temporary space once it is out of sight. */
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(view, &visible);
    if (visible.y >= -scroll->original_top)
        restore_top_space(scroll);
}

static void
adjust_to_cursor(GtkTextView *view, LandingPass pass)
{
    DocumentScroll *scroll = g_object_get_data(G_OBJECT(view), SCROLL_STATE);
    if (!scroll || scroll->adjusting || !gtk_widget_get_mapped(GTK_WIDGET(view)))
        return;

    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
    GtkTextIter cursor;
    if (scroll->virtual_rows)
        gtk_text_buffer_get_end_iter(buffer, &cursor);
    else if (scroll->placement != KEEP_VISIBLE)
        gtk_text_buffer_get_iter_at_offset(buffer, &cursor, scroll->anchor_offset);
    else
        gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
    if (pass != FINAL_LANDING) {
        gint height;
        gtk_text_view_get_line_yrange(view, &cursor, NULL, &height);
        if (height == 0)
            return;
    }

    scroll->adjusting = TRUE;
    GdkRectangle visible, caret;
    gtk_text_view_get_visible_rect(view, &visible);
    if (scroll->virtual_rows)
        cw_reveal_virtual_rect(view, scroll->virtual_rows, &caret);
    else
        gtk_text_view_get_iter_location(view, &cursor, &caret);

    gint64 destination = visible.y;
    if (scroll->placement == CENTER_LINE) {
        destination = (gint64)caret.y - (visible.height - caret.height) / 2;
        if (pass != DURING_LAYOUT && -destination > gtk_text_view_get_top_margin(view)) {
            /* Only an explicit center near the beginning needs room above
             * the manuscript. A fresh window keeps its normal top margin. */
            gtk_text_view_set_top_margin(view, (gint)MAX(-destination, visible.height));
            gtk_text_view_get_visible_rect(view, &visible);
        }
    } else if (scroll->placement == TOP_LINE) {
        destination = caret.y;
    } else {
        gint margin = pass != FINAL_LANDING ? visible.height / 2 : MIN(visible.height / 8, caret.height * 2);
        if (caret.y < visible.y)
            destination = (gint64)caret.y - margin;
        else if ((gint64)caret.y + caret.height > (gint64)visible.y + visible.height)
            destination = (gint64)caret.y + caret.height - visible.height + margin;
    }
    /* Adjustment notifications also run inside GTK's layout validator.
     * Changing margins there invalidates the layout it is still validating. */
    if (pass != DURING_LAYOUT && scroll->placement != KEEP_VISIBLE)
        ensure_landing_space(scroll, destination, visible.height);
    GtkAdjustment *adjustment = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));
    gdouble value = gtk_adjustment_get_value(adjustment) + destination - visible.y;
    gtk_adjustment_set_value(adjustment, value);
    scroll->adjusting = FALSE;
}

static void
layout_changed(GtkAdjustment *adjustment, GtkTextView *view)
{
    (void)adjustment;
    DocumentScroll *scroll = g_object_get_data(G_OBJECT(view), SCROLL_STATE);
    if (scroll && (scroll->placement != KEEP_VISIBLE || g_object_get_data(G_OBJECT(view), REVEAL_IDLE)))
        adjust_to_cursor(view, DURING_LAYOUT);
}

static gboolean
finish_scroll(gpointer data)
{
    g_object_steal_data(G_OBJECT(data), REVEAL_IDLE);
    adjust_to_cursor(GTK_TEXT_VIEW(data), FINAL_LANDING);
    return G_SOURCE_REMOVE;
}

static void
queue_landing(GtkTextView *view)
{
    if (!g_object_get_data(G_OBJECT(view), REVEAL_IDLE)) {
        guint id = g_idle_add_full(GTK_TEXT_VIEW_PRIORITY_VALIDATE + 1, finish_scroll, view, NULL);
        g_object_set_data_full(G_OBJECT(view), REVEAL_IDLE, GUINT_TO_POINTER(id), cancel_idle);
    }
}

static void
viewport_resized(GtkWidget *widget, GtkAllocation *allocation, DocumentScroll *scroll)
{
    (void)widget;
    (void)allocation;
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(scroll->view, &visible);
    /* Incremental wrapping queues size allocations as the document height
     * changes, including many with exactly the same viewport. */
    if (visible.width == scroll->viewport_width && visible.height == scroll->viewport_height)
        return;
    if (visible.width != scroll->viewport_width) {
        /* A negative Pango layout width disables wrapping. An extremely
         * narrow split can use the whole width for text; keep a glyph of
         * hysteresis so moving the divider by a pixel doesn't churn margins. */
        gint glyph = blank_height(scroll->view, scroll);
        gint64 threshold = (gint64)scroll->normal_left + scroll->normal_right +
                           (scroll->compact ? 2 : 1) * (gint64)glyph;
        gboolean compact = visible.width <= threshold;
        if (compact != scroll->compact) {
            scroll->compact = compact;
            gtk_text_view_set_left_margin(scroll->view, compact ? 0 : scroll->normal_left);
            gtk_text_view_set_right_margin(scroll->view, compact ? 0 : scroll->normal_right);
        }
    }
    scroll->viewport_width = visible.width;
    scroll->viewport_height = visible.height;
    update_bottom_space(scroll, visible.height);
    if (scroll->placement != KEEP_VISIBLE || scroll->virtual_rows) {
        adjust_to_cursor(scroll->view, REQUESTED_LANDING);
        queue_landing(scroll->view);
    }
}

static void
style_changed(GtkWidget *widget, DocumentScroll *scroll)
{
    (void)widget;
    scroll->line_height = 0;
}

static void
cursor_moved(GtkTextBuffer *buffer, GtkTextIter *iter, GtkTextMark *mark, GtkTextView *view)
{
    (void)iter;
    DocumentScroll *scroll = g_object_get_data(G_OBJECT(view), SCROLL_STATE);
    if (scroll && scroll->placement != KEEP_VISIBLE && mark == gtk_text_buffer_get_insert(buffer))
        cancel_alignment(view);
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
    cancel_alignment(GTK_TEXT_VIEW(data));
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
    /* This GTK action has no handled accumulator. Its default handler would
     * animate a second page or step after our immediate scrollbar move. */
    g_signal_stop_emission_by_name(scroll, "scroll-child");
    g_signal_emit_by_name(bar, "move-slider", step);
    return TRUE;
}

static gboolean
browse_manually(GtkWidget *widget, GdkEvent *event, gpointer data)
{
    (void)widget;
    GtkTextView *view = GTK_TEXT_VIEW(data);
    cancel_alignment(view);
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

static void
free_touch_scroll(gpointer data)
{
    DocumentScroll *touch = data;
    g_object_unref(touch->pan);
    g_object_unref(touch->hold);
    g_free(touch);
}

static void
pan_begin(GtkGestureDrag *gesture, gdouble x, gdouble y, DocumentScroll *touch)
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
pan_motion(GtkGesturePan *gesture, GtkPanDirection direction, gdouble offset, DocumentScroll *touch)
{
    (void)gesture;
    if (!touch->moved && !gtk_drag_check_threshold(GTK_WIDGET(touch->view), 0, 0, 0, (gint)offset))
        return;
    touch->moved = TRUE;
    cancel_alignment(touch->view);
    GtkAdjustment *doc = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(touch->view));
    gtk_adjustment_set_value(doc, touch->start + (direction == GTK_PAN_DIRECTION_UP ? offset : -offset));
}

static void
pan_end(GtkGesture *gesture, GdkEventSequence *sequence, DocumentScroll *touch)
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
    if (g_object_get_data(G_OBJECT(view), SCROLL_STATE))
        return;
    GtkWidget *parent = gtk_widget_get_ancestor(GTK_WIDGET(view), GTK_TYPE_SCROLLED_WINDOW);
    if (!parent)
        return;
    GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW(parent);
    /* The tile determines the wrapping width. GTK_POLICY_NEVER publishes the
     * largest measured paragraph as the pane's minimum and makes a shrink
     * wait for successive, sometimes one-pixel, whole-manuscript reflows. */
    gtk_scrolled_window_set_policy(scroll, GTK_POLICY_EXTERNAL, GTK_POLICY_AUTOMATIC);
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
    g_signal_connect_object(doc, "value-changed", G_CALLBACK(viewport_scrolled), view, G_CONNECT_AFTER);
    g_signal_connect_object(scroll, "scroll-child", G_CALLBACK(scroll_child), view, 0);
    g_signal_connect_object(scroll, "scroll-event", G_CALLBACK(browse_manually), view, 0);
    g_signal_connect_object(bar, "button-press-event", G_CALLBACK(browse_manually), view, 0);

    DocumentScroll *touch = g_new0(DocumentScroll, 1);
    touch->view = view;
    touch->original_top = gtk_text_view_get_top_margin(view);
    touch->original_bottom = gtk_text_view_get_bottom_margin(view);
    touch->normal_left = gtk_text_view_get_left_margin(view);
    touch->normal_right = gtk_text_view_get_right_margin(view);
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
    g_object_set_data_full(G_OBJECT(view), SCROLL_STATE, touch, free_touch_scroll);
    g_signal_connect_after(view, "size-allocate", G_CALLBACK(viewport_resized), touch);
    g_signal_connect(view, "style-updated", G_CALLBACK(style_changed), touch);
    g_signal_connect_object(gtk_text_view_get_buffer(view), "mark-set", G_CALLBACK(cursor_moved), view, 0);
    g_signal_connect_object(gtk_text_view_get_buffer(view), "changed", G_CALLBACK(cancel_alignment), view, G_CONNECT_SWAPPED);
    g_signal_connect_object(view, "button-press-event", G_CALLBACK(browse_manually), view, 0);
}

static void
place_view(GtkTextView *view, Placement placement, const GtkTextIter *position, guint virtual_rows)
{
    cw_reveal_configure(view);
    DocumentScroll *scroll = g_object_get_data(G_OBJECT(view), SCROLL_STATE);
    if (!scroll)
        return;
    scroll->placement = placement;
    if (placement == TOP_LINE)
        restore_top_space(scroll);
    scroll->anchor_offset = gtk_text_iter_get_offset(position);
    scroll->virtual_rows = virtual_rows;
    GdkRectangle visible;
    gtk_text_view_get_visible_rect(view, &visible);
    update_bottom_space(scroll, visible.height);
    queue_landing(view);

    /* GTK measures the neighborhood of a mark even when most of the newly
     * opened manuscript is still unwrapped. Only request this when necessary. */
    gint height;
    gtk_text_view_get_line_yrange(view, position, NULL, &height);
    if (height == 0) {
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
        GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, NULL, position, TRUE);
        gtk_text_view_scroll_mark_onscreen(view, mark);
        gtk_text_buffer_delete_mark(buffer, mark);
    }
    adjust_to_cursor(view, REQUESTED_LANDING);
}

void
cw_reveal_center_line(GtkTextView *view, const GtkTextIter *base, guint virtual_rows)
{
    place_view(view, CENTER_LINE, base, virtual_rows);
}

void
cw_reveal_top_line(GtkTextView *view, const GtkTextIter *header, guint virtual_rows)
{
    place_view(view, TOP_LINE, header, virtual_rows);
}

void
cw_reveal_virtual_cursor(GtkTextView *view, guint virtual_rows)
{
    cw_reveal_configure(view);
    DocumentScroll *scroll = g_object_get_data(G_OBJECT(view), SCROLL_STATE);
    if (!scroll || (!virtual_rows && !scroll->virtual_rows))
        return;
    if (virtual_rows) {
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(gtk_text_view_get_buffer(view), &end);
        place_view(view, KEEP_VISIBLE, &end, virtual_rows);
    } else {
        cancel_alignment(view);
        scroll->virtual_rows = 0;
        GdkRectangle visible;
        gtk_text_view_get_visible_rect(view, &visible);
        update_bottom_space(scroll, visible.height);
    }
}

void
cw_reveal_cursor(GtkTextView *view)
{
    cw_reveal_configure(view);
    cancel_alignment(view);
    cw_reveal_virtual_cursor(view, 0);
    DocumentScroll *scroll = g_object_get_data(G_OBJECT(view), SCROLL_STATE);
    if (scroll)
        restore_top_space(scroll);
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(view);
    gtk_text_view_scroll_mark_onscreen(view, gtk_text_buffer_get_insert(buffer));
    queue_landing(view);
}

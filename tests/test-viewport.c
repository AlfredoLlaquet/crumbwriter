#include "reveal.h"
#include <gtksourceview/gtksource.h>

typedef struct {
    GtkWidget *window;
    GtkTextView *view;
    GtkTextBuffer *buffer;
} Fixture;

static void
settle(void)
{
    while (g_main_context_iteration(NULL, FALSE))
        ;
}

static void
setup(Fixture *f, gconstpointer user_data)
{
    (void)user_data;
    f->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    f->view = GTK_TEXT_VIEW(gtk_source_view_new());
    f->buffer = gtk_text_view_get_buffer(f->view);
    gtk_text_view_set_wrap_mode(f->view, GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_top_margin(f->view, 18);
    gtk_text_view_set_bottom_margin(f->view, 18);
    gtk_text_view_set_left_margin(f->view, 24);
    gtk_text_view_set_right_margin(f->view, 24);
    gtk_text_view_set_pixels_below_lines(f->view, 6);
    GtkWidget *pane = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pane), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(pane), GTK_WIDGET(f->view));
    GtkWidget *split = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *sidebar = gtk_label_new("Visit history");
    gtk_widget_set_size_request(sidebar, 200, -1);
    gtk_paned_pack1(GTK_PANED(split), pane, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(split), sidebar, FALSE, TRUE);
    gtk_container_add(GTK_CONTAINER(f->window), split);
    cw_reveal_configure(f->view);
    gtk_window_set_default_size(GTK_WINDOW(f->window), 620, 400);
    gtk_widget_show_all(f->window);
    settle();
}

static void
teardown(Fixture *f, gconstpointer user_data)
{
    (void)user_data;
    gtk_widget_destroy(f->window);
    settle();
}

static void
load(Fixture *f, const gchar *text)
{
    gtk_source_buffer_begin_not_undoable_action(GTK_SOURCE_BUFFER(f->buffer));
    gtk_text_buffer_set_text(f->buffer, text, -1);
    gtk_source_buffer_end_not_undoable_action(GTK_SOURCE_BUFFER(f->buffer));
    gtk_text_buffer_set_modified(f->buffer, FALSE);
}

static void
assert_position(Fixture *f, const GtkTextIter *target, guint virtual_rows, gboolean centered)
{
    settle();
    GdkRectangle row, viewport;
    if (virtual_rows)
        cw_reveal_virtual_rect(f->view, virtual_rows, &row);
    else
        gtk_text_view_get_iter_location(f->view, target, &row);
    gtk_text_view_get_visible_rect(f->view, &viewport);
    gint expected = centered ? (viewport.height - row.height) / 2 : 0;
    g_assert_cmpint(ABS((row.y - viewport.y) - expected), <=, 1);
    g_assert_false(gtk_text_buffer_get_modified(f->buffer));
    g_assert_false(gtk_source_buffer_can_undo(GTK_SOURCE_BUFFER(f->buffer)));
}

static void
request_size(Fixture *f, gint width, gint height)
{
    gtk_window_resize(GTK_WINDOW(f->window), width, height);
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while ((gtk_widget_get_allocated_width(f->window) != width ||
            gtk_widget_get_allocated_height(f->window) != height) &&
           g_get_monotonic_time() < deadline) {
        if (!g_main_context_iteration(NULL, FALSE))
            g_usleep(1000);
    }
    g_assert_cmpint(gtk_widget_get_allocated_width(f->window), ==, width);
    g_assert_cmpint(gtk_widget_get_allocated_height(f->window), ==, height);
}

static void
resize(Fixture *f, gint width, gint height)
{
    request_size(f, width, height);
    settle();
}

static void
short_document(Fixture *f, gconstpointer data)
{
    load(f, data);
    settle();
    g_assert_cmpint(gtk_text_view_get_top_margin(f->view), ==, 18);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(f->buffer, &start, &end);
    gtk_text_buffer_select_range(f->buffer, &end, &start);
    cw_reveal_center_line(f->view, &end, 0);
    assert_position(f, &end, 0, TRUE);
    resize(f, 660, 510);
    assert_position(f, &end, 0, TRUE);
    cw_reveal_top_line(f->view, &start, 0);
    assert_position(f, &start, 0, FALSE);
    GtkTextIter actual;
    gtk_text_buffer_get_iter_at_mark(f->buffer, &actual, gtk_text_buffer_get_insert(f->buffer));
    g_assert_true(gtk_text_iter_equal(&actual, &end));
    gtk_text_buffer_get_iter_at_mark(f->buffer, &actual, gtk_text_buffer_get_selection_bound(f->buffer));
    g_assert_true(gtk_text_iter_equal(&actual, &start));
}

static void
virtual_rows(Fixture *f, gconstpointer data)
{
    load(f, data);
    settle();
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(f->buffer, &end);
    GdkRectangle first, second;
    cw_reveal_virtual_rect(f->view, 1, &first);
    cw_reveal_virtual_rect(f->view, 2, &second);
    g_assert_cmpint(first.x, ==, gtk_text_view_get_left_margin(f->view));
    g_assert_cmpint(second.y - first.y, ==, first.height + gtk_text_view_get_pixels_below_lines(f->view));
    cw_reveal_virtual_cursor(f->view, 137);
    cw_reveal_top_line(f->view, &end, 137);
    assert_position(f, &end, 137, FALSE);
    cw_reveal_center_line(f->view, &end, 137);
    assert_position(f, &end, 137, TRUE);
    resize(f, 580, 480);
    assert_position(f, &end, 137, TRUE);
    cw_reveal_virtual_cursor(f->view, 0);
    settle();
    g_assert_cmpint(gtk_text_view_get_bottom_margin(f->view), <=, 480);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(f->buffer, &start);
    g_autofree gchar *unchanged = gtk_text_buffer_get_text(f->buffer, &start, &end, TRUE);
    g_assert_cmpstr(unchanged, ==, data);

    /* A real blank line typed after EOF must occupy the same coordinates as
     * the virtual row that preceded it. */
    gtk_text_buffer_insert(f->buffer, &end, "\n", 1);
    settle();
    GdkRectangle materialized;
    gtk_text_view_get_iter_location(f->view, &end, &materialized);
    g_assert_cmpint(materialized.y, ==, first.y);
    g_assert_cmpint(materialized.height, ==, first.height);
}

static void
load_wrapped(Fixture *f)
{
    GString *text = g_string_new(NULL);
    for (gint line = 1; line <= 850; line++) {
        g_string_append_printf(text, "Crumb %d: A title: ", line);
        for (gint i = 0; i < 14; i++)
            g_string_append(text, "The gate faces the river. ");
        if (line < 850)
            g_string_append_c(text, '\n');
    }
    load(f, text->str);
    g_string_free(text, TRUE);
}

static void
wrapped_document(Fixture *f, gconstpointer data)
{
    (void)data;
    load_wrapped(f);
    GtkTextIter header, end;
    gtk_text_buffer_get_iter_at_line(f->buffer, &header, 849);
    gtk_text_buffer_get_end_iter(f->buffer, &end);
    gtk_text_buffer_place_cursor(f->buffer, &end);
    cw_reveal_center_line(f->view, &end, 0);
    assert_position(f, &end, 0, TRUE);
    cw_reveal_top_line(f->view, &header, 0);
    assert_position(f, &header, 0, FALSE);
    resize(f, 510, 450);
    assert_position(f, &header, 0, FALSE);

    /* Native scrollbar or keyboard scrolling cancels the alignment, even
     * when an unrelated size allocation follows the gesture. */
    GtkWidget *pane = gtk_widget_get_ancestor(GTK_WIDGET(f->view), GTK_TYPE_SCROLLED_WINDOW);
    gboolean handled;
    g_signal_emit_by_name(pane, "scroll-child", GTK_SCROLL_PAGE_BACKWARD, FALSE, &handled);
    g_assert_true(handled);
    resize(f, 510, 490);
    GdkRectangle visible, target;
    gtk_text_view_get_visible_rect(f->view, &visible);
    gtk_text_view_get_iter_location(f->view, &header, &target);
    g_assert_cmpint(target.y - visible.y, >, target.height);

    GtkTextIter first;
    gtk_text_buffer_get_start_iter(f->buffer, &first);
    cw_reveal_center_line(f->view, &first, 0);
    assert_position(f, &first, 0, TRUE);
    gtk_text_view_get_visible_rect(f->view, &visible);
    GtkAdjustment *doc = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(f->view));
    gdouble next = visible.y + gtk_adjustment_get_step_increment(doc);
    g_signal_emit_by_name(pane, "scroll-child", GTK_SCROLL_STEP_FORWARD, FALSE, &handled);
    settle();
    gtk_text_view_get_visible_rect(f->view, &visible);
    g_assert_cmpfloat_with_epsilon(visible.y, next, 1.0);
}

/* Each size reaches the native window before the next request. Leave GTK's
 * lower-priority paragraph validation free to overlap successive drag steps. */
static void
drag_height(Fixture *f, gint step)
{
    gint width = gtk_widget_get_allocated_width(f->window);
    gint height = gtk_widget_get_allocated_height(f->window);
    for (gint i = 1; i <= 8; i++)
        request_size(f, width, height + i * step);
    settle();
}

static void
count_margin(GObject *view, GParamSpec *property, guint *count)
{
    (void)view;
    (void)property;
    ++*count;
}

static void
large_resize(Fixture *f, gconstpointer data)
{
    (void)data;
    load_wrapped(f);
    GtkTextIter middle, last;
    gtk_text_buffer_get_iter_at_line_offset(f->buffer, &middle, 410, 210);
    gtk_text_buffer_get_iter_at_line(f->buffer, &last, 849);
    gtk_text_buffer_place_cursor(f->buffer, &middle);
    cw_reveal_cursor(f->view);
    settle();

    /* In GTK 3 either vertical text margin change invalidates wrapping for
     * every paragraph, even at a constant width. Count this work directly. */
    guint changes = 0;
    g_signal_connect(f->view, "notify::top-margin", G_CALLBACK(count_margin), &changes);
    g_signal_connect(f->view, "notify::bottom-margin", G_CALLBACK(count_margin), &changes);
    drag_height(f, 3);
    guint ordinary = changes;

    cw_reveal_top_line(f->view, &last, 0);
    assert_position(f, &last, 0, FALSE);
    changes = 0;
    drag_height(f, 3);
    assert_position(f, &last, 0, FALSE);
    guint top = changes;

    cw_reveal_center_line(f->view, &middle, 0);
    assert_position(f, &middle, 0, TRUE);
    GtkWidget *pane = gtk_widget_get_ancestor(GTK_WIDGET(f->view), GTK_TYPE_SCROLLED_WINDOW);
    gboolean handled;
    g_signal_emit_by_name(pane, "scroll-child", GTK_SCROLL_PAGE_BACKWARD, FALSE, &handled);
    g_assert_true(handled);
    settle();
    GdkRectangle before, after, caret, anchor_before, anchor_after;
    GtkTextIter anchor;
    gtk_text_view_get_visible_rect(f->view, &before);
    gtk_text_view_get_iter_at_location(f->view, &anchor, gtk_text_view_get_left_margin(f->view),
                                       before.y + 1);
    gtk_text_view_get_iter_location(f->view, &anchor, &anchor_before);
    changes = 0;
    drag_height(f, -3);
    gtk_text_view_get_visible_rect(f->view, &after);
    gtk_text_view_get_iter_location(f->view, &anchor, &anchor_after);
    gtk_text_view_get_iter_location(f->view, &middle, &caret);
    /* Compare the same display line on screen, even if GTK measured more
     * paragraphs above it while pixels were repainted. */
    g_assert_cmpint(ABS((anchor_after.y - after.y) - (anchor_before.y - before.y)), <=, 1);
    g_assert_cmpint(caret.y, >=, after.y + after.height);
    guint manual = changes;

    GtkTextIter first;
    gtk_text_buffer_get_start_iter(f->buffer, &first);
    cw_reveal_center_line(f->view, &first, 0);
    assert_position(f, &first, 0, TRUE);
    changes = 0;
    drag_height(f, 3);
    assert_position(f, &first, 0, TRUE);
    g_signal_handlers_disconnect_by_data(f->view, &changes);

    g_test_message("Eight height allocations changed vertical margins %u times while editing, "
                   "%u at the last header, %u after scrolling and %u at the centered first line",
                   ordinary, top, manual, changes);
    g_assert_cmpuint(ordinary, <=, 1);
    g_assert_cmpuint(top, <=, 1);
    g_assert_cmpuint(manual, <=, 1);
    g_assert_cmpuint(changes, <=, 1);
}

static void
tile_width(Fixture *f, gconstpointer data)
{
    (void)data;
    load_wrapped(f);
    GtkTextIter middle;
    gtk_text_buffer_get_iter_at_line_offset(f->buffer, &middle, 410, 210);
    gtk_text_buffer_place_cursor(f->buffer, &middle);
    cw_reveal_center_line(f->view, &middle, 0);
    assert_position(f, &middle, 0, TRUE);

    GtkWidget *pane = gtk_widget_get_ancestor(GTK_WIDGET(f->view), GTK_TYPE_SCROLLED_WINDOW);
    GtkPaned *split = GTK_PANED(gtk_widget_get_parent(pane));
    gint pane_width = gtk_widget_get_allocated_width(pane), minimum, wide_height;
    gtk_widget_get_preferred_width(pane, &minimum, NULL);
    gtk_text_view_get_line_yrange(f->view, &middle, NULL, &wide_height);

    /* A tiler must be able to take 120 pixels from the writing pane in one
     * allocation. Publishing the current wrapped paragraph width as a minimum
     * makes GTK approach the target a pixel or two at a time. */
    request_size(f, gtk_widget_get_allocated_width(f->window) - 120,
                 gtk_widget_get_allocated_height(f->window));
    g_test_message("Writing pane %d to %d, split at %d, requested minimum %d", pane_width,
                   gtk_widget_get_allocated_width(pane), gtk_paned_get_position(split), minimum);
    g_assert_cmpint(gtk_widget_get_allocated_width(pane), ==, gtk_paned_get_position(split));
    g_assert_cmpint(gtk_widget_get_allocated_width(pane), <=, pane_width - 119);
    g_assert_cmpint(minimum, <=, pane_width - 120);
    assert_position(f, &middle, 0, TRUE);
    gint narrow_height;
    gtk_text_view_get_line_yrange(f->view, &middle, NULL, &narrow_height);
    g_assert_cmpint(narrow_height, >, wide_height);

    /* A divider can request less space than the usual two 24px margins.
     * Even one uninterrupted word must keep wrapping in that tile. */
    g_autofree gchar *word = g_strnfill(1024, 'w');
    g_autofree gchar *crumb = g_strconcat("Crumb 1: : ", word, NULL);
    load(f, crumb);
    gtk_paned_set_position(split, 32);
    resize(f, gtk_widget_get_allocated_width(f->window),
           gtk_widget_get_allocated_height(f->window) + 1);
    GtkTextIter end;
    GdkRectangle mid_word, word_end;
    gtk_text_buffer_get_iter_at_offset(f->buffer, &middle, 512);
    gtk_text_buffer_get_end_iter(f->buffer, &end);
    gtk_text_view_get_iter_location(f->view, &middle, &mid_word);
    gtk_text_view_get_iter_location(f->view, &end, &word_end);
    g_assert_cmpint(gtk_widget_get_allocated_width(pane), ==, 32);
    g_assert_cmpint(word_end.y, >, mid_word.y);
    GtkWidget *horizontal = gtk_scrolled_window_get_hscrollbar(GTK_SCROLLED_WINDOW(pane));
    g_assert_false(gtk_widget_get_child_visible(horizontal));
    gtk_paned_set_position(split, 260);
    resize(f, gtk_widget_get_allocated_width(f->window),
           gtk_widget_get_allocated_height(f->window) + 1);
    g_assert_cmpint(gtk_text_view_get_left_margin(f->view), ==, 24);
    g_assert_cmpint(gtk_text_view_get_right_margin(f->view), ==, 24);
}

int
main(int argc, char **argv)
{
    gtk_test_init(&argc, &argv, NULL);
    g_test_add("/viewport/empty", Fixture, "", setup, short_document, teardown);
    g_test_add("/viewport/one-line", Fixture, "Crumb 1: A: Here.", setup, short_document, teardown);
    g_test_add("/viewport/virtual-unterminated", Fixture, "Last", setup, virtual_rows, teardown);
    g_test_add("/viewport/virtual-final-lf", Fixture, "Last\n", setup, virtual_rows, teardown);
    g_test_add("/viewport/wrapped", Fixture, NULL, setup, wrapped_document, teardown);
    g_test_add("/viewport/large-resize", Fixture, NULL, setup, large_resize, teardown);
    g_test_add("/viewport/tile-width", Fixture, NULL, setup, tile_width, teardown);
    return g_test_run();
}

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
    gtk_text_view_set_pixels_below_lines(f->view, 6);
    GtkWidget *pane = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(pane), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(pane), GTK_WIDGET(f->view));
    gtk_container_add(GTK_CONTAINER(f->window), pane);
    cw_reveal_configure(f->view);
    gtk_window_set_default_size(GTK_WINDOW(f->window), 420, 400);
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
resize(Fixture *f, gint width, gint height)
{
    gtk_window_resize(GTK_WINDOW(f->window), width, height);
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while (gtk_widget_get_allocated_height(GTK_WIDGET(f->view)) != height &&
           g_get_monotonic_time() < deadline) {
        settle();
        g_usleep(1000);
    }
    g_assert_cmpint(gtk_widget_get_allocated_height(GTK_WIDGET(f->view)), ==, height);
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
    resize(f, 460, 510);
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
    resize(f, 380, 480);
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
wrapped_document(Fixture *f, gconstpointer data)
{
    (void)data;
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
    GtkTextIter header, end;
    gtk_text_buffer_get_iter_at_line(f->buffer, &header, 849);
    gtk_text_buffer_get_end_iter(f->buffer, &end);
    gtk_text_buffer_place_cursor(f->buffer, &end);
    cw_reveal_center_line(f->view, &end, 0);
    assert_position(f, &end, 0, TRUE);
    cw_reveal_top_line(f->view, &header, 0);
    assert_position(f, &header, 0, FALSE);
    resize(f, 310, 450);
    assert_position(f, &header, 0, FALSE);

    /* Native scrollbar or keyboard scrolling cancels the alignment, even
     * when an unrelated size allocation follows the gesture. */
    GtkWidget *pane = gtk_widget_get_ancestor(GTK_WIDGET(f->view), GTK_TYPE_SCROLLED_WINDOW);
    gboolean handled;
    g_signal_emit_by_name(pane, "scroll-child", GTK_SCROLL_PAGE_BACKWARD, FALSE, &handled);
    g_assert_true(handled);
    resize(f, 310, 490);
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

int
main(int argc, char **argv)
{
    gtk_test_init(&argc, &argv, NULL);
    g_test_add("/viewport/empty", Fixture, "", setup, short_document, teardown);
    g_test_add("/viewport/one-line", Fixture, "Crumb 1: A: Here.", setup, short_document, teardown);
    g_test_add("/viewport/virtual-unterminated", Fixture, "Last", setup, virtual_rows, teardown);
    g_test_add("/viewport/virtual-final-lf", Fixture, "Last\n", setup, virtual_rows, teardown);
    g_test_add("/viewport/wrapped", Fixture, NULL, setup, wrapped_document, teardown);
    return g_test_run();
}

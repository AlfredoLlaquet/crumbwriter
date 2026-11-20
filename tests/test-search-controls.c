#include "search.h"
#include "editor-spacing.h"
#include <string.h>

typedef struct {
    GtkWidget *window, *view, *panel;
    GtkTextBuffer *buffer;
    CwSearch *search;
    GKeyFile *preferences;
} Fixture;

static void
settle(void)
{
    while (g_main_context_iteration(NULL, FALSE))
        ;
}

static void
setup(Fixture *f, gconstpointer unused)
{
    (void)unused;
    f->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(f->window), 420, 340);
    GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(f->window), column);
    f->view = gtk_source_view_new();
    f->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(f->view));
    f->preferences = g_key_file_new();
    f->search = cw_search_new(GTK_SOURCE_VIEW(f->view), f->preferences, NULL, NULL);
    f->panel = cw_search_widget(f->search);
    gtk_box_pack_start(GTK_BOX(column), f->panel, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), f->view);
    gtk_box_pack_start(GTK_BOX(column), scroll, TRUE, TRUE, 0);
    gtk_widget_show_all(f->window);
    settle();
}

static void
teardown(Fixture *f, gconstpointer unused)
{
    (void)unused;
    cw_search_free(f->search);
    gtk_widget_destroy(f->window);
    g_key_file_unref(f->preferences);
    settle();
}

static GtkWidget *
find_named(GtkWidget *widget, const gchar *name)
{
    if (g_str_equal(name, gtk_widget_get_name(widget)))
        return widget;
    if (!GTK_IS_CONTAINER(widget))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = NULL;
    for (GList *it = children; it && !found; it = it->next)
        found = find_named(GTK_WIDGET(it->data), name);
    g_list_free(children);
    return found;
}

static GtkWidget *
control(Fixture *f, const gchar *name)
{
    GtkWidget *widget = find_named(f->panel, name);
    g_assert_nonnull(widget);
    return widget;
}

static gboolean
key(Fixture *f, guint keyval, GdkModifierType mods)
{
    GdkEventKey event = {.type = GDK_KEY_PRESS, .keyval = keyval, .state = mods};
    return cw_search_handle_key(f->search, &event);
}

static void
query(Fixture *f, const gchar *value)
{
    gtk_entry_set_text(GTK_ENTRY(control(f, "cw-find")), value);
}

static void
assert_match(Fixture *f, const gchar *text, gint line)
{
    GtkTextIter start, end;
    gboolean has_selection = gtk_text_buffer_get_selection_bounds(f->buffer, &start, &end);
    if (!has_selection)
        g_test_message("Expected %s on line %d", text, line);
    g_assert_true(has_selection);
    g_autofree gchar *selected = gtk_text_buffer_get_text(f->buffer, &start, &end, TRUE);
    g_assert_cmpstr(selected, ==, text);
    g_assert_cmpint(gtk_text_iter_get_line(&start), ==, line);
}

static gboolean
active(Fixture *f, const gchar *id)
{
    return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(control(f, id)));
}

static void
place(Fixture *f, gint line, gint column)
{
    GtkTextIter caret;
    gtk_text_buffer_get_iter_at_line_offset(f->buffer, &caret, line, column);
    gtk_text_buffer_place_cursor(f->buffer, &caret);
}

static void
assert_caret(Fixture *f, gint line, gint column)
{
    GtkTextIter caret;
    gtk_text_buffer_get_iter_at_mark(f->buffer, &caret, gtk_text_buffer_get_insert(f->buffer));
    g_assert_cmpint(gtk_text_iter_get_line(&caret), ==, line);
    g_assert_cmpint(gtk_text_iter_get_line_offset(&caret), ==, column);
}

static void
assert_caret_centered(Fixture *f)
{
    /* The search panel changes the paper height when it opens or closes. */
    gtk_test_widget_wait_for_draw(f->view);
    settle();
    GtkTextView *view = GTK_TEXT_VIEW(f->view);
    GtkTextIter caret;
    gtk_text_buffer_get_iter_at_mark(f->buffer, &caret, gtk_text_buffer_get_insert(f->buffer));
    GdkRectangle row, viewport;
    gtk_text_view_get_iter_location(view, &caret, &row);
    gtk_text_view_get_visible_rect(view, &viewport);
    gint expected = (viewport.height - row.height) / 2;
    if (ABS(row.y - viewport.y - expected) > 1)
        g_test_message("Line %d at %d pixels, expected %d", gtk_text_iter_get_line(&caret) + 1,
                       row.y - viewport.y, expected);
    g_assert_cmpint(ABS(row.y - viewport.y - expected), <=, 1);
}

static void
live_find_and_keys(Fixture *f, gconstpointer unused)
{
    (void)unused;
    gtk_text_buffer_set_text(f->buffer, "Crumb 1: : Story.\nCrumb 2: : stone.\nCrumb 3: : Stone.", -1);
    place(f, 0, 0);
    g_assert_false(key(f, GDK_KEY_n, GDK_CONTROL_MASK));
    g_assert_false(key(f, GDK_KEY_w, GDK_CONTROL_MASK));
    cw_search_show(f->search, FALSE);
    query(f, "sto");
    assert_match(f, "Sto", 0);
    query(f, "stone");
    assert_match(f, "stone", 1);
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == control(f, "cw-find"));
    g_assert_false(g_key_file_has_key(f->preferences, "search", "recent", NULL));
    gtk_widget_grab_focus(f->view);
    g_assert_true(key(f, GDK_KEY_F3, 0));
    assert_match(f, "Stone", 2);
    g_assert_true(key(f, GDK_KEY_F3, GDK_SHIFT_MASK));
    assert_match(f, "stone", 1);
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == f->view);
    g_assert_true(key(f, GDK_KEY_m, GDK_CONTROL_MASK));
    g_assert_true(active(f, "cw-search-case"));
    g_assert_true(key(f, GDK_KEY_w, GDK_CONTROL_MASK));
    g_assert_true(active(f, "cw-search-word"));
    g_assert_true(key(f, GDK_KEY_w, GDK_MOD1_MASK));
    g_assert_false(active(f, "cw-search-wrap"));
    g_assert_true(key(f, GDK_KEY_n, GDK_CONTROL_MASK));
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == control(f, "cw-crumb-number"));
    g_assert_true(key(f, GDK_KEY_p, GDK_CONTROL_MASK));
    assert_match(f, "stone", 1);
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == control(f, "cw-find"));
    g_assert_false(key(f, GDK_KEY_y, GDK_CONTROL_MASK));
    gtk_widget_grab_focus(f->view);
    GtkTextIter before, after;
    gtk_text_buffer_get_iter_at_mark(f->buffer, &before, gtk_text_buffer_get_insert(f->buffer));
    g_assert_true(key(f, GDK_KEY_Escape, 0));
    gtk_text_buffer_get_iter_at_mark(f->buffer, &after, gtk_text_buffer_get_insert(f->buffer));
    g_assert_true(gtk_text_iter_equal(&before, &after));
    g_assert_false(gtk_text_buffer_get_has_selection(f->buffer));
    g_assert_false(gtk_widget_get_visible(f->panel));
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == f->view);
    g_assert_false(key(f, GDK_KEY_Escape, 0));
}

static void
regex_modes(Fixture *f, gconstpointer unused)
{
    (void)unused;
    gtk_text_buffer_set_text(f->buffer, "a1b2b\ncatfish underdog cat dog", -1);
    cw_search_show(f->search, TRUE);
    g_assert_true(key(f, GDK_KEY_r, GDK_CONTROL_MASK));
    query(f, "a.+b");
    cw_search_step(f->search, FALSE);
    assert_match(f, "a1b", 0);
    query(f, "(?-U)a.+b");
    cw_search_step(f->search, FALSE);
    assert_match(f, "a1b2b", 0);
    g_assert_true(key(f, GDK_KEY_w, GDK_CONTROL_MASK));
    query(f, "(*UTF)cat|dog");
    cw_search_step(f->search, FALSE);
    assert_match(f, "cat", 1);
    query(f, "[");
    cw_search_step(f->search, FALSE);
    g_assert_true(*gtk_label_get_text(GTK_LABEL(control(f, "cw-search-status"))) != '\0');
    query(f, "(?-U:cat|dog)");
    cw_search_step(f->search, FALSE);
    assert_match(f, "cat", 1);
    key(f, GDK_KEY_w, GDK_CONTROL_MASK);
    query(f, "(a)(.+)(b)");
    gtk_entry_set_text(GTK_ENTRY(control(f, "cw-replacement")), "\\3-\\2-\\1");
    cw_search_step(f->search, FALSE);
    assert_match(f, "a1b", 0);
    gtk_button_clicked(GTK_BUTTON(control(f, "cw-replace-one")));
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(f->buffer, &start);
    end = start;
    gtk_text_iter_forward_to_line_end(&end);
    g_autofree gchar *line = gtk_text_buffer_get_text(f->buffer, &start, &end, TRUE);
    g_assert_cmpstr(line, ==, "b-1-a2b");
    cw_search_hide(f->search);
    g_autofree gchar *history = g_key_file_get_string(f->preferences, "search", "recent", NULL);
    g_assert_nonnull(strstr(history, "(?-U:cat|dog)"));
    g_assert_null(strstr(history, "(?U)"));
}

static void
popovers_and_tile(Fixture *f, gconstpointer unused)
{
    (void)unused;
    gtk_text_buffer_set_text(f->buffer, "alpha beta", -1);
    cw_search_show(f->search, TRUE);
    GtkRequisition minimum;
    gtk_widget_get_preferred_size(f->panel, &minimum, NULL);
    g_test_message("Find/replace panel minimum %d x %d", minimum.width, minimum.height);
    g_assert_cmpint(minimum.width, <=, 400);
    GtkWidget *mode = control(f, "cw-search-mode");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mode), TRUE);
    GtkWidget *popup = GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(mode)));
    const gchar *choices[] = {"cw-mode-plain", "cw-mode-regex", "cw-mode-number"};
    GtkWidget *last = find_named(popup, choices[G_N_ELEMENTS(choices) - 1]);
    g_assert_nonnull(last);
    /* Mapping precedes the first frame-clock layout on some displays. GTK
     * reports a 1-pixel placeholder until the popover has allocated its rows. */
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while (gtk_widget_get_allocated_height(last) <= 1 && g_get_monotonic_time() < deadline) {
        settle();
        g_usleep(1000);
    }
    g_assert_cmpint(gtk_widget_get_allocated_height(last), >, 1);
    gint previous_bottom = -1;
    for (guint i = 0; i < G_N_ELEMENTS(choices); i++) {
        GtkWidget *item = find_named(popup, choices[i]);
        g_assert_nonnull(item);
        g_assert_true(gtk_widget_get_mapped(item));
        gint x, y;
        g_assert_true(gtk_widget_translate_coordinates(item, popup, 0, 0, &x, &y));
        g_assert_cmpint(y, >=, previous_bottom);
        previous_bottom = y + gtk_widget_get_allocated_height(item);
    }
    g_assert_cmpint(previous_bottom, <=, gtk_widget_get_allocated_height(popup));
    gtk_button_clicked(GTK_BUTTON(find_named(popup, "cw-mode-plain")));
    query(f, "alpha"); cw_search_step(f->search, FALSE);
    query(f, "beta"); cw_search_step(f->search, FALSE);
    gtk_widget_grab_focus(f->view);
    g_assert_true(key(f, GDK_KEY_Down, GDK_MOD1_MASK));
    settle();
    GtkWidget *recent = control(f, "cw-search-history");
    popup = GTK_WIDGET(gtk_menu_button_get_popover(GTK_MENU_BUTTON(recent)));
    GtkWidget *tree = find_named(popup, "cw-recent-list");
    g_assert_true(gtk_widget_get_mapped(tree));
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == tree);
    GtkTreePath *second = gtk_tree_path_new_from_indices(1, -1);
    gtk_tree_view_row_activated(GTK_TREE_VIEW(tree), second, gtk_tree_view_get_column(GTK_TREE_VIEW(tree), 0));
    gtk_tree_path_free(second);
    assert_match(f, "alpha", 0);
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(control(f, "cw-find"))), ==, "alpha");
}

static void
rapid_preview(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_autoptr(GString) manuscript = g_string_new("firstmatch\n");
    for (guint i = 0; i < 8000; i++)
        g_string_append(manuscript, "A line of manuscript text.\n");
    g_string_append(manuscript, "lastmatch");
    gtk_text_buffer_set_text(f->buffer, manuscript->str, -1);
    cw_search_show(f->search, FALSE);
    query(f, "lastmatch");
    query(f, "missing");
    query(f, "firstmatch");
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!gtk_text_buffer_get_has_selection(f->buffer) && g_get_monotonic_time() < deadline) {
        settle();
        g_usleep(1000);
    }
    assert_match(f, "firstmatch", 0);
    query(f, "lastmatch");
    /* Teardown cancels the pending search before its buffer loses its view. */
}

static void
repeat_while_previewing(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_autoptr(GString) manuscript = g_string_new(NULL);
    for (guint i = 0; i < 6000; i++)
        g_string_append(manuscript, "Written line.\n");
    g_string_append(manuscript, "needle\nneedle\nneedle");
    gtk_text_buffer_set_text(f->buffer, manuscript->str, -1);
    place(f, 0, 0);
    cw_search_find(f->search, TRUE);
    key(f, GDK_KEY_w, GDK_MOD1_MASK);
    query(f, "needle");
    /* The backward preview has no hit. The queued opposite repeat does. */
    g_assert_true(key(f, GDK_KEY_F3, GDK_SHIFT_MASK));
    gint64 deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!gtk_text_buffer_get_has_selection(f->buffer) && g_get_monotonic_time() < deadline) {
        settle();
        g_usleep(1000);
    }
    assert_match(f, "needle", 6000);
    assert_caret_centered(f);
}

static void
centered_matches(Fixture *f, gconstpointer unused)
{
    (void)unused;
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(f->view), GTK_WRAP_WORD_CHAR);
    g_autoptr(GString) manuscript = g_string_new("Crumb 1: : needle\n");
    for (guint i = 2; i < 65; i++) {
        g_string_append_printf(manuscript, "Crumb %u: : ", i);
        if (i == 32) {
            for (guint j = 0; j < 100; j++)
                g_string_append(manuscript, "written words ");
            g_string_append(manuscript, "needle");
        }
        g_string_append_c(manuscript, '\n');
    }
    g_string_append(manuscript, "Crumb 65: : needle");
    gtk_text_buffer_set_text(f->buffer, manuscript->str, -1);
    place(f, 0, 0);
    cw_search_find(f->search, FALSE);
    query(f, "needle");
    assert_match(f, "needle", 0);
    assert_caret_centered(f);
    g_assert_true(key(f, GDK_KEY_F3, 0));
    assert_match(f, "needle", 31);
    assert_caret_centered(f);
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == control(f, "cw-find"));
    g_assert_true(key(f, GDK_KEY_Escape, 0));
    g_assert_false(gtk_text_buffer_get_has_selection(f->buffer));
    assert_caret_centered(f);

    cw_search_find(f->search, TRUE);
    g_assert_true(key(f, GDK_KEY_r, GDK_CONTROL_MASK));
    query(f, "needle\\nCrumb");
    g_signal_emit_by_name(control(f, "cw-find"), "activate");
    assert_match(f, "needle\nCrumb", 0);
    assert_caret_centered(f);

    cw_search_number(f->search);
    GtkWidget *number = control(f, "cw-crumb-number");
    gtk_entry_set_text(GTK_ENTRY(number), "65");
    g_signal_emit_by_name(number, "activate");
    assert_caret(f, 64, 0);
    g_assert_false(gtk_widget_get_visible(f->panel));
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == f->view);
    assert_caret_centered(f);
}

static void
search_after_word_separator(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_autoptr(GString) manuscript = g_string_new("Crumb 1: : needle\n");
    for (guint i = 2; i < 60; i++)
        g_string_append_printf(manuscript, "Crumb %u: : Body.\n", i);
    g_string_append(manuscript, "Crumb 60: : Writing");
    gtk_text_buffer_set_text(f->buffer, manuscript->str, -1);
    CwEditorSpacing *spacing = cw_editor_spacing_new(f->buffer);
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(f->buffer, &end);
    gtk_text_buffer_place_cursor(f->buffer, &end);
    gtk_text_buffer_begin_user_action(f->buffer);
    gtk_text_buffer_insert_at_cursor(f->buffer, " ", 1);
    gtk_text_buffer_end_user_action(f->buffer);
    cw_search_find(f->search, TRUE);
    query(f, "needle");
    assert_match(f, "needle", 0);
    assert_caret_centered(f);
    g_assert_true(key(f, GDK_KEY_Escape, 0));
    assert_caret_centered(f);
    g_assert_cmpint(gtk_text_buffer_get_char_count(f->buffer), ==, (gint)manuscript->len);
    cw_editor_spacing_free(spacing);
}

static void
directions(Fixture *f, gconstpointer unused)
{
    (void)unused;
    const gchar *text = "è needle\nold nest\nanchor\nnew nest\nnext needle\nlast needle";
    gtk_text_buffer_set_text(f->buffer, text, -1);
    place(f, 2, 3);
    cw_search_find(f->search, TRUE);
    query(f, "ne"); assert_match(f, "ne", 1);
    query(f, "needle"); assert_match(f, "needle", 0);
    query(f, "ne"); assert_match(f, "ne", 1);
    query(f, "needle"); assert_match(f, "needle", 0);
    query(f, "needless"); assert_caret(f, 2, 3);
    g_assert_false(gtk_text_buffer_get_has_selection(f->buffer));
    query(f, "needle"); assert_match(f, "needle", 0);
    query(f, ""); assert_caret(f, 2, 3);
    query(f, "needle");
    g_signal_emit_by_name(control(f, "cw-find"), "activate");
    assert_match(f, "needle", 5);
    key(f, GDK_KEY_F3, 0); assert_match(f, "needle", 4);
    key(f, GDK_KEY_F3, GDK_SHIFT_MASK); assert_match(f, "needle", 5);

    /* A subsequent opener takes the current manuscript caret as a fresh origin. */
    gtk_widget_grab_focus(f->view);
    place(f, 2, 3);
    cw_search_find(f->search, FALSE);
    gint a, b;
    g_assert_true(gtk_editable_get_selection_bounds(GTK_EDITABLE(control(f, "cw-find")), &a, &b));
    g_assert_cmpint(a, ==, 0);
    g_assert_cmpint(b, ==, 6);
    query(f, "ne"); assert_match(f, "ne", 3);
    query(f, "needle"); assert_match(f, "needle", 4);
    query(f, "ne"); assert_match(f, "ne", 3);
    gtk_widget_grab_focus(f->view);
    place(f, 0, 1);
    query(f, "needle"); assert_match(f, "needle", 0);

    place(f, 0, 0);
    cw_search_find(f->search, TRUE);
    query(f, "need"); assert_match(f, "need", 5);
    key(f, GDK_KEY_w, GDK_MOD1_MASK);
    g_assert_false(gtk_text_buffer_get_has_selection(f->buffer));
    assert_caret(f, 0, 0);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(control(f, "cw-search-status"))), ==, "No earlier match");
    key(f, GDK_KEY_w, GDK_MOD1_MASK); assert_match(f, "need", 5);
    key(f, GDK_KEY_Escape, 0); assert_caret(f, 5, 5);
    g_assert_false(gtk_text_buffer_get_has_selection(f->buffer));
}

static void
repeat_direction(Fixture *f, gconstpointer data)
{
    gboolean backwards = GPOINTER_TO_INT(data);
    gint nearest = backwards ? 2 : 4;
    gint next = backwards ? 1 : 5;
    gtk_text_buffer_set_text(f->buffer, "needle\nneedle\nneedle\nanchor\nneedle\nneedle\nneedle", -1);
    place(f, 3, 2);
    cw_search_find(f->search, backwards);
    query(f, "needle");
    assert_match(f, "needle", nearest);
    GtkWidget *entry = control(f, "cw-find");
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == entry);
    g_assert_true(key(f, GDK_KEY_F3, 0)); assert_match(f, "needle", next);
    g_assert_true(key(f, GDK_KEY_F3, GDK_SHIFT_MASK)); assert_match(f, "needle", nearest);
    g_signal_emit_by_name(entry, "activate"); assert_match(f, "needle", next);
    GdkEventKey shifted_enter = {.type = GDK_KEY_PRESS, .keyval = GDK_KEY_Return, .state = GDK_SHIFT_MASK};
    gboolean handled = FALSE;
    g_signal_emit_by_name(entry, "key-press-event", &shifted_enter, &handled);
    g_assert_true(handled); assert_match(f, "needle", nearest);

    /* The toolbar arrows always follow their physical direction. */
    gtk_button_clicked(GTK_BUTTON(control(f, "cw-search-previous")));
    assert_match(f, "needle", backwards ? 1 : 2);
    gtk_button_clicked(GTK_BUTTON(control(f, "cw-search-next")));
    assert_match(f, "needle", nearest);

    gtk_widget_grab_focus(f->view);
    g_assert_true(key(f, GDK_KEY_F3, 0)); assert_match(f, "needle", next);
    g_assert_true(key(f, GDK_KEY_F3, GDK_SHIFT_MASK)); assert_match(f, "needle", nearest);
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == f->view);

    g_assert_true(key(f, GDK_KEY_Escape, 0));
    g_assert_false(gtk_widget_get_visible(f->panel));
    g_assert_false(gtk_text_buffer_get_has_selection(f->buffer));
    assert_caret(f, nearest, 0);
    g_assert_true(key(f, GDK_KEY_F3, 0)); assert_match(f, "needle", next);
    g_assert_true(key(f, GDK_KEY_Escape, 0));
    g_assert_true(key(f, GDK_KEY_F3, GDK_SHIFT_MASK)); assert_match(f, "needle", nearest);
    g_assert_false(gtk_widget_get_visible(f->panel));
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == f->view);

    /* Typing establishes a new origin while preserving the chosen direction. */
    place(f, 3, 1);
    gtk_text_buffer_insert_at_cursor(f->buffer, "x", -1);
    cw_search_repeat(f->search, FALSE); assert_match(f, "needle", nearest);

    place(f, 3, 2);
    cw_search_find(f->search, backwards);
    g_assert_true(key(f, GDK_KEY_r, GDK_CONTROL_MASK));
    query(f, "n[e]+dle");
    g_signal_emit_by_name(entry, "activate"); assert_match(f, "needle", nearest);
    g_assert_true(key(f, GDK_KEY_F3, 0)); assert_match(f, "needle", next);
    g_assert_true(key(f, GDK_KEY_F3, GDK_SHIFT_MASK)); assert_match(f, "needle", nearest);
}

static void
numeric_duplicates(Fixture *f, gconstpointer unused)
{
    (void)unused;
    gtk_text_buffer_set_text(f->buffer, "Book//Leaf 9: A: x\nLeaf 5: B: here\nLeaf 9: C: x", -1);
    place(f, 1, 5);
    cw_search_find(f->search, TRUE);
    key(f, GDK_KEY_n, GDK_CONTROL_MASK);
    gtk_entry_set_text(GTK_ENTRY(control(f, "cw-crumb-number")), "9");
    g_signal_emit_by_name(control(f, "cw-crumb-number"), "activate");
    assert_caret(f, 0, 6);
    place(f, 1, 5);
    cw_search_number(f->search);
    g_signal_emit_by_name(control(f, "cw-crumb-number"), "activate");
    assert_caret(f, 2, 0);
    place(f, 2, 8);
    cw_search_number(f->search);
    g_assert_true(gtk_widget_get_visible(control(f, "cw-search-wrap")));
    key(f, GDK_KEY_w, GDK_MOD1_MASK);
    g_signal_emit_by_name(control(f, "cw-crumb-number"), "activate");
    assert_caret(f, 2, 8);
    g_assert_true(gtk_widget_get_visible(f->panel));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(control(f, "cw-search-status"))),
                    ==, "No later crumb numbered 9");
}

int
main(int argc, char **argv)
{
    gtk_test_init(&argc, &argv, NULL);
    g_test_add("/search/controls/live-find", Fixture, NULL, setup, live_find_and_keys, teardown);
    g_test_add("/search/controls/regex", Fixture, NULL, setup, regex_modes, teardown);
    g_test_add("/search/controls/popovers-and-tile", Fixture, NULL, setup, popovers_and_tile, teardown);
    g_test_add("/search/controls/rapid-preview", Fixture, NULL, setup, rapid_preview, teardown);
    g_test_add("/search/controls/queued-repeat", Fixture, NULL, setup, repeat_while_previewing, teardown);
    g_test_add("/search/controls/centered-matches", Fixture, NULL, setup, centered_matches, teardown);
    g_test_add("/search/controls/pending-word-separator", Fixture, NULL, setup, search_after_word_separator, teardown);
    g_test_add("/search/controls/cursor-directions", Fixture, NULL, setup, directions, teardown);
    g_test_add("/search/controls/repeat-forward", Fixture, GINT_TO_POINTER(FALSE), setup, repeat_direction, teardown);
    g_test_add("/search/controls/repeat-backward", Fixture, GINT_TO_POINTER(TRUE), setup, repeat_direction, teardown);
    g_test_add("/search/controls/number-duplicates", Fixture, NULL, setup, numeric_duplicates, teardown);
    return g_test_run();
}

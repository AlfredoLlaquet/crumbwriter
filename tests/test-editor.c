#include "editor.h"
#include "reveal.h"
#include "search.h"
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkBox *layout;
    GtkSourceView *view;
    GtkTextBuffer *buffer;
    CwEditor *editor;
    CwSearch *search;
    GKeyFile *prefs;
} Fixture;

static void
settle(void)
{
    while (g_main_context_iteration(NULL, FALSE))
        ;
}

static void
setup(Fixture *f, gconstpointer data)
{
    (void)data;
    GtkSourceBuffer *buffer = gtk_source_buffer_new(NULL);
    f->view = GTK_SOURCE_VIEW(gtk_source_view_new_with_buffer(buffer));
    f->buffer = GTK_TEXT_BUFFER(buffer);
    g_object_unref(buffer);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(f->view), GTK_WRAP_WORD_CHAR);
    f->editor = cw_editor_new(f->view);

    f->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(f->window), 800, 400);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    f->layout = GTK_BOX(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0));
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(f->view));
    cw_reveal_configure(GTK_TEXT_VIEW(f->view));
    gtk_box_pack_start(GTK_BOX(box), cw_editor_sidebar(f->editor), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    gtk_box_pack_end(f->layout, box, TRUE, TRUE, 0);
    gtk_container_add(GTK_CONTAINER(f->window), GTK_WIDGET(f->layout));
    gtk_widget_show_all(f->window);
    gtk_widget_grab_focus(GTK_WIDGET(f->view));
    settle();
}

static void
teardown(Fixture *f, gconstpointer data)
{
    (void)data;
    if (f->search) {
        cw_search_free(f->search);
        g_key_file_unref(f->prefs);
    }
    cw_editor_free(f->editor);
    gtk_widget_destroy(f->window);
    settle();
}

static void
assert_text(Fixture *f, const gchar *expected)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(f->buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(f->buffer, &start, &end, TRUE);
    g_assert_cmpstr(text, ==, expected);
    g_free(text);
}

static GtkTextIter
cursor(Fixture *f)
{
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_mark(f->buffer, &it, gtk_text_buffer_get_insert(f->buffer));
    return it;
}

static void
assert_cursor(Fixture *f, gint line, const gchar *preceding)
{
    GtkTextIter it = cursor(f);
    g_assert_cmpint(gtk_text_iter_get_line(&it), ==, line);
    g_assert_cmpint(gtk_text_iter_get_line_offset(&it), ==, g_utf8_strlen(preceding, -1));
}

static void
key(Fixture *f, guint keyval, GdkModifierType state)
{
    GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
    GdkWindow *window = gtk_text_view_get_window(GTK_TEXT_VIEW(f->view), GTK_TEXT_WINDOW_TEXT);
    GdkDisplay *display = gdk_window_get_display(window);
    event->key.window = g_object_ref(window);
    event->key.keyval = keyval;
    event->key.state = state;
    event->key.send_event = TRUE;
    GdkKeymapKey *entries = NULL;
    gint n_entries = 0;
    if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(display),
                                          keyval, &entries, &n_entries)) {
        event->key.hardware_keycode = entries[0].keycode;
        event->key.group = entries[0].group;
        g_free(entries);
    }
    GdkSeat *seat = gdk_display_get_default_seat(display);
    gdk_event_set_device(event, gdk_seat_get_keyboard(seat));
    gunichar c = gdk_keyval_to_unicode(keyval);
    if (c && g_unichar_isprint(c)) {
        gchar utf8[7];
        event->key.length = g_unichar_to_utf8(c, utf8);
        event->key.string = g_strndup(utf8, event->key.length);
    }
    gboolean handled = gtk_widget_event(GTK_WIDGET(f->view), event);
    gdk_event_free(event);
    g_assert_true(handled);
    settle();
}

static void
append_and_undo(Fixture *f, gconstpointer data)
{
    const gchar *ending = data;
    gchar *original = g_strconcat("Quadern//Bocí 1: Àmbar: Primer.", ending, NULL);
    gchar *next = g_strconcat("Quadern//Bocí 1: Àmbar: Primer.\nBocí 2: : ", ending, NULL);
    cw_editor_load(f->editor, original);
    cw_editor_append(f->editor);
    assert_text(f, next);
    assert_cursor(f, 1, "Bocí 2: : ");
    g_assert_true(gtk_source_buffer_can_undo(GTK_SOURCE_BUFFER(f->buffer)));
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, original);
    g_assert_true(gtk_source_buffer_can_redo(GTK_SOURCE_BUFFER(f->buffer)));
    gtk_source_buffer_redo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, next);
    g_free(original);
    g_free(next);
}

static void
field_and_crumb_navigation(Fixture *f, gconstpointer data)
{
    (void)data;
    cw_editor_load(f->editor, "Títol//Bocí 1: Àmbar: Un.\nBocí 2: : Dos.\n");
    GtkTextIter it;
    gtk_text_buffer_get_start_iter(f->buffer, &it);
    gtk_text_buffer_place_cursor(f->buffer, &it);
    key(f, GDK_KEY_Tab, 0);
    assert_cursor(f, 0, "Títol//");
    key(f, GDK_KEY_Tab, 0);
    assert_cursor(f, 0, "Títol//Bocí 1: ");
    key(f, GDK_KEY_Tab, 0);
    assert_cursor(f, 0, "Títol//Bocí 1: Àmbar: ");
    key(f, GDK_KEY_Tab, 0);
    assert_cursor(f, 1, "");
    key(f, GDK_KEY_Tab, 0);
    assert_cursor(f, 1, "Bocí 2: ");
    key(f, GDK_KEY_Tab, 0);
    assert_cursor(f, 1, "Bocí 2: : ");
    key(f, GDK_KEY_ISO_Left_Tab, GDK_SHIFT_MASK);
    assert_cursor(f, 1, "Bocí 2: ");
    key(f, GDK_KEY_Up, GDK_CONTROL_MASK);
    assert_cursor(f, 0, "Títol//");
    key(f, GDK_KEY_Down, GDK_CONTROL_MASK);
    assert_cursor(f, 1, "");
}

static void
visual_line_navigation(Fixture *f, gconstpointer data)
{
    (void)data;
    GString *text = g_string_new("Crumb 1: Long: ");
    for (gint i = 0; i < 80; ++i)
        g_string_append(text, "one more word ");
    g_string_append(text, "\nCrumb 2: Short: Done.\n");
    cw_editor_load(f->editor, text->str);
    g_string_free(text, TRUE);
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(f->buffer, &start);
    gtk_text_buffer_place_cursor(f->buffer, &start);
    settle();

    GtkTextView *view = GTK_TEXT_VIEW(f->view);
    GdkRectangle before, after;
    gtk_text_view_get_iter_location(view, &start, &before);
    key(f, GDK_KEY_Down, 0);
    GtkTextIter next = cursor(f);
    gtk_text_view_get_iter_location(view, &next, &after);
    g_assert_cmpint(gtk_text_iter_get_line(&next), ==, 0);
    g_assert_cmpint(after.y, >, before.y);
    key(f, GDK_KEY_Up, 0);
    assert_cursor(f, 0, "");
}

static void
record_and_file_boundaries(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *first = "Obra//  Bocí 1: Àmbar: A.  ";
    GString *wrapped = g_string_new("  Bocí 2: Volta: ");
    for (guint i = 0; i < 80; i++)
        g_string_append(wrapped, "finestra ");
    g_string_append(wrapped, "fi.   ");
    const gchar *last = "Bocí 3: Darrer: Z. ";
    g_autofree gchar *text = g_strjoin("\n", first, wrapped->str, last, "", NULL);
    cw_editor_load_at(f->editor, text, 1, 250);
    settle();
    GtkTextIter start, inside = cursor(f);
    gtk_text_buffer_get_iter_at_line(f->buffer, &start, 1);
    GdkRectangle top, middle;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(f->view), &start, &top);
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(f->view), &inside, &middle);
    g_assert_cmpint(middle.y, >, top.y);

    key(f, GDK_KEY_Home, GDK_CONTROL_MASK);
    assert_cursor(f, 1, "  ");
    key(f, GDK_KEY_End, GDK_CONTROL_MASK);
    assert_cursor(f, 1, wrapped->str);
    key(f, GDK_KEY_KP_Home, GDK_CONTROL_MASK);
    assert_cursor(f, 1, "  ");
    key(f, GDK_KEY_KP_End, GDK_CONTROL_MASK);
    assert_cursor(f, 1, wrapped->str);
    key(f, GDK_KEY_Prior, GDK_CONTROL_MASK);
    assert_cursor(f, 0, "");
    key(f, GDK_KEY_Begin, GDK_CONTROL_MASK);
    assert_cursor(f, 0, "Obra//  ");
    key(f, GDK_KEY_KP_End, GDK_CONTROL_MASK);
    assert_cursor(f, 0, first);
    key(f, GDK_KEY_Next, GDK_CONTROL_MASK);
    assert_cursor(f, 3, "");
    key(f, GDK_KEY_End, GDK_CONTROL_MASK);
    assert_cursor(f, 2, last);
    key(f, GDK_KEY_KP_Next, GDK_CONTROL_MASK);
    assert_cursor(f, 3, "");
    key(f, GDK_KEY_KP_Begin, GDK_CONTROL_MASK);
    assert_cursor(f, 2, "");
    key(f, GDK_KEY_KP_Prior, GDK_CONTROL_MASK);
    assert_cursor(f, 0, "");
    g_string_free(wrapped, TRUE);
    assert_text(f, text);
}

static void
draft_boundaries(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *partial = "  esborrany   ";
    g_autofree gchar *text = g_strconcat("Bocí 1: : A\n", partial, NULL);
    cw_editor_load_at(f->editor, text, 1, 5);
    key(f, GDK_KEY_Home, GDK_CONTROL_MASK);
    assert_cursor(f, 1, "");
    key(f, GDK_KEY_End, GDK_CONTROL_MASK);
    assert_cursor(f, 1, partial);
    key(f, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
    assert_cursor(f, 0, "");
    key(f, GDK_KEY_Page_Down, GDK_CONTROL_MASK);
    assert_cursor(f, 1, partial);
    cw_editor_load(f->editor, "Bocí 1: : A\n\n");
    key(f, GDK_KEY_Page_Down, GDK_CONTROL_MASK);
    assert_cursor(f, 2, "");
    key(f, GDK_KEY_End, GDK_CONTROL_MASK);
    assert_cursor(f, 0, "Bocí 1: : A");
    cw_editor_load(f->editor, "");
    key(f, GDK_KEY_End, GDK_CONTROL_MASK);
    assert_cursor(f, 0, "");
}

static void
settings_changed(gpointer data)
{
    (void)data;
}

static void
setup_search(Fixture *f, gconstpointer data)
{
    setup(f, data);
    f->prefs = g_key_file_new();
    f->search = cw_search_new(f->view, f->prefs, settings_changed, f);
    gtk_box_pack_start(f->layout, cw_search_widget(f->search), FALSE, FALSE, 0);
}

static GtkWidget *
find_named_widget(GtkWidget *root, const gchar *name)
{
    if (g_strcmp0(gtk_widget_get_name(root), name) == 0)
        return root;
    if (!GTK_IS_CONTAINER(root))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget *found = NULL;
    for (GList *item = children; item && !found; item = item->next)
        found = find_named_widget(GTK_WIDGET(item->data), name);
    g_list_free(children);
    return found;
}

static GtkWidget *
control(Fixture *f, const gchar *name)
{
    GtkWidget *widget = find_named_widget(cw_search_widget(f->search), name);
    g_assert_nonnull(widget);
    return widget;
}

static void
assert_sidebar(Fixture *f, const gchar *name, const gchar *value)
{
    GtkWidget *label = find_named_widget(cw_editor_sidebar(f->editor), name);
    g_assert_true(GTK_IS_LABEL(label));
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(label)), ==, value);
}

/* Choose the level that actually produces # in this keyboard layout. */
static void
type_hash(Fixture *f)
{
    GdkKeymap *map = gdk_keymap_get_for_display(gtk_widget_get_display(GTK_WIDGET(f->view)));
    GdkKeymapKey *entries = NULL;
    gint count;
    g_assert_true(gdk_keymap_get_entries_for_keyval(map, GDK_KEY_numbersign, &entries, &count));
    const GdkModifierType layers[] = { 0, GDK_SHIFT_MASK, GDK_MOD5_MASK,
                                      GDK_MOD5_MASK | GDK_SHIFT_MASK };
    for (guint i = 0; i < G_N_ELEMENTS(layers); i++) {
        guint keyval;
        if (gdk_keymap_translate_keyboard_state(map, entries[0].keycode, layers[i],
                                                entries[0].group, &keyval, NULL, NULL, NULL) &&
            keyval == GDK_KEY_numbersign) {
            g_free(entries);
            key(f, keyval, layers[i]);
            return;
        }
    }
    g_free(entries);
    g_assert_not_reached();
}

static void
complete_genre_number(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *old = "Book//Leaf 1: Décors mécaniques #43: x.\n"
                       "Leaf 2: de\314\201cors mécaniques #7: y.\n";
    const gchar *prefix = "Leaf 3: DÉCORS  MÉCANIQUES   ";
    const gchar *rest = ": \nLeaf 4: Décors mécaniques #99: ";
    g_autofree gchar *original = g_strconcat(old, prefix, rest, NULL);
    g_autofree gchar *completed = g_strconcat(old, prefix, "#8", rest, NULL);
    cw_editor_load_at(f->editor, original, 2, g_utf8_strlen(prefix, -1));
    type_hash(f);
    assert_text(f, completed);
    assert_cursor(f, 2, "Leaf 3: DÉCORS  MÉCANIQUES   #8");
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, original);
    gtk_source_buffer_redo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, completed);
}

static void
literal_genre_hash(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *drafts[] = {
        "Crumb 1: Haiku #3: /\nCrumb 2: Haiku |55: x",
        "Crumb 1: Haiku #3: /\nCrumb 2: Haiku | here: x",
        "Crumb 1: Haiku #3: /\nCrumb 2: Haiku|: x",
        "Crumb 1: Haiku #3: /\nCrumb 2: T: Haiku |",
        "Crumb 1: Haiku #3: /\nCrumb 2: Other |: x",
        "Crumb 1: Haiku #3: /\nCrumb 2: Overflow #18446744073709551616: /\nCrumb 3: Overflow |: ",
        "Crumb 1: Haiku |: x\nCrumb 2: Haiku #3: /",
    };
    for (guint i = 0; i < G_N_ELEMENTS(drafts); i++) {
        gchar **parts = g_strsplit(drafts[i], "|", 2);
        g_autofree gchar *original = g_strconcat(parts[0], parts[1], NULL);
        g_autofree gchar *literal = g_strconcat(parts[0], "#", parts[1], NULL);
        cw_editor_load(f->editor, original);
        GtkTextIter it;
        gtk_text_buffer_get_iter_at_offset(f->buffer, &it, g_utf8_strlen(parts[0], -1));
        gtk_text_buffer_place_cursor(f->buffer, &it);
        type_hash(f);
        assert_text(f, literal);
        g_strfreev(parts);
    }

    const gchar *draft = "Crumb 1: Haiku #3: /\nCrumb 2: Haiku ";
    cw_editor_load_at(f->editor, draft, -1, 0);
    gtk_text_buffer_insert_at_cursor(f->buffer, "#", 1);
    assert_text(f, "Crumb 1: Haiku #3: /\nCrumb 2: Haiku #");
    cw_editor_load_at(f->editor, draft, -1, 0);
    GtkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(f->view), GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, "#", 1);
    g_signal_emit_by_name(f->view, "paste-clipboard");
    const gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while (gtk_text_buffer_get_char_count(f->buffer) == (gint)strlen(draft) &&
           g_get_monotonic_time() < deadline) {
        settle();
        g_usleep(1000);
    }
    assert_text(f, "Crumb 1: Haiku #3: /\nCrumb 2: Haiku #");
}

static void
append_listed_genre(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *original = "Book//Folio 5: Sketch #70: x\n"
                            "Folio 9: Chord #11: y\nFolio 10: sKeTcH #3: z\n";
    const gchar *next = "Folio 11: sKeTcH #4: ";
    g_autofree gchar *expected = g_strconcat(original, next, "\n", NULL);
    cw_editor_load_at(f->editor, original, 0, 0);
    GtkTreeView *genres = GTK_TREE_VIEW(find_named_widget(cw_editor_sidebar(f->editor), "crumb-genres"));
    g_assert_true(GTK_IS_TREE_VIEW(genres));
    cw_editor_focus_genres(f->editor);
    g_assert_true(gtk_window_get_focus(GTK_WINDOW(f->window)) == GTK_WIDGET(genres));
    GtkTreePath *path = gtk_tree_path_new_from_indices(1, -1); /* Chord, then Sketch */
    gtk_tree_view_row_activated(genres, path, gtk_tree_view_get_column(genres, 0));
    assert_text(f, expected);
    assert_cursor(f, 3, next);
    assert_sidebar(f, "session-crumbs", "1");
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, original);

    /* Removing the latest occurrence rolls the alphabetized entry back to #70. */
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_line(f->buffer, &start, 2);
    gtk_text_buffer_get_end_iter(f->buffer, &end);
    gtk_text_buffer_delete(f->buffer, &start, &end);
    gtk_tree_view_row_activated(genres, path, gtk_tree_view_get_column(genres, 0));
    assert_cursor(f, 2, "Folio 10: Sketch #71: ");
    gtk_tree_path_free(path);
}

static void
compound_genres(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *draft =
        "Book//Crumb 1: Haiku #7, Haiku #5, Echo #3: a\n"
        "Crumb 2: The Headline, Report #4: x\n"
        "Crumb 3: Haiku #38, Fingers (HAIKU ), Echo #24: a\n"
        "Crumb 4: Echo #31, Report , Haiku #77: b\n"
        "Crumb 5: Report #49, Echo #90: c";
    cw_editor_load_at(f->editor, draft, 2, strlen("Crumb 3: Haiku #38, Fingers (HAIKU "));
    type_hash(f);
    assert_cursor(f, 2, "Crumb 3: Haiku #38, Fingers (HAIKU #6");
    assert_sidebar(f, "crumb-title", "Haiku #38, Fingers (HAIKU #6), Echo #24");

    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line_offset(f->buffer, &it, 3, strlen("Crumb 4: Echo #31, Report "));
    gtk_text_buffer_place_cursor(f->buffer, &it);
    type_hash(f);
    assert_cursor(f, 3, "Crumb 4: Echo #31, Report #5");
    assert_sidebar(f, "crumb-title", "Echo #31, Report #5, Haiku #77");

    GtkTreeView *genres = GTK_TREE_VIEW(find_named_widget(cw_editor_sidebar(f->editor), "crumb-genres"));
    g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(genres), NULL), ==, 3);
    GtkTreePath *path = gtk_tree_path_new_from_indices(2, -1); /* Echo, Haiku, Report. */
    gtk_tree_view_row_activated(genres, path, gtk_tree_view_get_column(genres, 0));
    assert_cursor(f, 5, "Crumb 6: Report #50: ");
    gtk_tree_path_free(path);
}

static void
sidebar_and_history(Fixture *f, gconstpointer data)
{
    (void)data;
    cw_editor_load(f->editor, "Book//Crumb 1: Alpha: One—two / three.\nCrumb 2: Beta: Four.\n");
    assert_sidebar(f, "crumb-number", "Crumb 1");
    assert_sidebar(f, "crumb-title", "Alpha");
    assert_sidebar(f, "crumb-words", "6");
    assert_sidebar(f, "session-crumbs", "0");
    cw_editor_append(f->editor);
    assert_sidebar(f, "session-crumbs", "1");
    key(f, GDK_KEY_Up, GDK_CONTROL_MASK);
    assert_sidebar(f, "crumb-number", "Crumb 2");

    /* Visits in reverse order are now 2, 3, 1. Activate the middle row. */
    GtkTreeView *history = GTK_TREE_VIEW(find_named_widget(cw_editor_sidebar(f->editor), "crumb-history"));
    GtkTreePath *path = gtk_tree_path_new_from_indices(1, -1);
    gtk_tree_view_row_activated(history, path, gtk_tree_view_get_column(history, 0));
    gtk_tree_path_free(path);
    assert_cursor(f, 2, "");
    key(f, GDK_KEY_Tab, 0);
    gtk_text_buffer_insert_at_cursor(f->buffer, "New Title", -1);
    assert_sidebar(f, "crumb-title", "New Title");
    assert_sidebar(f, "crumb-words", "4");

    GtkTextIter end;
    gtk_text_buffer_get_end_iter(f->buffer, &end);
    gtk_text_buffer_begin_user_action(f->buffer);
    gtk_text_buffer_insert(f->buffer, &end, "Crumb 4: A: Body.\nCrumb 5: B: Body.\n", -1);
    gtk_text_buffer_end_user_action(f->buffer);
    assert_sidebar(f, "session-crumbs", "3");
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_sidebar(f, "session-crumbs", "1");
}

static gint
selected_line(Fixture *f)
{
    GtkTextIter start, end;
    g_assert_true(gtk_text_buffer_get_selection_bounds(f->buffer, &start, &end));
    gchar *match = gtk_text_buffer_get_text(f->buffer, &start, &end, TRUE);
    g_assert_cmpint(g_ascii_strcasecmp(match, "stone"), ==, 0);
    g_free(match);
    return gtk_text_iter_get_line(&start);
}

static void
search_cycle_and_number(Fixture *f, gconstpointer data)
{
    (void)data;
    cw_editor_load(f->editor, "Book//Leaf 9: First: Stone.\nLeaf 42: Last: sToNe.\n");
    cw_search_show(f->search, FALSE);
    gtk_entry_set_text(GTK_ENTRY(control(f, "cw-find")), "stone");
    g_assert_cmpint(selected_line(f), ==, 0);
    cw_search_step(f->search, FALSE);
    gint first = selected_line(f);
    cw_search_step(f->search, FALSE);
    g_assert_cmpint(selected_line(f), !=, first);
    cw_search_step(f->search, FALSE);
    g_assert_cmpint(selected_line(f), ==, first);

    cw_search_number(f->search);
    GtkEntry *number = GTK_ENTRY(control(f, "cw-crumb-number"));
    gtk_entry_set_text(number, "9");
    g_signal_emit_by_name(number, "activate");
    assert_cursor(f, 0, "Book//");
    cw_search_number(f->search);
    gtk_entry_set_text(number, "42");
    g_signal_emit_by_name(number, "activate");
    assert_cursor(f, 1, "");
}

static void
assert_caret_in_view(Fixture *f, gint line)
{
    settle();
    GtkTextView *view = GTK_TEXT_VIEW(f->view);
    GdkRectangle visible, caret;
    GtkTextIter position = cursor(f);
    g_assert_cmpint(gtk_text_iter_get_line(&position), ==, line);
    gtk_text_view_get_visible_rect(view, &visible);
    gtk_text_view_get_iter_location(view, &position, &caret);
    g_assert_cmpint(caret.y, >=, visible.y);
    g_assert_cmpint(caret.y + caret.height, <=, visible.y + visible.height);
}

static void
large_document_jumps(Fixture *f, gconstpointer data)
{
    (void)data;
    GString *text = g_string_new(NULL);
    for (gint i = 1; i <= 1800; i++) {
        g_string_append_printf(text, "Crumb %d: In the garden: ", i);
        for (gint j = 0; j < 12; j++)
            g_string_append(text, "The rain falls through the leaves. ");
        g_string_append_c(text, '\n');
    }
    cw_editor_load_at(f->editor, text->str, 1450, 0);
    g_string_free(text, TRUE);
    assert_caret_in_view(f, 1450);
    cw_search_number(f->search);
    GtkEntry *number = GTK_ENTRY(control(f, "cw-crumb-number"));
    gtk_entry_set_text(number, "2");
    g_signal_emit_by_name(number, "activate");
    assert_caret_in_view(f, 1);
    cw_search_number(f->search);
    gtk_entry_set_text(number, "1600");
    g_signal_emit_by_name(number, "activate");
    assert_caret_in_view(f, 1599);
}

static void
regex_replace_and_undo(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *original = "Crumb 1: : word 3.\nCrumb 2: : Word 10.\n";
    cw_editor_load(f->editor, original);
    cw_search_show(f->search, TRUE);
    GdkEventKey regex = {.keyval = GDK_KEY_r, .state = GDK_CONTROL_MASK};
    g_assert_true(cw_search_handle_key(f->search, &regex));
    gtk_entry_set_text(GTK_ENTRY(control(f, "cw-find")), "word ([[:digit:]]+)");
    gtk_entry_set_text(GTK_ENTRY(control(f, "cw-replacement")), "entry \\1");
    GtkWidget *replace_all = control(f, "cw-replace-all");
    const gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while (!gtk_widget_is_sensitive(replace_all) && g_get_monotonic_time() < deadline) {
        settle();
        g_usleep(1000);
    }
    g_assert_true(gtk_widget_is_sensitive(replace_all));
    gtk_button_clicked(GTK_BUTTON(replace_all));
    assert_text(f, original);
    gtk_button_clicked(GTK_BUTTON(replace_all));
    assert_text(f, "Crumb 1: : entry 3.\nCrumb 2: : entry 10.\n");
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, original);
}

static GdkEvent *
pointer_event(Fixture *f, GdkEventType type)
{
    GdkWindow *window = gtk_text_view_get_window(GTK_TEXT_VIEW(f->view), GTK_TEXT_WINDOW_TEXT);
    GdkSeat *seat = gdk_display_get_default_seat(gdk_window_get_display(window));
    GdkEvent *event = gdk_event_new(type);
    event->any.window = g_object_ref(window);
    event->any.send_event = TRUE;
    gdk_event_set_device(event, gdk_seat_get_pointer(seat));
    gdk_event_set_source_device(event, gdk_seat_get_pointer(seat));
    return event;
}

static void
touchpad(Fixture *f, guint32 time, gdouble delta, gboolean stop)
{
    GdkEvent *event = pointer_event(f, GDK_SCROLL);
    event->scroll.time = time;
    event->scroll.direction = GDK_SCROLL_SMOOTH;
    event->scroll.delta_y = delta;
    event->scroll.is_stop = stop;
    gtk_main_do_event(event);
    gdk_event_free(event);
}

static void
touch(Fixture *f, GdkEventType type, guint32 time, gdouble y)
{
    GdkEvent *event = pointer_event(f, type);
    event->touch.time = time;
    event->touch.x = 60;
    event->touch.y = y;
    event->touch.sequence = (GdkEventSequence *)GINT_TO_POINTER(1);
    event->touch.emulating_pointer = TRUE;
    event->touch.state = type == GDK_TOUCH_BEGIN ? 0 : GDK_BUTTON1_MASK;
    gtk_main_do_event(event);
    gdk_event_free(event);
}

static void
instant_document_scroll(void)
{
    GtkSettings *settings = gtk_settings_get_default();
    gboolean original_animations;
    g_object_get(settings, "gtk-enable-animations", &original_animations, NULL);
    g_object_set(settings, "gtk-enable-animations", TRUE, NULL);
    Fixture f = {0};
    setup(&f, NULL);
    GString *text = g_string_new(NULL);
    for (gint i = 1; i <= 400; i++)
        g_string_append_printf(text, "Crumb %d: A title: A line of text.\n", i);
    cw_editor_load_at(f.editor, text->str, 0, 0);
    g_string_free(text, TRUE);
    settle();

    GtkAdjustment *doc = gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(f.view));
    GtkWidget *scroll = gtk_widget_get_ancestor(GTK_WIDGET(f.view), GTK_TYPE_SCROLLED_WINDOW);
    GtkWidget *bar = gtk_scrolled_window_get_vscrollbar(GTK_SCROLLED_WINDOW(scroll));
    gdouble first_page = gtk_adjustment_get_value(doc) + gtk_adjustment_get_page_increment(doc);
    g_signal_emit_by_name(f.view, "move-cursor", GTK_MOVEMENT_PAGES, 1, FALSE);
    g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(doc), first_page, 0.01);
    g_assert_cmpfloat_with_epsilon(gtk_range_get_value(GTK_RANGE(bar)), first_page, 0.01);

    gboolean handled;
    g_signal_emit_by_name(bar, "change-value", GTK_SCROLL_JUMP, 1800.0, &handled);
    g_assert_true(handled);
    g_assert_cmpfloat(gtk_adjustment_get_value(doc), ==, 1800.0);
    touchpad(&f, 100, .75, FALSE);
    touchpad(&f, 116, 1.25, FALSE);
    gdouble panned = 1800.0 + 2 * gtk_adjustment_get_step_increment(doc);
    g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(doc), panned, 0.01);
    touchpad(&f, 123, 0, TRUE);

    GtkTextIter before = cursor(&f);
    touch(&f, GDK_TOUCH_BEGIN, 300, 220);
    touch(&f, GDK_TOUCH_UPDATE, 311, 190);
    touch(&f, GDK_TOUCH_UPDATE, 321, 155);
    touch(&f, GDK_TOUCH_END, 329, 155);
    g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(doc), panned + 65, 0.01);
    GtkTextIter after = cursor(&f);
    g_assert_true(gtk_text_iter_equal(&before, &after));

    /* One GTK scroll animation normally lasts 200 ms. */
    gint64 deadline = g_get_monotonic_time() + 300 * G_TIME_SPAN_MILLISECOND;
    while (g_get_monotonic_time() < deadline) {
        g_main_context_iteration(NULL, FALSE);
        g_usleep(1000);
    }
    g_assert_cmpfloat_with_epsilon(gtk_adjustment_get_value(doc), panned + 65, 0.01);
    touch(&f, GDK_TOUCH_BEGIN, 930, 120);
    touch(&f, GDK_TOUCH_END, 940, 120);
    after = cursor(&f);
    g_assert_false(gtk_text_iter_equal(&before, &after));
    gboolean still_animated;
    g_object_get(settings, "gtk-enable-animations", &still_animated, NULL);
    g_assert_true(still_animated);
    teardown(&f, NULL);
    g_object_set(settings, "gtk-enable-animations", original_animations, NULL);
}

int
main(int argc, char **argv)
{
    gtk_test_init(&argc, &argv, NULL);
    g_test_add("/editor/genres/typed-number", Fixture, NULL, setup, complete_genre_number, teardown);
    g_test_add("/editor/genres/literal-hash", Fixture, NULL, setup, literal_genre_hash, teardown);
    g_test_add("/editor/genres/append-from-list", Fixture, NULL, setup, append_listed_genre, teardown);
    g_test_add("/editor/genres/compound-titles", Fixture, NULL, setup, compound_genres, teardown);
    g_test_add("/editor/append/final-lf", Fixture, "\n", setup, append_and_undo, teardown);
    g_test_add("/editor/append/no-final-lf", Fixture, "", setup, append_and_undo, teardown);
    g_test_add("/editor/navigation/fields", Fixture, NULL, setup, field_and_crumb_navigation, teardown);
    g_test_add("/editor/navigation/wrapped-lines", Fixture, NULL, setup, visual_line_navigation, teardown);
    g_test_add("/editor/navigation/record-and-file-boundaries", Fixture, NULL, setup, record_and_file_boundaries, teardown);
    g_test_add("/editor/navigation/draft-boundaries", Fixture, NULL, setup, draft_boundaries, teardown);
    g_test_add("/editor/sidebar/recency-and-edits", Fixture, NULL, setup, sidebar_and_history, teardown);
    g_test_add("/editor/navigation/large-document-jumps", Fixture, NULL, setup_search, large_document_jumps, teardown);
    g_test_add_func("/editor/navigation/instant-document-scroll", instant_document_scroll);
    g_test_add("/search/cycle-and-number", Fixture, NULL, setup_search, search_cycle_and_number, teardown);
    g_test_add("/search/regex-replace-and-undo", Fixture, NULL, setup_search, regex_replace_and_undo, teardown);
    return g_test_run();
}

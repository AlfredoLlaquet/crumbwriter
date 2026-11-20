#include "editor.h"
#include "reveal.h"
#include "typography.h"

typedef struct {
    GtkWidget *window;
    GtkSourceView *view;
    GtkTextBuffer *buffer;
    CwEditor *editor;
    CwTypography *type;
    GtkIMContext *input;
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
    f->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(f->window), 540, 320);
    f->view = GTK_SOURCE_VIEW(gtk_source_view_new());
    f->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(f->view));
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(f->view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(f->view), 5);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(f->view), 16);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(f->view));
    gtk_container_add(GTK_CONTAINER(f->window), scroll);
    f->editor = cw_editor_new(f->view);
    f->type = cw_typography_new(f->view);
    gtk_widget_show_all(f->window);
    gtk_widget_grab_focus(GTK_WIDGET(f->view));
    cw_reveal_configure(GTK_TEXT_VIEW(f->view));
    settle();
}

static void
teardown(Fixture *f, gconstpointer data)
{
    (void)data;
    g_clear_object(&f->input);
    cw_typography_free(f->type);
    cw_editor_free(f->editor);
    gtk_widget_destroy(f->window);
    settle();
}

static void
key(Fixture *f, guint keyval, GdkModifierType state)
{
    GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
    GdkWindow *window = gtk_text_view_get_window(GTK_TEXT_VIEW(f->view), GTK_TEXT_WINDOW_TEXT);
    GdkDisplay *display = gdk_window_get_display(window);
    event->key.window = g_object_ref(window);
    event->key.send_event = TRUE;
    event->key.keyval = keyval;
    event->key.state = state;
    gunichar c = gdk_keyval_to_unicode(keyval);
    if (c && !g_unichar_iscntrl(c)) {
        gchar utf8[7];
        event->key.length = g_unichar_to_utf8(c, utf8);
        event->key.string = g_strndup(utf8, event->key.length);
    }
    GdkKeymapKey *entries = NULL;
    gint count;
    if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(display),
                                          keyval, &entries, &count)) {
        event->key.hardware_keycode = entries[0].keycode;
        event->key.group = entries[0].group;
        g_free(entries);
    }
    gdk_event_set_device(event, gdk_seat_get_keyboard(gdk_display_get_default_seat(display)));
    g_assert_true(gtk_widget_event(GTK_WIDGET(f->view), event));
    gdk_event_free(event);
    settle();
}

static void
assert_text(Fixture *f, const gchar *expected)
{
    GtkTextIter a, b;
    gtk_text_buffer_get_bounds(f->buffer, &a, &b);
    g_autofree gchar *text = gtk_text_buffer_get_text(f->buffer, &a, &b, TRUE);
    g_assert_cmpstr(text, ==, expected);
}

static GtkTextIter
cursor(Fixture *f)
{
    GtkTextIter caret;
    gtk_text_buffer_get_iter_at_mark(f->buffer, &caret, gtk_text_buffer_get_insert(f->buffer));
    return caret;
}

static void
navigation(Fixture *f, gconstpointer unused)
{
    (void)unused;
    const gchar *original = "Book//Note 1: Test: Text.\n";
    cw_editor_load_at(f->editor, original, -1, 0);
    key(f, GDK_KEY_Down, 0);
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 0);
    GtkTextIter eof = cursor(f);
    g_assert_true(gtk_text_iter_is_end(&eof));
    for (guint i = 1; i <= 28; i++) {
        key(f, GDK_KEY_Down, 0);
        g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, i);
    }
    GdkRectangle virtual, visible;
    cw_reveal_virtual_rect(GTK_TEXT_VIEW(f->view), 28, &virtual);
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(f->view), &visible);
    g_assert_cmpint(virtual.y, >=, visible.y);
    g_assert_cmpint(virtual.y + virtual.height, <=, visible.y + visible.height);
    key(f, GDK_KEY_Return, GDK_CONTROL_MASK);
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(f->view), &visible);
    g_assert_cmpint(ABS((2 * virtual.y + virtual.height) - (2 * visible.y + visible.height)), <=, 3);
    key(f, GDK_KEY_KP_Enter, GDK_MOD1_MASK);
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(f->view), &visible);
    g_assert_cmpint(ABS(virtual.y - visible.y), <=, 1);
    key(f, GDK_KEY_Up, 0);
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 27);
    key(f, GDK_KEY_Escape, 0);
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 0);
    g_assert_true(gtk_text_view_get_cursor_visible(GTK_TEXT_VIEW(f->view)));
    g_assert_false(gtk_source_buffer_can_undo(GTK_SOURCE_BUFFER(f->buffer)));
    g_assert_false(gtk_text_buffer_get_modified(f->buffer));
    assert_text(f, original);
}

static void
type_and_undo(Fixture *f, gconstpointer data)
{
    const gchar *original = data;
    cw_editor_load_at(f->editor, original, -1, 0);
    key(f, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
    key(f, GDK_KEY_Down, 0);
    key(f, GDK_KEY_Down, 0);
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 2);
    key(f, GDK_KEY_x, 0);
    g_autofree gchar *expected = g_strconcat(original, "\n\nx", NULL);
    assert_text(f, expected);
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 0);
    GtkTextIter at = cursor(f);
    g_assert_true(gtk_text_iter_is_end(&at));
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, original);
    gtk_source_buffer_redo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, expected);
}

static void
preceding_record(Fixture *f, gconstpointer data)
{
    const gchar *ending = data;
    g_autofree gchar *text = g_strconcat("Note 1: : A\n  Note 2: Last: ", ending, NULL);
    cw_editor_load_at(f->editor, text, -1, 0);
    key(f, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
    key(f, GDK_KEY_Down, 0);
    key(f, GDK_KEY_Down, GDK_CONTROL_MASK);
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 1);
    key(f, GDK_KEY_Up, GDK_CONTROL_MASK);
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 0);
    GtkTextIter at = cursor(f);
    g_assert_cmpint(gtk_text_iter_get_line(&at), ==, 1);
    g_assert_cmpint(gtk_text_iter_get_line_offset(&at), ==, 2);
    key(f, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
    key(f, GDK_KEY_Down, 0);
    key(f, GDK_KEY_ISO_Left_Tab, GDK_SHIFT_MASK);
    at = cursor(f);
    g_assert_cmpint(gtk_text_iter_get_line(&at), ==, 1);
    g_assert_cmpint(gtk_text_iter_get_line_offset(&at), ==, 16);
}

static void
align_retains_selection(Fixture *f, gconstpointer unused)
{
    (void)unused;
    GString *text = g_string_new("Note 1: First: a\n  Note 2: Middle: ");
    for (gint i = 0; i < 620; i++)
        g_string_append(text, "window ");
    g_string_append(text, "\nNote 3: Last: z\n");
    cw_editor_load(f->editor, text->str);
    g_string_free(text, TRUE);
    GtkTextIter caret, bound, header;
    gtk_text_buffer_get_iter_at_line_offset(f->buffer, &caret, 1, 3000);
    gtk_text_buffer_get_iter_at_line_offset(f->buffer, &bound, 1, 2990);
    gtk_text_buffer_get_iter_at_line_offset(f->buffer, &header, 1, 2);
    gtk_text_buffer_select_range(f->buffer, &caret, &bound);
    key(f, GDK_KEY_Return, GDK_CONTROL_MASK);
    GdkRectangle visible, line;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(f->view), &caret, &line);
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(f->view), &visible);
    g_assert_cmpint(ABS((2 * line.y + line.height) - (2 * visible.y + visible.height)), <=, 3);
    key(f, GDK_KEY_Return, GDK_MOD1_MASK);
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(f->view), &header, &line);
    gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(f->view), &visible);
    g_assert_cmpint(ABS(line.y - visible.y), <=, 1);
    GtkTextIter a, b;
    g_assert_true(gtk_text_buffer_get_selection_bounds(f->buffer, &a, &b));
    g_assert_true(gtk_text_iter_equal(&a, &bound));
    g_assert_true(gtk_text_iter_equal(&b, &caret));
    GtkTextIter current = cursor(f);
    g_assert_true(gtk_text_iter_equal(&current, &caret));
}

static gboolean
capture_method(GSignalInvocationHint *hint, guint count, const GValue *values, gpointer data)
{
    (void)count;
    Fixture *f = data;
    GtkIMContext *method = g_value_get_object(values);
    if (g_signal_handler_find(method, G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_DATA,
                              hint->signal_id, 0, NULL, NULL, f->view))
        g_set_object(&f->input, method);
    return TRUE;
}

static gboolean
deliver_commit(gpointer data)
{
    g_signal_emit_by_name(data, "commit", "Café -- oui...");
    return G_SOURCE_REMOVE;
}

static void
ime_and_paste(Fixture *f, gconstpointer data)
{
    (void)data;
    guint signal = g_signal_lookup("commit", GTK_TYPE_IM_CONTEXT);
    gulong hook = g_signal_add_emission_hook(signal, 0, capture_method, f, NULL);
    key(f, GDK_KEY_x, 0);
    g_signal_remove_emission_hook(signal, hook);
    g_assert_nonnull(f->input);
    const gchar *original = "Note 1: : Body.";
    cw_editor_load_at(f->editor, original, -1, 0);
    key(f, GDK_KEY_Down, 0);
    g_idle_add(deliver_commit, f->input);
    settle();
    assert_text(f, "Note 1: : Body.\nCafé — oui...");
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, original);
    key(f, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
    key(f, GDK_KEY_Down, 0);
    key(f, GDK_KEY_Down, 0);
    const gchar *pasted = "Note 2: À: One--two...\nNote 3: : Z. ";
    GtkClipboard *clip = gtk_widget_get_clipboard(GTK_WIDGET(f->view), GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clip, pasted, -1);
    g_signal_emit_by_name(f->view, "paste-clipboard");
    gint64 deadline = g_get_monotonic_time() + 3 * G_TIME_SPAN_SECOND;
    while (cw_editor_virtual_rows(f->editor) && g_get_monotonic_time() < deadline) {
        settle();
        g_usleep(1000);
    }
    g_autofree gchar *expected = g_strconcat(original, "\n\n", pasted, NULL);
    assert_text(f, expected);
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, original);
}

static void
cancel_on_actual_edit(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *original = "Note 1: Test: Original.";
    cw_editor_load_at(f->editor, original, -1, 0);
    key(f, GDK_KEY_Down, 0);
    g_assert_false(cw_editor_delete_crumb(f->editor));
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 0);
    key(f, GDK_KEY_Down, 0);
    g_assert_false(cw_editor_copy_crumb(f->editor));
    assert_text(f, original);
    key(f, GDK_KEY_Down, 0);
    GtkTextIter it;
    gtk_text_buffer_get_start_iter(f->buffer, &it);
    gtk_text_buffer_place_cursor(f->buffer, &it);
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 0);
    key(f, GDK_KEY_Page_Up, GDK_CONTROL_MASK);
    key(f, GDK_KEY_Down, 0);
    cw_editor_append(f->editor);
    assert_text(f, "Note 1: Test: Original.\nNote 2: : ");
    key(f, GDK_KEY_Down, 0);
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    g_assert_cmpuint(cw_editor_virtual_rows(f->editor), ==, 0);
    assert_text(f, original);
}

int
main(int argc, char **argv)
{
    gtk_test_init(&argc, &argv, NULL);
    g_test_add("/virtual/navigation-and-align", Fixture, NULL, setup, navigation, teardown);
    g_test_add("/virtual/type/final-lf", Fixture, "Note 1: : A\n", setup, type_and_undo, teardown);
    g_test_add("/virtual/type/no-final-lf", Fixture, "Note 1: : A", setup, type_and_undo, teardown);
    g_test_add("/virtual/preceding-record/final-lf", Fixture, "\n", setup, preceding_record, teardown);
    g_test_add("/virtual/preceding-record/no-final-lf", Fixture, "", setup, preceding_record, teardown);
    g_test_add("/virtual/real-viewport-selection", Fixture, NULL, setup, align_retains_selection, teardown);
    g_test_add("/virtual/ime-and-paste", Fixture, NULL, setup, ime_and_paste, teardown);
    g_test_add("/virtual/cancel-on-actual-edit", Fixture, NULL, setup, cancel_on_actual_edit, teardown);
    return g_test_run();
}

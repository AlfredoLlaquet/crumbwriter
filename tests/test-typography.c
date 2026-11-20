#include "typography.h"

typedef struct {
    GtkWidget *window;
    GtkSourceView *view;
    GtkSourceBuffer *source;
    GtkTextBuffer *buffer;
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
setup(Fixture *f, gconstpointer unused)
{
    (void)unused;
    f->source = gtk_source_buffer_new(NULL);
    f->buffer = GTK_TEXT_BUFFER(f->source);
    f->view = GTK_SOURCE_VIEW(gtk_source_view_new_with_buffer(f->source));
    f->type = cw_typography_new(f->view);
    f->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_add(GTK_CONTAINER(f->window), GTK_WIDGET(f->view));
    gtk_widget_show_all(f->window);
    gtk_widget_grab_focus(GTK_WIDGET(f->view));
    settle();
}

static void
teardown(Fixture *f, gconstpointer unused)
{
    (void)unused;
    cw_typography_free(f->type);
    g_clear_object(&f->input);
    gtk_widget_destroy(f->window);
    g_object_unref(f->source);
    settle();
}

/* Deliver a complete GTK widget event, including GTK's input method and the
 * event-after phase, as on the desktop. No test-only insertion path. */
static void
send_key(Fixture *f, guint keyval, const gchar *string, gint bytes)
{
    GdkWindow *window = gtk_text_view_get_window(GTK_TEXT_VIEW(f->view), GTK_TEXT_WINDOW_TEXT);
    GdkDisplay *display = gdk_window_get_display(window);
    GdkSeat *seat = gdk_display_get_default_seat(display);
    GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = g_object_ref(window);
    event->key.send_event = TRUE;
    event->key.keyval = keyval;
    event->key.length = bytes;
    event->key.string = g_strndup(string, bytes);
    GdkKeymapKey *entries = NULL;
    gint n = 0;
    if (gdk_keymap_get_entries_for_keyval(gdk_keymap_get_for_display(display),
                                          keyval, &entries, &n)) {
        event->key.hardware_keycode = entries[0].keycode;
        event->key.group = entries[0].group;
        if (entries[0].level == 1)
            event->key.state = GDK_SHIFT_MASK;
        g_free(entries);
    }
    gdk_event_set_device(event, gdk_seat_get_keyboard(seat));
    g_assert_true(gtk_widget_event(GTK_WIDGET(f->view), event));
    gdk_event_free(event);
}

static void
type_text(Fixture *f, const gchar *text)
{
    for (const gchar *p = text; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        guint keyval = c == '\n' ? GDK_KEY_Return : gdk_unicode_to_keyval(c);
        send_key(f, keyval, p, (gint)(g_utf8_next_char(p) - p));
    }
}

static void
assert_text(Fixture *f, const gchar *expected)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(f->buffer, &start, &end);
    gchar *actual = gtk_text_buffer_get_text(f->buffer, &start, &end, TRUE);
    g_assert_cmpstr(actual, ==, expected);
    g_free(actual);
}

/* GTK 3 keeps the view's multicontext private. Capture it from a real key
 * commit so these tests can replay a desktop IM's asynchronous callback on
 * the same object and through GtkTextView's own commit handler. */
static gboolean
capture_input(GSignalInvocationHint *hint, guint count, const GValue *args, gpointer data)
{
    (void)count;
    Fixture *f = data;
    GtkIMContext *input = g_value_get_object(args);
    if (g_signal_handler_find(input, G_SIGNAL_MATCH_ID | G_SIGNAL_MATCH_DATA,
                              hint->signal_id, 0, NULL, NULL, f->view))
        g_set_object(&f->input, input);
    return TRUE;
}

static void
get_input(Fixture *f)
{
    guint signal = g_signal_lookup("commit", GTK_TYPE_IM_CONTEXT);
    gulong hook = g_signal_add_emission_hook(signal, 0, capture_input, f, NULL);
    type_text(f, "x");
    g_signal_remove_emission_hook(signal, hook);
    g_assert_nonnull(f->input);
    gtk_text_buffer_set_text(f->buffer, "", -1);
}

typedef struct {
    GtkIMContext *input;
    const gchar *text;
} Commit;

static gboolean
deliver_commit(gpointer data)
{
    Commit *commit = data;
    g_signal_emit_by_name(commit->input, "commit", commit->text);
    return G_SOURCE_REMOVE;
}

static void
commit(Fixture *f, const gchar *text)
{
    Commit next = { f->input, text };
    g_idle_add(deliver_commit, &next);
    settle();
}

static void
delayed_input(Fixture *f, gconstpointer unused)
{
    (void)unused;
    get_input(f);
    commit(f, "I'");
    assert_text(f, "I'");
    commit(f, "d");
    assert_text(f, "I’d");
    commit(f, " Pepe,\" new--");
    assert_text(f, "I’d Pepe,” new--");
    commit(f, "sí… ");
    assert_text(f, "I’d Pepe,” new—sí… ");
    gtk_source_buffer_undo(f->source);
    assert_text(f, "I’d Pepe,” new--");
    gtk_source_buffer_redo(f->source);
    assert_text(f, "I’d Pepe,” new—sí… ");
    commit(f, "\"Entrée\" ...");
    assert_text(f, "I’d Pepe,” new—sí… “Entrée” ...");
    commit(f, "\n");
    assert_text(f, "I’d Pepe,” new—sí… “Entrée” …\n");
}

static void
selection_and_overwrite(Fixture *f, gconstpointer unused)
{
    (void)unused;
    get_input(f);
    gtk_text_buffer_set_text(f->buffer, "Still old Z", -1);
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_offset(f->buffer, &start, 6);
    gtk_text_buffer_get_iter_at_offset(f->buffer, &end, 9);
    gtk_text_buffer_select_range(f->buffer, &start, &end);
    commit(f, "\"à--b...\" ");
    assert_text(f, "Still “à—b…”  Z");
    gtk_source_buffer_undo(f->source);
    assert_text(f, "Still old Z");
    gtk_text_buffer_set_text(f->buffer, "word XXX", -1);
    gtk_text_buffer_get_iter_at_offset(f->buffer, &start, 5);
    gtk_text_buffer_place_cursor(f->buffer, &start);
    gtk_text_view_set_overwrite(GTK_TEXT_VIEW(f->view), TRUE);
    commit(f, "-");
    commit(f, "-");
    commit(f, "s");
    assert_text(f, "word —s");
    gtk_source_buffer_undo(f->source);
    assert_text(f, "word --X");
}

static void
composition_and_insert_action(Fixture *f, gconstpointer unused)
{
    (void)unused;
    g_object_set(f->view, "im-module", "gtk-im-context-simple", NULL);
    type_text(f, "word--");
    send_key(f, GDK_KEY_dead_acute, "", 0);
    assert_text(f, "word--");
    type_text(f, "e");
    assert_text(f, "word—é");

    g_signal_emit_by_name(f->view, "insert-at-cursor", " \"Act...\" ");
    assert_text(f, "word—é “Act…” ");
    /* The same payload through an ordinary buffer edit stays literal. */
    gtk_text_buffer_insert_interactive_at_cursor(f->buffer, "\"Act...\" ", -1, TRUE);
    assert_text(f, "word—é “Act…” \"Act...\" ");
}

static void
lookahead(Fixture *f, gconstpointer unused)
{
    (void)unused;
    type_text(f, "I'");
    assert_text(f, "I'");
    type_text(f, "d Pepe,\"");
    assert_text(f, "I’d Pepe,\"");
    type_text(f, " new--");
    assert_text(f, "I’d Pepe,” new--");
    type_text(f, "s...");
    assert_text(f, "I’d Pepe,” new—s...");
    type_text(f, " ");
    assert_text(f, "I’d Pepe,” new—s… ");
}

static void
direction(Fixture *f, gconstpointer unused)
{
    (void)unused;
    type_text(f, "\"The dogs' collars are 'blue,'\" said Alex. 'Tis the '90s. 'Tissue' ");
    assert_text(f, "“The dogs’ collars are ‘blue,’” said Alex. ’Tis the ’90s. ‘Tissue’ ");
    type_text(f, "\"unfinished\"");
    cw_typography_finish(f->type);
    assert_text(f, "“The dogs’ collars are ‘blue,’” said Alex. ’Tis the ’90s. ‘Tissue’ “unfinished”");
}

static void
paste_done(GtkTextBuffer *buffer, GtkClipboard *clipboard, gboolean *complete)
{
    (void)buffer;
    (void)clipboard;
    *complete = TRUE;
}

static void
undo_and_literal_edits(Fixture *f, gconstpointer unused)
{
    (void)unused;
    gtk_text_buffer_set_text(f->buffer, "Old \" ' .. ", -1);
    type_text(f, "new--s");
    assert_text(f, "Old \" ' .. new—s");
    gtk_source_buffer_undo(f->source);
    assert_text(f, "Old \" ' .. new--");
    gtk_source_buffer_redo(f->source);
    assert_text(f, "Old \" ' .. new—s");
    GtkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(f->view), GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, " paste--and... \" ", -1);
    gboolean complete = FALSE;
    gulong pasted = g_signal_connect(f->buffer, "paste-done", G_CALLBACK(paste_done), &complete);
    g_signal_emit_by_name(f->view, "paste-clipboard");
    while (!complete)
        g_main_context_iteration(NULL, TRUE);
    g_signal_handler_disconnect(f->buffer, pasted);
    type_text(f, "x ");
    assert_text(f, "Old \" ' .. new—s paste--and... \" x ");

    /* One typed period must not enlist existing punctuation on its left. */
    gtk_text_buffer_set_text(f->buffer, "Unchanged..", -1);
    type_text(f, ". ");
    assert_text(f, "Unchanged... ");
    type_text(f, "--");
    gtk_text_buffer_set_text(f->buffer, "Different--", -1);
    type_text(f, "x");
    assert_text(f, "Different--x");
    GtkSourceSearchSettings *settings = gtk_source_search_settings_new();
    gtk_source_search_settings_set_search_text(settings, "Different--x");
    GtkSourceSearchContext *search = gtk_source_search_context_new(f->source, settings);
    GError *error = NULL;
    g_assert_cmpuint(gtk_source_search_context_replace_all(search, "Literal \"...--", -1, &error), ==, 1);
    g_assert_no_error(error);
    type_text(f, " ");
    assert_text(f, "Literal \"...-- ");
    gtk_source_search_settings_set_search_text(settings, "—");
    g_signal_emit_by_name(f->view, "insert-at-cursor", "é--y");
    GtkTextIter begin, match_start, match_end;
    gtk_text_buffer_get_start_iter(f->buffer, &begin);
    g_assert_true(gtk_source_search_context_forward(search, &begin, &match_start, &match_end, NULL));
    assert_text(f, "Literal \"...-- é—y");
    g_object_unref(search);
    g_object_unref(settings);
}

static void
departure(Fixture *f, gconstpointer unused)
{
    (void)unused;
    type_text(f, "Two--");
    GtkTextIter it;
    gtk_text_buffer_get_start_iter(f->buffer, &it);
    gtk_text_buffer_place_cursor(f->buffer, &it);
    settle();
    assert_text(f, "Two—");
    type_text(f, "A ");
    assert_text(f, "A Two—");
    cw_typography_enable(f->type, FALSE);
    type_text(f, "straight \"...-- ");
    cw_typography_finish(f->type);
    assert_text(f, "A straight \"...-- Two—");
    cw_typography_enable(f->type, TRUE);
    type_text(f, "'Yes' ");
    assert_text(f, "A straight \"...-- ‘Yes’ Two—");
}

int
main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    if (!gtk_init_check(&argc, &argv)) {
        g_test_message("A display is required for the keyboard-input tests.");
        return 77;
    }
    g_test_add("/typography/lookahead", Fixture, NULL, setup, lookahead, teardown);
    g_test_add("/typography/delayed-input", Fixture, NULL, setup, delayed_input, teardown);
    g_test_add("/typography/selection-overwrite", Fixture, NULL, setup, selection_and_overwrite, teardown);
    g_test_add("/typography/composition-action", Fixture, NULL, setup, composition_and_insert_action, teardown);
    g_test_add("/typography/direction", Fixture, NULL, setup, direction, teardown);
    g_test_add("/typography/undo-literal", Fixture, NULL, setup, undo_and_literal_edits, teardown);
    g_test_add("/typography/departure", Fixture, NULL, setup, departure, teardown);
    return g_test_run();
}

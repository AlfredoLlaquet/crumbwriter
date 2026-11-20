#include "editor.h"
#include <string.h>

typedef struct {
    GtkWidget *window;
    GtkSourceView *view;
    GtkTextBuffer *buffer;
    CwEditor *editor;
} Fixture;

static void
setup(Fixture *f, gconstpointer data)
{
    (void)data;
    f->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    f->view = GTK_SOURCE_VIEW(gtk_source_view_new());
    f->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(f->view));
    gtk_container_add(GTK_CONTAINER(f->window), GTK_WIDGET(f->view));
    f->editor = cw_editor_new(f->view);
    gtk_widget_show_all(f->window);
}

static void
teardown(Fixture *f, gconstpointer data)
{
    (void)data;
    cw_editor_free(f->editor);
    gtk_widget_destroy(f->window);
}

static void
assert_text(Fixture *f, const gchar *expected)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(f->buffer, &start, &end);
    g_autofree gchar *text = gtk_text_buffer_get_text(f->buffer, &start, &end, TRUE);
    g_assert_cmpstr(text, ==, expected);
}

static void
assert_cursor(Fixture *f, gint line, gint column)
{
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(f->buffer, &cursor, gtk_text_buffer_get_insert(f->buffer));
    g_assert_cmpint(gtk_text_iter_get_line(&cursor), ==, line);
    g_assert_cmpint(gtk_text_iter_get_line_offset(&cursor), ==, column);
}

/* |...| marks the exact character range that should be bold red. */
static void
assert_warning_markup(Fixture *f, const gchar *expected)
{
    GtkTextTag *tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(f->buffer),
                                                "crumb-number-warning");
    gboolean wanted = FALSE;
    GtkTextIter it;
    gtk_text_buffer_get_start_iter(f->buffer, &it);
    for (const gchar *p = expected; *p; p = g_utf8_next_char(p)) {
        if (*p == '|') {
            wanted = !wanted;
            continue;
        }
        g_assert_false(gtk_text_iter_is_end(&it));
        g_assert_cmpint(gtk_text_iter_get_char(&it), ==, g_utf8_get_char(p));
        g_assert_cmpint(gtk_text_iter_has_tag(&it, tag), ==, wanted);
        gtk_text_iter_forward_char(&it);
    }
    g_assert_false(wanted);
    g_assert_true(gtk_text_iter_is_end(&it));
}

static void
header_digits(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *text = "Títol//  Bocí 001: Á: x\n"
                        "Bocí 010: (Éloge #1): y\n"
                        "Bocí 0: A: z\n"
                        "Bocí 18446744073709551616: B: q\n"
                        "Missing numeric header\n";
    cw_editor_load(f->editor, text);
    assert_warning_markup(f, "Títol//  Bocí 001: Á: x\n"
                             "Bocí |010|: (Éloge #1): y\n"
                             "Bocí |0|: A: z\n"
                             "Bocí |18446744073709551616|: B: q\n"
                             "Missing numeric header\n");
    GtkTextIter it;
    gtk_text_buffer_get_iter_at_line_offset(f->buffer, &it, 1, 5);
    GtkTextTag *header = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(f->buffer),
                                                   "crumb-header");
    g_assert_true(gtk_text_iter_has_tag(&it, header));
    GtkTextTag *tag = gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(f->buffer),
                                                "crumb-number-warning");
    gint weight;
    GdkRGBA *color;
    g_object_get(tag, "weight", &weight, "foreground-rgba", &color, NULL);
    g_assert_cmpint(weight, ==, PANGO_WEIGHT_BOLD);
    g_assert_cmpfloat(color->red, >, color->green);
    g_assert_cmpfloat(color->red, >, color->blue);
    gdk_rgba_free(color);
    g_assert_false(gtk_text_buffer_get_modified(f->buffer));
}

static void
genre_digits(Fixture *f, gconstpointer data)
{
    (void)data;
    const gchar *original = "Bocí 1: Índex (Éloge #01), ECHO #1, echo #003: <&>\n"
                            "Bocí 2: (éLOGE #4), Echo #4: #7\n"
                            "Bocí 3: Éloge #5: x\n"
                            "Bocí 4: Éloge #5: y\n";
    cw_editor_load(f->editor, original);
    assert_warning_markup(f, "Bocí 1: Índex (Éloge #01), ECHO #1, echo #|003|: <&>\n"
                             "Bocí 2: (éLOGE #|4|), Echo #4: #7\n"
                             "Bocí 3: Éloge #5: x\nBocí 4: Éloge #|5|: y\n");

    /* A changed predecessor affects its immediate successor. The next
     * comparison still uses the actual ordinal in that successor. */
    GtkTextIter a, b;
    gtk_text_buffer_get_iter_at_line_offset(f->buffer, &a, 1, g_utf8_strlen("Bocí 2: (éLOGE #", -1));
    b = a;
    gtk_text_iter_forward_char(&b);
    gtk_text_buffer_begin_user_action(f->buffer);
    gtk_text_buffer_delete(f->buffer, &a, &b);
    gtk_text_buffer_insert(f->buffer, &a, "2", 1);
    gtk_text_buffer_end_user_action(f->buffer);
    assert_warning_markup(f, "Bocí 1: Índex (Éloge #01), ECHO #1, echo #|003|: <&>\n"
                             "Bocí 2: (éLOGE #2), Echo #4: #7\n"
                             "Bocí 3: Éloge #|5|: x\nBocí 4: Éloge #|5|: y\n");
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_text(f, original);

    gtk_text_buffer_get_iter_at_line(f->buffer, &a, 1);
    gtk_text_buffer_get_iter_at_line(f->buffer, &b, 2);
    gtk_text_buffer_delete(f->buffer, &a, &b);
    assert_warning_markup(f, "Bocí 1: Índex (Éloge #01), ECHO #1, echo #|003|: <&>\n"
                             "Bocí |3|: Éloge #|5|: x\nBocí |4|: Éloge #|5|: y\n");
    gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
    assert_warning_markup(f, "Bocí 1: Índex (Éloge #01), ECHO #1, echo #|003|: <&>\n"
                             "Bocí 2: (éLOGE #|4|), Echo #4: #7\n"
                             "Bocí 3: Éloge #5: x\nBocí 4: Éloge #|5|: y\n");
}

static void
copy_crumb(Fixture *f, gconstpointer data)
{
    (void)data;
    cw_editor_load_at(f->editor, "Llibre//  Bocí 1: Èpic: Dos.  \nBocí 2: : Tres.\n", 0, 0);
    g_assert_true(cw_editor_copy_crumb(f->editor));
    GtkTextIter a, b;
    g_assert_true(gtk_text_buffer_get_selection_bounds(f->buffer, &a, &b));
    g_autofree gchar *selection = gtk_text_buffer_get_text(f->buffer, &a, &b, TRUE);
    g_assert_cmpstr(selection, ==, "  Bocí 1: Èpic: Dos.  ");
    GtkClipboard *clip = gtk_widget_get_clipboard(GTK_WIDGET(f->view), GDK_SELECTION_CLIPBOARD);
    g_autofree gchar *copied = gtk_clipboard_wait_for_text(clip);
    g_assert_cmpstr(copied, ==, selection);
    g_assert_false(gtk_text_buffer_get_modified(f->buffer));
    gtk_text_buffer_get_end_iter(f->buffer, &a);
    gtk_text_buffer_place_cursor(f->buffer, &a);
    g_assert_false(cw_editor_copy_crumb(f->editor));
}

static void
end_of_file(Fixture *f, gconstpointer data)
{
    (void)data;
    const struct { const gchar *text; gint line, column; } cases[] = {
        { "", 0, 0 }, { "partial É", 0, 9 }, { "Note 1: x: é\npárt", 1, 4 },
        { "Note 1: x: é\npárt\n", 1, 4 }, { "Note 1: x: é\n\n", 1, 0 },
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        cw_editor_load_at(f->editor, cases[i].text, -1, 0);
        assert_cursor(f, cases[i].line, cases[i].column);
        GtkTextIter first;
        gtk_text_buffer_get_start_iter(f->buffer, &first);
        gtk_text_buffer_place_cursor(f->buffer, &first);
        cw_editor_go_to_end(f->editor);
        assert_cursor(f, cases[i].line, cases[i].column);
    }
}

static void
delete_crumb(Fixture *f, gconstpointer data)
{
    (void)data;
    const struct { const gchar *text; gint line; const gchar *after; } cases[] = {
        { "Note 1: A: x\nNote 2: B: y\nNote 3: C: z\n", 1, "Note 1: A: x\nNote 3: C: z\n" },
        { "Title //  Note 1: A: x\nNote 2: B: y\n", 0, "Title //  Note 2: B: y\n" },
        { "Note 1: A: x\nNote 2: B: y\n", 1, "Note 1: A: x\n" },
        { "Note 1: A: x\nNote 2: B: y", 1, "Note 1: A: x" },
        { "Title //  Note 1: A: x\n", 0, "Title //  \n" },
        { "Title //  Note 1: A: x", 0, "Title //  " },
        { "Note 1: A: x\n", 0, "\n" }, { "Note 1: A: x", 0, "" }
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        cw_editor_load_at(f->editor, cases[i].text, cases[i].line, 4);
        GtkTextIter caret, other;
        gtk_text_buffer_get_iter_at_mark(f->buffer, &caret, gtk_text_buffer_get_insert(f->buffer));
        gtk_text_buffer_get_end_iter(f->buffer, &other);
        gtk_text_buffer_select_range(f->buffer, &caret, &other);
        g_assert_true(cw_editor_delete_crumb(f->editor));
        assert_text(f, cases[i].after);
        gtk_source_buffer_undo(GTK_SOURCE_BUFFER(f->buffer));
        assert_text(f, cases[i].text);
    }
    cw_editor_load(f->editor, "Title //  Note 1: A: x\n");
    g_assert_true(cw_editor_delete_crumb(f->editor));
    cw_editor_append(f->editor);
    assert_text(f, "Title //  Note 1: : \n");
    cw_editor_load(f->editor, "a partial header");
    g_assert_false(cw_editor_delete_crumb(f->editor));
    assert_text(f, "a partial header");
}

int
main(int argc, char **argv)
{
    gtk_test_init(&argc, &argv, NULL);
    g_test_add("/numbering/header-digits", Fixture, NULL, setup, header_digits, teardown);
    g_test_add("/numbering/genre-digits-and-propagation", Fixture, NULL, setup, genre_digits, teardown);
    g_test_add("/record/copy", Fixture, NULL, setup, copy_crumb, teardown);
    g_test_add("/record/end", Fixture, NULL, setup, end_of_file, teardown);
    g_test_add("/record/delete", Fixture, NULL, setup, delete_crumb, teardown);
    return g_test_run();
}

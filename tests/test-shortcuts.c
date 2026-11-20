#include "shortcuts.h"

#include <string.h>

typedef struct {
    GtkWindow *editor;
    GtkWidget *manuscript;
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
    f->editor = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    f->manuscript = gtk_text_view_new();
    gtk_container_add(GTK_CONTAINER(f->editor), f->manuscript);
    gtk_widget_show_all(GTK_WIDGET(f->editor));
    settle();
}

static void
teardown(Fixture *f, gconstpointer unused)
{
    (void)unused;
    gtk_widget_destroy(GTK_WIDGET(f->editor));
    settle();
}

static GtkWindow *
reference_for(GtkWindow *parent)
{
    GtkWindow *found = NULL;
    GList *windows = gtk_window_list_toplevels();
    for (GList *p = windows; p; p = p->next) {
        GtkWindow *window = GTK_WINDOW(p->data);
        if (gtk_window_get_transient_for(window) == parent &&
            g_strcmp0(gtk_window_get_role(window), "keyboard-shortcuts") == 0) {
            g_assert_null(found);
            found = window;
        }
    }
    g_list_free(windows);
    return found;
}

static GtkWidget *
find_named(GtkWidget *widget, const gchar *name)
{
    if (g_str_equal(name, gtk_widget_get_name(widget)))
        return widget;
    if (!GTK_IS_CONTAINER(widget))
        return NULL;
    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *result = NULL;
    for (GList *p = children; p && !result; p = p->next)
        result = find_named(p->data, name);
    g_list_free(children);
    return result;
}

static GtkListBoxRow *
result_for(GtkListBox *list, const gchar *description)
{
    for (gint i = 0; ; i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(list, i);
        if (!row)
            return NULL;
        if (gtk_widget_get_child_visible(GTK_WIDGET(row)) &&
            strstr(atk_object_get_name(gtk_widget_get_accessible(GTK_WIDGET(row))), description))
            return row;
    }
}

static gboolean
press(GtkWindow *window, guint keyval, GdkModifierType state)
{
    GdkEventKey event = {.type = GDK_KEY_PRESS, .keyval = keyval, .state = state};
    gboolean handled = FALSE;
    g_signal_emit_by_name(window, "key-press-event", &event, &handled);
    return handled;
}

static void
window_lifecycle(Fixture *f, gconstpointer unused)
{
    (void)unused;
    cw_shortcuts_show(f->editor);
    GtkWindow *first = reference_for(f->editor);
    g_assert_nonnull(first);
    g_assert_false(gtk_window_get_modal(first));
    g_assert_true(gtk_window_get_destroy_with_parent(first));
    g_assert_cmpint(gtk_window_get_type_hint(first), ==, GDK_WINDOW_TYPE_HINT_DIALOG);
    g_assert_true(gtk_window_get_focus(first) == find_named(GTK_WIDGET(first), "cw-shortcuts-filter"));
    gtk_window_present(f->editor);
    gtk_widget_grab_focus(f->manuscript);
    g_assert_true(gtk_window_get_focus(f->editor) == f->manuscript);
    g_assert_true(gtk_widget_get_sensitive(f->manuscript));
    cw_shortcuts_show(f->editor);
    g_assert_true(reference_for(f->editor) == first);

    GtkWindow *other = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));
    gtk_widget_show(GTK_WIDGET(other));
    cw_shortcuts_show(other);
    GtkWindow *second = reference_for(other);
    g_assert_nonnull(second);
    g_assert_true(first != second);
    g_assert_true(press(first, GDK_KEY_Escape, 0));
    settle();
    g_assert_null(reference_for(f->editor));
    g_assert_true(reference_for(other) == second);
    cw_shortcuts_show(f->editor);
    g_assert_nonnull(reference_for(f->editor));
    gpointer child = second;
    g_object_add_weak_pointer(G_OBJECT(second), &child);
    gtk_widget_destroy(GTK_WIDGET(other));
    settle();
    g_assert_null(child);
}

static void
search_reference(Fixture *f, gconstpointer unused)
{
    (void)unused;
    cw_shortcuts_show(f->editor);
    GtkWindow *window = reference_for(f->editor);
    GtkWidget *filter = find_named(GTK_WIDGET(window), "cw-shortcuts-filter");
    GtkListBox *list = GTK_LIST_BOX(find_named(GTK_WIDGET(window), "cw-shortcuts-list"));
    const gchar *groups[] = {
        "Documents", "Editing the manuscript", "Crumb and file navigation", "Side panel lists",
        "Find from the manuscript caret", "While the search panel is open", "Menus and settings",
        "In Keyboard Shortcuts"
    };
    for (guint i = 0; i < G_N_ELEMENTS(groups); i++) {
        GtkListBoxRow *row = result_for(list, groups[i]);
        g_assert_nonnull(row);
        GtkWidget *header = gtk_list_box_row_get_header(row);
        g_assert_true(GTK_IS_LABEL(header));
        g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(header)), ==, groups[i]);
    }
    const struct { const gchar *query, *action; } controls[] = {
        {"Ctrl+Y", "Delete the current crumb"}, {"Ctrl+Q", "copy the current crumb"},
        {"Ctrl+Begin", "start of the current crumb"}, {"Ctrl+End", "end of the current crumb"},
        {"Control+Next", "start of the file, including the work title"},
        {"Ctrl+PageUp", "physical end of the file"}, {"Ctrl+Prior", "physical end of the file"},
        {"Home", "start of the current wrapped visual line"}, {"End", "end of the current wrapped visual line"},
        {"Ctrl+Enter", "Center the current visual line"}, {"Alt+Enter", "current crumb at the top"},
        {"Down", "virtual lines below the end of the file"},
        {"whole-word", "Ctrl+W"}, {"Ctrl+W", "Close the document window"},
        {"Ctrl+G", "Find backward from the manuscript caret"}, {"F1", "Open Keyboard Shortcuts"}
    };
    for (guint i = 0; i < G_N_ELEMENTS(controls); i++) {
        gtk_entry_set_text(GTK_ENTRY(filter), controls[i].query);
        g_assert_nonnull(result_for(list, controls[i].action));
    }
    gtk_entry_set_text(GTK_ENTRY(filter), "Ctrl+Q");
    g_assert_null(result_for(list, "Delete the current crumb"));
    g_signal_emit_by_name(filter, "activate");
    g_assert_true(gtk_window_get_focus(window) == GTK_WIDGET(result_for(list, "copy the current crumb")));
    g_assert_true(press(window, GDK_KEY_f, GDK_CONTROL_MASK));
    g_assert_true(gtk_window_get_focus(window) == filter);
    gtk_entry_set_text(GTK_ENTRY(filter), "no-such-binding");
    g_assert_null(result_for(list, ""));
    g_signal_emit_by_name(filter, "activate");
    g_assert_true(gtk_window_get_focus(window) == filter);
    g_assert_true(press(window, GDK_KEY_Escape, 0));
    g_assert_null(reference_for(f->editor));
}

int
main(int argc, char **argv)
{
    gtk_test_init(&argc, &argv, NULL);
    g_test_add("/shortcuts/reference/window", Fixture, NULL, setup, window_lifecycle, teardown);
    g_test_add("/shortcuts/reference/keys-and-filter", Fixture, NULL, setup, search_reference, teardown);
    return g_test_run();
}

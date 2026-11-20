#include "shortcuts.h"

#include <string.h>

typedef struct {
    const gchar *keys, *action;
} Shortcut;

typedef struct {
    GtkWidget *filter, *list;
    gchar **terms;
} Reference;

static const Shortcut documents[] = {
    {"Ctrl+O", "Open a manuscript"},
    {"Ctrl+Shift+N", "Open a new document window"},
    {"Ctrl+S", "Save the document"},
    {"Ctrl+Shift+S", "Save the document under a different name"},
    {"Ctrl+W", "Close the document window when search is closed"}
};

static const Shortcut editing[] = {
    {"Ctrl+N", "Append a new crumb when search is closed"},
    {"#", "Continue a numbered genre after its name in the title"},
    {"Ctrl+Q", "Select and copy the current crumb"},
    {"Ctrl+Y", "Delete the current crumb"},
    {"Ctrl+Z", "Undo the last edit"},
    {"Ctrl+Shift+Z", "Redo the last undone edit"},
    {"Ctrl+A", "Select all text"},
    {"Ctrl+X", "Cut the selected text"},
    {"Ctrl+C", "Copy the selected text"},
    {"Ctrl+V", "Paste from the clipboard"},
    {"Escape", "Clear the selection at the caret"}
};

static const Shortcut navigation[] = {
    {"Up / Down", "Move one wrapped visual line up or down"},
    {"Down", "Move through virtual lines below the end of the file"},
    {"Home", "Go to the start of the current wrapped visual line"},
    {"End", "Go to the end of the current wrapped visual line"},
    {"Ctrl+Enter", "Center the current visual line vertically"},
    {"Alt+Enter", "Place the current crumb at the top of the viewport"},
    {"Ctrl+Up", "Go to the previous crumb"},
    {"Ctrl+Down", "Go to the next crumb"},
    {"Tab", "Go to the start of the next header, title, or body"},
    {"Shift+Tab", "Go to the start of the previous header, title, or body"},
    {"Ctrl+Home / Ctrl+Begin", "Go to the start of the current crumb"},
    {"Ctrl+End", "Go to the end of the current crumb"},
    {"Ctrl+Page Up / Ctrl+Prior", "Go to the start of the file, including the work title"},
    {"Ctrl+Page Down / Ctrl+Next", "Go to the physical end of the file, including its final newline"}
};

static const Shortcut sidebar[] = {
    {"F6", "Switch focus between the manuscript and visit history"},
    {"F7", "Focus the genre list"},
    {"F9", "Show or hide the side panel"},
    {"Up / Down", "Select an entry in the focused list"},
    {"Enter", "Visit the selected crumb in history"},
    {"Enter", "Append a crumb in the selected genre"}
};

static const Shortcut find[] = {
    {"Ctrl+F", "Find forward from the manuscript caret"},
    {"Ctrl+G", "Find backward from the manuscript caret"},
    {"Ctrl+H", "Open forward Find and Replace"},
    {"F3", "Repeat in the opening direction (forward after Ctrl+F, backward after Ctrl+G)"},
    {"Shift+F3", "Repeat in the opposite direction (backward after Ctrl+F, forward after Ctrl+G)"}
};

static const Shortcut search_panel[] = {
    {"Ctrl+P", "Use plain-text search"},
    {"Ctrl+R", "Use regular-expression search"},
    {"Ctrl+N", "Search by crumb number"},
    {"Ctrl+M", "Toggle case-sensitive matching"},
    {"Ctrl+W", "Toggle whole-word matching"},
    {"Alt+W", "Toggle wraparound at the start or end of the file"},
    {"Alt+Down", "Open recent searches and focus the list"},
    {"Up / Down", "Select a recent search in the open list"},
    {"Enter", "Run the selected recent search"},
    {"Enter", "In the query, continue in the opening direction or jump to the crumb number"},
    {"Shift+Enter", "In the query, continue in the opposite direction"},
    {"Alt+L", "Focus the replacement field when Replace is open"},
    {"Enter", "In the replacement field, replace the selected match"},
    {"Alt+O", "Replace one match when Replace is open"},
    {"Alt+A", "Request Replace All, then confirm it with the same shortcut"},
    {"Escape", "Close search, clear the selection, and return to the manuscript"}
};

static const Shortcut application[] = {
    {"F10", "Open the document menu"},
    {"Ctrl+,", "Open Settings"},
    {"F1", "Open Keyboard Shortcuts"}
};

static const Shortcut help_keys[] = {
    {"Ctrl+F", "Focus the shortcut filter"},
    {"Enter", "From the filter, focus the first matching shortcut"},
    {"Up / Down", "Browse the shortcut list"},
    {"Escape", "Close Keyboard Shortcuts"}
};

static void
free_reference(gpointer data)
{
    Reference *reference = data;
    g_strfreev(reference->terms);
    g_free(reference);
}

/* Index labels and key names together. A typed chord such as Ctrl+PageDown
 * also matches its printed form Ctrl+Page Down. */
static gchar *
fold(const gchar *value)
{
    g_autofree gchar *lower = g_utf8_casefold(value, -1);
    GString *key = g_string_sized_new(strlen(lower));
    for (const gchar *p = lower; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (!g_unichar_isspace(c) && c != '+' && c != '_' && c != '-')
            g_string_append_unichar(key, c);
    }
    g_string_replace(key, "control", "ctrl", 0);
    g_string_replace(key, "repág", "pageup", 0);
    g_string_replace(key, "repag", "pageup", 0);
    g_string_replace(key, "avpág", "pagedown", 0);
    g_string_replace(key, "avpag", "pagedown", 0);
    g_string_replace(key, "avpg", "pagedown", 0);
    return g_string_free(key, FALSE);
}

static gboolean
matches(GtkListBoxRow *row, gpointer data)
{
    Reference *reference = data;
    const gchar *haystack = g_object_get_data(G_OBJECT(row), "cw-shortcut-index");
    for (gchar **term = reference->terms; term && *term; term++) {
        if (**term && !strstr(haystack, *term))
            return FALSE;
    }
    return TRUE;
}

static void
filter_changed(GtkEntry *entry, Reference *reference)
{
    g_strfreev(reference->terms);
    reference->terms = g_strsplit_set(gtk_entry_get_text(entry), " \t\r\n", -1);
    for (gchar **p = reference->terms; *p; p++) {
        gchar *token = fold(*p);
        g_free(*p);
        *p = token;
    }
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(reference->list));
    gtk_list_box_invalidate_headers(GTK_LIST_BOX(reference->list));
}

static void
section_header(GtkListBoxRow *row, GtkListBoxRow *previous, gpointer unused)
{
    (void)unused;
    const gchar *group = g_object_get_data(G_OBJECT(row), "cw-shortcut-group");
    if (previous && g_str_equal(group, g_object_get_data(G_OBJECT(previous), "cw-shortcut-group"))) {
        gtk_list_box_row_set_header(row, NULL);
        return;
    }
    if (gtk_list_box_row_get_header(row))
        return;
    GtkWidget *header = gtk_label_new(group);
    gtk_label_set_xalign(GTK_LABEL(header), 0);
    gtk_widget_set_margin_start(header, 9);
    gtk_widget_set_margin_top(header, 10);
    gtk_widget_set_margin_bottom(header, 3);
    PangoAttrList *attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_weight_new(PANGO_WEIGHT_BOLD));
    gtk_label_set_attributes(GTK_LABEL(header), attributes);
    pango_attr_list_unref(attributes);
    gtk_widget_show(header);
    gtk_list_box_row_set_header(row, header);
}

static void
add_group(Reference *reference, GtkSizeGroup *keys_width, const gchar *group,
          const Shortcut *items, guint count)
{
    for (guint i = 0; i < count; i++) {
        const Shortcut *shortcut = &items[i];
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_start(box, 9);
        gtk_widget_set_margin_end(box, 9);
        gtk_widget_set_margin_top(box, 3);
        gtk_widget_set_margin_bottom(box, 3);

        GtkWidget *keys = gtk_label_new(shortcut->keys);
        gtk_label_set_xalign(GTK_LABEL(keys), 0);
        gtk_label_set_line_wrap(GTK_LABEL(keys), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(keys), 24);
        gtk_size_group_add_widget(keys_width, keys);
        gtk_box_pack_start(GTK_BOX(box), keys, FALSE, FALSE, 0);
        GtkWidget *action = gtk_label_new(shortcut->action);
        gtk_label_set_xalign(GTK_LABEL(action), 0);
        gtk_label_set_line_wrap(GTK_LABEL(action), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(action), 46);
        gtk_box_pack_start(GTK_BOX(box), action, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(row), box);
        g_autofree gchar *index = g_strjoin(" ", group, shortcut->keys, shortcut->action, NULL);
        g_object_set_data_full(G_OBJECT(row), "cw-shortcut-index", fold(index), g_free);
        g_object_set_data(G_OBJECT(row), "cw-shortcut-group", (gpointer)group);
        g_autofree gchar *description = g_strjoin(", ", shortcut->keys, shortcut->action, group, NULL);
        atk_object_set_name(gtk_widget_get_accessible(row), description);
        gtk_container_add(GTK_CONTAINER(reference->list), row);
    }
}

static void
focus_results(GtkEntry *entry, Reference *reference)
{
    (void)entry;
    GtkListBox *list = GTK_LIST_BOX(reference->list);
    for (gint i = 0; ; i++) {
        GtkListBoxRow *row = gtk_list_box_get_row_at_index(list, i);
        if (!row)
            return;
        if (gtk_widget_get_child_visible(GTK_WIDGET(row))) {
            gtk_list_box_select_row(list, row);
            gtk_widget_grab_focus(GTK_WIDGET(row));
            return;
        }
    }
}

static gboolean
help_key(GtkWidget *window, GdkEventKey *event, Reference *reference)
{
    GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
    if (event->keyval == GDK_KEY_Escape && mods == 0) {
        gtk_widget_destroy(window);
        return TRUE;
    }
    if (gdk_keyval_to_lower(event->keyval) == GDK_KEY_f && mods == GDK_CONTROL_MASK) {
        gtk_widget_grab_focus(reference->filter);
        gtk_editable_select_region(GTK_EDITABLE(reference->filter), 0, -1);
        return TRUE;
    }
    return FALSE;
}

static void
forget_window(GtkWidget *window, GtkWindow *parent)
{
    (void)window;
    g_object_set_data(G_OBJECT(parent), "cw-shortcuts-window", NULL);
}

void
cw_shortcuts_show(GtkWindow *parent)
{
    GtkWidget *window = g_object_get_data(G_OBJECT(parent), "cw-shortcuts-window");
    if (window) {
        gtk_window_present(GTK_WINDOW(window));
        return;
    }
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Keyboard Shortcuts — Crumb Writer");
    gtk_window_set_role(GTK_WINDOW(window), "keyboard-shortcuts");
    gtk_window_set_transient_for(GTK_WINDOW(window), parent);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
    gtk_window_set_modal(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 585, 480);
    g_object_set_data(G_OBJECT(parent), "cw-shortcuts-window", window);
    g_signal_connect_object(window, "destroy", G_CALLBACK(forget_window), parent, 0);

    Reference *reference = g_new0(Reference, 1);
    g_object_set_data_full(G_OBJECT(window), "cw-shortcuts-ui", reference, free_reference);
    GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(column), 9);
    gtk_container_add(GTK_CONTAINER(window), column);
    reference->filter = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(reference->filter), "Filter by action or shortcut");
    gtk_widget_set_name(reference->filter, "cw-shortcuts-filter");
    atk_object_set_name(gtk_widget_get_accessible(reference->filter), "Filter keyboard shortcuts");
    gtk_box_pack_start(GTK_BOX(column), reference->filter, FALSE, FALSE, 0);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_pack_start(GTK_BOX(column), scroll, TRUE, TRUE, 0);
    reference->list = gtk_list_box_new();
    gtk_widget_set_name(reference->list, "cw-shortcuts-list");
    atk_object_set_name(gtk_widget_get_accessible(reference->list), "Keyboard shortcuts by context");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(reference->list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(reference->list), FALSE);
    GtkWidget *empty = gtk_label_new("No matching shortcuts");
    gtk_list_box_set_placeholder(GTK_LIST_BOX(reference->list), empty);
    gtk_container_add(GTK_CONTAINER(scroll), reference->list);

    g_autoptr(GtkSizeGroup) width = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    add_group(reference, width, "Documents", documents, G_N_ELEMENTS(documents));
    add_group(reference, width, "Editing the manuscript", editing, G_N_ELEMENTS(editing));
    add_group(reference, width, "Crumb and file navigation", navigation, G_N_ELEMENTS(navigation));
    add_group(reference, width, "Side panel lists", sidebar, G_N_ELEMENTS(sidebar));
    add_group(reference, width, "Find from the manuscript caret", find, G_N_ELEMENTS(find));
    add_group(reference, width, "While the search panel is open", search_panel, G_N_ELEMENTS(search_panel));
    add_group(reference, width, "Menus and settings", application, G_N_ELEMENTS(application));
    add_group(reference, width, "In Keyboard Shortcuts", help_keys, G_N_ELEMENTS(help_keys));
    gtk_list_box_set_filter_func(GTK_LIST_BOX(reference->list), matches, reference, NULL);
    gtk_list_box_set_header_func(GTK_LIST_BOX(reference->list), section_header, NULL, NULL);
    g_signal_connect(reference->filter, "changed", G_CALLBACK(filter_changed), reference);
    g_signal_connect(reference->filter, "activate", G_CALLBACK(focus_results), reference);
    g_signal_connect(window, "key-press-event", G_CALLBACK(help_key), reference);
    gtk_widget_show_all(window);
    gtk_widget_grab_focus(reference->filter);
    gtk_window_present(GTK_WINDOW(window));
}

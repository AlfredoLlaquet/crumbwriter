#include "preferences.h"
#include <errno.h>
#include <glib/gstdio.h>
#include <stdlib.h>

#define GROUP "editor"

void
cw_preferences_load(CwApp *app)
{
    g_autofree gchar *dir = g_build_filename(g_get_user_config_dir(), "crumbwriter", NULL);
    app->preferences = g_key_file_new();
    app->preferences_path = g_build_filename(dir, "settings.ini", NULL);
    g_autoptr(GError) error = NULL;
    if (!g_key_file_load_from_file(app->preferences, app->preferences_path,
                                  G_KEY_FILE_NONE, &error) &&
        !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning("Could not read preferences: %s", error->message);
    g_autofree gchar *uri = g_key_file_get_string(app->preferences, "session", "last-document", NULL);
    if (uri)
        app->last_document = g_filename_from_uri(uri, NULL, NULL);
}

void
cw_app_write_preferences(gpointer data)
{
    CwApp *app = data;
    g_autofree gchar *dir = g_path_get_dirname(app->preferences_path);
    if (g_mkdir_with_parents(dir, 0700) < 0) {
        g_warning("Could not create settings directory: %s", g_strerror(errno));
        return;
    }
    gsize length;
    g_autofree gchar *text = g_key_file_to_data(app->preferences, &length, NULL);
    g_autoptr(GError) error = NULL;
    if (!g_file_set_contents_full(app->preferences_path, text, length,
                                  G_FILE_SET_CONTENTS_CONSISTENT, 0600, &error))
        g_warning("Could not save preferences: %s", error->message);
}

gboolean
cw_preference_enabled(CwApp *app, const gchar *name)
{
    g_autoptr(GError) error = NULL;
    gboolean enabled = g_key_file_get_boolean(app->preferences, GROUP, name, &error);
    return error ? TRUE : enabled;
}

gint
cw_preference_integer(CwApp *app, const gchar *name, gint fallback, gint min, gint max)
{
    g_autoptr(GError) error = NULL;
    gint n = g_key_file_get_integer(app->preferences, GROUP, name, &error);
    return error ? fallback : CLAMP(n, min, max);
}

void
cw_preferences_theme(CwApp *app)
{
    GtkSettings *settings = gtk_settings_get_default();
    g_autofree gchar *theme = g_key_file_get_string(app->preferences, GROUP, "theme", NULL);
    if (g_strcmp0(theme, "light") == 0 || g_strcmp0(theme, "dark") == 0) {
        /* A desktop can select a permanently dark theme by name. Both explicit
         * overrides use GTK's built-in paired variants. */
        g_object_set(settings, "gtk-theme-name", "Adwaita",
                     "gtk-application-prefer-dark-theme", g_str_equal(theme, "dark"), NULL);
    } else {
        gtk_settings_reset_property(settings, "gtk-theme-name");
        gtk_settings_reset_property(settings, "gtk-application-prefer-dark-theme");
    }
}

static gchar *
font_family(CwApp *app)
{
    gchar *name = g_key_file_get_string(app->preferences, GROUP, "font-family", NULL);
    return name ? name : g_strdup("Serif");
}

void
cw_preferences_font(CwApp *app, GtkCssProvider *provider)
{
    g_autofree gchar *family = font_family(app);
    GString *css = g_string_new("textview { font-family: \"");
    for (const gchar *c = family; *c; c++) {
        if (*c == '\\' || *c == '"')
            g_string_append_c(css, '\\');
        g_string_append_c(css, g_ascii_iscntrl(*c) ? ' ' : *c);
    }
    gint size = cw_preference_integer(app, "font-size", 14, 6, 96);
    g_string_append_printf(css, "\"; font-size: %dpt; font-style: normal; font-weight: normal; }", size);
    gtk_css_provider_load_from_data(provider, css->str, -1, NULL);
    g_string_free(css, TRUE);
}

void
cw_preferences_remember_document(CwApp *app, const gchar *path)
{
    /* GTK paths are in the filesystem encoding. A URI also represents names
     * containing bytes that cannot be stored literally in a UTF-8 GKeyFile. */
    if (g_strcmp0(path, app->last_document) == 0)
        return;
    g_autofree gchar *uri = g_filename_to_uri(path, NULL, NULL);
    if (!uri)
        return;
    g_free(app->last_document);
    app->last_document = g_strdup(path);
    g_key_file_set_string(app->preferences, "session", "last-document", uri);
    cw_app_write_preferences(app);
}

static void
preference_changed(CwApp *app)
{
    cw_app_write_preferences(app);
    cw_app_apply_preferences(app);
}

static void
toggle_changed(GtkToggleButton *button, gpointer data)
{
    CwApp *app = data;
    const gchar *name = g_object_get_data(G_OBJECT(button), "key");
    g_key_file_set_boolean(app->preferences, GROUP, name, gtk_toggle_button_get_active(button));
    preference_changed(app);
}

static void
number_changed(GtkSpinButton *spin, gpointer data)
{
    CwApp *app = data;
    const gchar *name = g_object_get_data(G_OBJECT(spin), "key");
    g_key_file_set_integer(app->preferences, GROUP, name, gtk_spin_button_get_value_as_int(spin));
    preference_changed(app);
}

static void
choice_changed(GtkComboBox *combo, gpointer data)
{
    CwApp *app = data;
    const gchar *name = g_object_get_data(G_OBJECT(combo), "key");
    const gchar *id = gtk_combo_box_get_active_id(combo);
    if (!id)
        return;
    g_key_file_set_string(app->preferences, GROUP, name, id);
    if (g_str_equal(name, "theme"))
        cw_preferences_theme(app);
    preference_changed(app);
}

static void
add_row(GtkGrid *grid, gint row, const gchar *label, GtkWidget *widget)
{
    GtkWidget *text = gtk_label_new_with_mnemonic(label);
    gtk_label_set_mnemonic_widget(GTK_LABEL(text), widget);
    gtk_widget_set_halign(text, GTK_ALIGN_START);
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(grid, text, 0, row, 1, 1);
    gtk_grid_attach(grid, widget, 1, row, 1, 1);
}

static GtkWidget *
spin_preference(CwApp *app, const gchar *key, gint def, gint min, gint max)
{
    GtkWidget *spin = gtk_spin_button_new_with_range(min, max, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), cw_preference_integer(app, key, def, min, max));
    g_object_set_data(G_OBJECT(spin), "key", (gpointer)key);
    g_signal_connect(spin, "value-changed", G_CALLBACK(number_changed), app);
    return spin;
}

static void
add_toggle(CwApp *app, GtkGrid *grid, gint row, const gchar *key, const gchar *label)
{
    GtkWidget *button = gtk_check_button_new_with_mnemonic(label);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), cw_preference_enabled(app, key));
    gtk_grid_attach(grid, button, 0, row, 2, 1);
    g_object_set_data(G_OBJECT(button), "key", (gpointer)key);
    g_signal_connect(button, "toggled", G_CALLBACK(toggle_changed), app);
}

static gint
family_order(gconstpointer a, gconstpointer b)
{
    return g_utf8_collate(pango_font_family_get_name(*(PangoFontFamily *const *)a),
                          pango_font_family_get_name(*(PangoFontFamily *const *)b));
}

static gboolean
settings_key(GtkWidget *window, GdkEventKey *event, gpointer data)
{
    (void)data;
    if (event->keyval != GDK_KEY_Escape)
        return FALSE;
    gtk_widget_destroy(window);
    return TRUE;
}

void
cw_preferences_show(CwApp *app, GtkWindow *parent)
{
    if (app->settings_window) {
        gtk_window_present(GTK_WINDOW(app->settings_window));
        return;
    }
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    app->settings_window = window;
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&app->settings_window);
    gtk_window_set_title(GTK_WINDOW(window), "Crumb Writer Settings");
    gtk_window_set_role(GTK_WINDOW(window), "settings");
    gtk_window_set_transient_for(GTK_WINDOW(window), parent);
    gtk_window_set_type_hint(GTK_WINDOW(window), GDK_WINDOW_TYPE_HINT_DIALOG);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(window), TRUE);
    gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
    GtkGrid *grid = GTK_GRID(gtk_grid_new());
    gtk_container_set_border_width(GTK_CONTAINER(grid), 18);
    gtk_grid_set_row_spacing(grid, 10);
    gtk_grid_set_column_spacing(grid, 18);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(grid));

    GtkComboBoxText *families = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(families, "Serif", "Serif");
    gtk_combo_box_text_append(families, "Sans", "Sans");
    PangoFontFamily **available;
    gint count;
    pango_context_list_families(gtk_widget_get_pango_context(window), &available, &count);
    qsort(available, count, sizeof(*available), family_order);
    for (gint i = 0; i < count; i++) {
        const gchar *name = pango_font_family_get_name(available[i]);
        if (g_ascii_strcasecmp(name, "serif") && g_ascii_strcasecmp(name, "sans"))
            gtk_combo_box_text_append(families, name, name);
    }
    g_free(available);
    g_autofree gchar *family = font_family(app);
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(families), family))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(families), "Serif");
    g_object_set_data(G_OBJECT(families), "key", "font-family");
    g_signal_connect(families, "changed", G_CALLBACK(choice_changed), app);
    add_row(grid, 0, "Font _family", GTK_WIDGET(families));
    add_row(grid, 1, "Font _size (points)", spin_preference(app, "font-size", 14, 6, 96));

    GtkComboBoxText *theme = GTK_COMBO_BOX_TEXT(gtk_combo_box_text_new());
    gtk_combo_box_text_append(theme, "desktop", "Follow desktop");
    gtk_combo_box_text_append(theme, "light", "Light");
    gtk_combo_box_text_append(theme, "dark", "Dark");
    g_autofree gchar *choice = g_key_file_get_string(app->preferences, GROUP, "theme", NULL);
    if (!choice || !gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme), choice))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(theme), "desktop");
    g_object_set_data(G_OBJECT(theme), "key", "theme");
    g_signal_connect(theme, "changed", G_CALLBACK(choice_changed), app);
    add_row(grid, 2, "_Appearance", GTK_WIDGET(theme));
    add_toggle(app, grid, 3, "smart-punctuation", "Use smart _punctuation while typing");
    gtk_grid_attach(grid, gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), 0, 4, 2, 1);
    add_toggle(app, grid, 5, "backup", "Make a _backup beside each file before editing");
    add_toggle(app, grid, 6, "live-save", "Save _live while typing");
    add_row(grid, 7, "Save _interval (seconds)", spin_preference(app, "save-delay", 1, 1, 30));
    add_toggle(app, grid, 8, "reopen-last", "_Reopen the last manuscript at startup");
    GtkWidget *done = gtk_button_new_with_mnemonic("_Done");
    gtk_widget_set_halign(done, GTK_ALIGN_END);
    gtk_grid_attach(grid, done, 1, 9, 1, 1);
    g_signal_connect_swapped(done, "clicked", G_CALLBACK(gtk_widget_destroy), window);
    g_signal_connect(window, "key-press-event", G_CALLBACK(settings_key), NULL);
    gtk_widget_show_all(window);
}

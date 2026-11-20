#include "app.h"
#include "draft.h"
#include "preferences.h"
#include "window.h"
#include <gtksourceview/gtksource.h>
#include <locale.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#define APPLICATION_ID "org.thecrumbs.CrumbWriter"

static void
install_styles(void)
{
    const gchar *css =
        ".writer-toolbar { padding: 5px 9px; border-bottom: 1px solid alpha(currentColor,.16); }"
        ".writer-toolbar button { padding: 4px 9px; }"
        ".writer-footer { padding: 5px 12px; border-top: 1px solid alpha(currentColor,.16); }"
        ".writer-notice { padding: 6px 10px; background-color: alpha(@theme_selected_bg_color,.12); }"
        "textview.inactive-manuscript, textview.inactive-manuscript > text {"
        " background-image: none; background-color: #e5e5e5; color: #303030; }"
        ".section-label { font-weight: bold; }"
        ".crumb-number { font-size: 1.6em; font-weight: bold; }"
        ".metric-number { font-size: 1.2em; font-weight: bold; }"
        ".subtle { opacity: .78; }";
    g_autoptr(GtkCssProvider) provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, css, -1, NULL);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

void
cw_app_apply_preferences(CwApp *app)
{
    for (GList *p = gtk_application_get_windows(app->gtk); p; p = p->next) {
        CwWindow *w = g_object_get_data(G_OBJECT(p->data), "cw-window");
        if (w)
            cw_window_apply_preferences(w);
    }
}

static void
startup(GtkApplication *gtk, CwApp *app)
{
#ifdef GDK_WINDOWING_WAYLAND
    /* GTK 3 advertises the program name as its xdg_toplevel app_id. */
    if (GDK_IS_WAYLAND_DISPLAY(gdk_display_get_default()))
        g_set_prgname(APPLICATION_ID);
#endif
    gtk_source_init();
    cw_preferences_load(app);
    cw_preferences_theme(app);
    install_styles();
    cw_window_register_shortcuts(gtk);
    gtk_window_set_default_icon_name(APPLICATION_ID);
}

static void
recover_once(CwApp *app)
{
    if (app->recovered)
        return;
    app->recovered = TRUE;
    g_autoptr(GPtrArray) drafts = cw_draft_list();
    for (guint i = 0; i < drafts->len; i++)
        cw_window_recover(app, g_ptr_array_index(drafts, i));
}

static void
activate(GtkApplication *gtk, CwApp *app)
{
    gboolean cold_start = !app->recovered;
    recover_once(app);
    if (cold_start && app->last_document && cw_preference_enabled(app, "reopen-last")) {
        cw_window_open(app, app->last_document);
        return;
    }
    GtkWindow *current = gtk_application_get_active_window(gtk);
    if (current)
        gtk_window_present(current);
    else
        cw_window_new(app);
}

static void
open_files(GtkApplication *gtk, GFile **files, gint count, const gchar *hint, CwApp *app)
{
    (void)gtk;
    (void)hint;
    recover_once(app);
    for (gint i = 0; i < count; i++) {
        g_autofree gchar *path = g_file_get_path(files[i]);
        cw_window_open(app, path);
    }
}

static gint
local_options(GApplication *application, GVariantDict *options, gpointer data)
{
    (void)application;
    (void)data;
    if (g_variant_dict_contains(options, "version")) {
        g_print("Crumb Writer 1.4\n");
        return 0;
    }
    return -1;
}

int
main(int argc, char **argv)
{
    setlocale(LC_ALL, "");
    g_set_prgname("crumbwriter");
    g_set_application_name("Crumb Writer");
    CwApp app = {0};
    app.gtk = gtk_application_new(APPLICATION_ID, G_APPLICATION_HANDLES_OPEN);
    g_application_add_main_option(G_APPLICATION(app.gtk), "version", 'v', G_OPTION_FLAG_NONE,
                                  G_OPTION_ARG_NONE, "Show the program version", NULL);
    g_application_set_option_context_parameter_string(G_APPLICATION(app.gtk), "[FILE…]");
    g_signal_connect(app.gtk, "handle-local-options", G_CALLBACK(local_options), NULL);
    g_signal_connect(app.gtk, "startup", G_CALLBACK(startup), &app);
    g_signal_connect(app.gtk, "activate", G_CALLBACK(activate), &app);
    g_signal_connect(app.gtk, "open", G_CALLBACK(open_files), &app);
    gint result = g_application_run(G_APPLICATION(app.gtk), argc, argv);
    g_object_unref(app.gtk);
    if (app.preferences) {
        g_key_file_unref(app.preferences);
        gtk_source_finalize();
    }
    g_free(app.preferences_path);
    g_free(app.last_document);
    return result;
}

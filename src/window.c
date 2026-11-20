#include "window.h"
#include "reveal.h"
#include "draft.h"
#include "editor.h"
#include "preferences.h"
#include "search.h"
#include "shortcuts.h"
#include "storage.h"
#include "typography.h"
#include <glib/gstdio.h>

struct CwWindow {
    CwApp *app;
    GtkWidget *window, *sidebar, *menu_button, *status, *save_mode;
    GtkWidget *notice, *notice_text, *notice_save, *notice_discard, *notice_stay;
    GtkWidget *chooser;
    GtkSourceView *view;
    GtkTextBuffer *buffer;
    GtkCssProvider *font;
    CwEditor *editor;
    CwSearch *search;
    CwTypography *typography;
    CwFile *file;
    gchar *draft;
    guint save_timer;
    gint save_delay;
    gboolean live, loading, save_failed, close_pending;
};

static void choose_file(CwWindow *w, gboolean saving);

static void
style(GtkWidget *widget, const gchar *class_name)
{
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), class_name);
}

static void
status(CwWindow *w, const gchar *message)
{
    gtk_label_set_text(GTK_LABEL(w->status), message);
}

static void
show_notice(CwWindow *w, const gchar *message, gboolean closing)
{
    w->close_pending = closing;
    gtk_label_set_text(GTK_LABEL(w->notice_text), message);
    gtk_widget_set_visible(w->notice_save, closing);
    gtk_widget_set_visible(w->notice_discard, closing);
    gtk_button_set_label(GTK_BUTTON(w->notice_stay), closing ? "_Keep Editing" : "_Dismiss");
    gtk_widget_show(w->notice);
}

static void
hide_notice(CwWindow *w)
{
    w->close_pending = FALSE;
    gtk_widget_hide(w->notice);
}

static void
update_title(CwWindow *w)
{
    g_autofree gchar *name = w->file ? g_filename_display_basename(cw_file_path(w->file)) :
                                      g_strdup("Untitled");
    gboolean modified = gtk_text_buffer_get_modified(w->buffer);
    g_autofree gchar *title = g_strdup_printf("%s%s — Crumb Writer", modified ? "• " : "", name);
    gtk_window_set_title(GTK_WINDOW(w->window), title);
}

static void
update_undo_actions(CwWindow *w)
{
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(w->window));
    gboolean document = !focus || !GTK_IS_EDITABLE(focus);
    GActionMap *map = G_ACTION_MAP(w->window);
    GtkSourceBuffer *buffer = GTK_SOURCE_BUFFER(w->buffer);
    g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(map, "undo")),
                                document && gtk_source_buffer_can_undo(buffer));
    g_simple_action_set_enabled(G_SIMPLE_ACTION(g_action_map_lookup_action(map, "redo")),
                                document && gtk_source_buffer_can_redo(buffer));
}

static void
window_focus_changed(GtkWindow *window, GtkWidget *focus, CwWindow *w)
{
    (void)window;
    if (focus && gtk_widget_is_ancestor(focus, cw_search_widget(w->search)))
        cw_editor_leave_virtual(w->editor);
    update_undo_actions(w);
}

static void
update_manuscript_appearance(GtkWindow *window, GParamSpec *property, CwWindow *w)
{
    (void)property;
    GtkStyleContext *context = gtk_widget_get_style_context(GTK_WIDGET(w->view));
    if (gtk_window_is_active(window))
        gtk_style_context_remove_class(context, "inactive-manuscript");
    else
        gtk_style_context_add_class(context, "inactive-manuscript");
}

static gchar *
buffer_text(CwWindow *w)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(w->buffer, &start, &end);
    return gtk_text_buffer_get_text(w->buffer, &start, &end, TRUE);
}

static void
cancel_timer(CwWindow *w)
{
    if (w->save_timer)
        g_source_remove(w->save_timer);
    w->save_timer = 0;
}

static gboolean
save_draft(CwWindow *w, const gchar *text)
{
    g_autoptr(GError) error = NULL;
    if (cw_draft_write(&w->draft, text, &error))
        return TRUE;
    g_autofree gchar *message = g_strdup_printf("Could not save a recovery draft. %s", error->message);
    show_notice(w, message, FALSE);
    status(w, "Save failed");
    return FALSE;
}

static gboolean
save_manuscript(CwWindow *w)
{
    g_autofree gchar *text = buffer_text(w);
    g_autoptr(GError) error = NULL;
    cancel_timer(w);
    if (!cw_file_save(w->file, text, &error)) {
        w->save_failed = TRUE;
        gboolean recovered = save_draft(w, text);
        g_autofree gchar *message = g_strdup_printf("%s  %s Use Save As to keep a separate copy.",
                      error->message, recovered ? "A recovery draft is saved." :
                                                  "The recovery draft also failed to save.");
        show_notice(w, message, FALSE);
        status(w, "Manuscript save failed");
        return FALSE;
    }
    w->save_failed = FALSE;
    cw_draft_remove(&w->draft);
    gtk_text_buffer_set_modified(w->buffer, FALSE);
    hide_notice(w);
    status(w, "Saved");
    return TRUE;
}

static gboolean
save_tick(gpointer data)
{
    CwWindow *w = data;
    w->save_timer = 0;
    if (!gtk_text_buffer_get_modified(w->buffer))
        return G_SOURCE_REMOVE;
    if (w->file && !w->save_failed)
        save_manuscript(w);
    else {
        g_autofree gchar *text = buffer_text(w);
        if (save_draft(w, text))
            status(w, w->file ? "Recovery draft saved · manuscript save paused" :
                                "Draft saved · Ctrl+S to name the file");
    }
    return G_SOURCE_REMOVE;
}

static void
schedule_save(CwWindow *w)
{
    /* Anchor the deadline to the first unsaved edit, even during continuous typing. */
    if (w->live && !w->save_timer)
        w->save_timer = g_timeout_add(w->save_delay * 1000, save_tick, w);
}

static void
buffer_changed(GtkTextBuffer *buffer, CwWindow *w)
{
    (void)buffer;
    if (w->loading)
        return;
    if (w->file)
        cw_preferences_remember_document(w->app, cw_file_path(w->file));
    status(w, w->save_failed ? "Manuscript save paused" : "Unsaved changes");
    schedule_save(w);
}

static void
cursor_used(GtkTextBuffer *buffer, GtkTextIter *position, GtkTextMark *mark, CwWindow *w)
{
    (void)position;
    if (!w->loading && w->file && mark == gtk_text_buffer_get_insert(buffer))
        cw_preferences_remember_document(w->app, cw_file_path(w->file));
}

static void
save_command(CwWindow *w)
{
    cw_typography_finish(w->typography);
    if (!w->file) {
        choose_file(w, TRUE);
        return;
    }
    cw_preferences_remember_document(w->app, cw_file_path(w->file));
    gboolean close_after = w->close_pending;
    if (save_manuscript(w) && close_after)
        gtk_widget_destroy(w->window);
}

static gboolean
request_close(GtkWidget *window, GdkEvent *event, CwWindow *w)
{
    (void)window;
    (void)event;
    cw_typography_finish(w->typography);
    if (!gtk_text_buffer_get_modified(w->buffer)) {
        cw_draft_remove(&w->draft);
        return FALSE;
    }
    if (w->file && w->live && !w->save_failed && save_manuscript(w))
        return FALSE;
    const gchar *question = w->save_failed ?
        "The manuscript could not be saved. Save a separate copy before closing?" :
        "Save this document before closing?";
    show_notice(w, question, TRUE);
    gtk_widget_grab_focus(w->notice_save);
    return TRUE;
}

static void
close_save(GtkButton *button, CwWindow *w)
{
    (void)button;
    if (w->file && w->save_failed)
        choose_file(w, TRUE);
    else
        save_command(w);
}

static void
discard(GtkButton *button, CwWindow *w)
{
    (void)button;
    cw_draft_remove(&w->draft);
    gtk_widget_destroy(w->window);
}

static void
stay(GtkButton *button, CwWindow *w)
{
    (void)button;
    hide_notice(w);
    gtk_widget_grab_focus(GTK_WIDGET(w->view));
}

static gboolean
same_document(const gchar *a, const gchar *b)
{
    if (g_strcmp0(a, b) == 0)
        return TRUE;
    GStatBuf left, right;
    return g_stat(a, &left) == 0 && g_stat(b, &right) == 0 &&
           left.st_dev == right.st_dev && left.st_ino == right.st_ino;
}

static CwWindow *
find_open(CwApp *app, const gchar *path)
{
    for (GList *p = gtk_application_get_windows(app->gtk); p; p = p->next) {
        CwWindow *w = g_object_get_data(G_OBJECT(p->data), "cw-window");
        if (w && w->file && same_document(cw_file_path(w->file), path))
            return w;
    }
    return NULL;
}

static void
file_tooltip(CwWindow *w)
{
    const gchar *backup = cw_file_backup_path(w->file);
    g_autofree gchar *path = g_filename_display_name(cw_file_path(w->file));
    g_autofree gchar *copy = backup ? g_filename_display_name(backup) : NULL;
    g_autofree gchar *tip = copy ? g_strdup_printf("%s\nBackup: %s", path, copy) : g_strdup(path);
    gtk_widget_set_tooltip_text(w->status, tip);
}

static void
save_as(CwWindow *w, const gchar *path)
{
    cw_typography_finish(w->typography);
    CwWindow *existing = find_open(w->app, path);
    if (existing) {
        if (existing == w)
            save_command(w);
        else
            show_notice(w, "That manuscript is open in another window. Choose a different filename.", FALSE);
        return;
    }
    g_autofree gchar *text = buffer_text(w);
    g_autoptr(GError) error = NULL;
    CwFile *file = cw_file_save_as_like(w->file, path, text,
                                       cw_preference_enabled(w->app, "backup"), &error);
    if (!file) {
        show_notice(w, error->message, FALSE);
        return;
    }
    gboolean close_after = w->close_pending;
    cancel_timer(w);
    cw_file_free(w->file);
    w->file = file;
    w->save_failed = FALSE;
    cw_preferences_remember_document(w->app, cw_file_path(w->file));
    cw_draft_remove(&w->draft);
    gtk_text_buffer_set_modified(w->buffer, FALSE);
    update_title(w);
    file_tooltip(w);
    hide_notice(w);
    status(w, "Saved");
    if (close_after)
        gtk_widget_destroy(w->window);
}

static void
chooser_response(GtkDialog *dialog, gint response, CwWindow *w)
{
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    gboolean saving = gtk_file_chooser_get_action(chooser) == GTK_FILE_CHOOSER_ACTION_SAVE;
    g_autofree gchar *path = response == GTK_RESPONSE_ACCEPT ? gtk_file_chooser_get_filename(chooser) : NULL;
    gtk_widget_destroy(GTK_WIDGET(dialog));
    if (!path)
        return;
    if (saving)
        save_as(w, path);
    else
        cw_window_open(w->app, path);
}

static void
choose_file(CwWindow *w, gboolean saving)
{
    if (w->chooser) {
        gtk_window_present(GTK_WINDOW(w->chooser));
        return;
    }
    GtkWidget *dialog = gtk_file_chooser_dialog_new(saving ? "Save Manuscript As" : "Open Manuscript",
        GTK_WINDOW(w->window), saving ? GTK_FILE_CHOOSER_ACTION_SAVE : GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL, saving ? "_Save" : "_Open", GTK_RESPONSE_ACCEPT, NULL);
    w->chooser = dialog;
    g_signal_connect(dialog, "destroy", G_CALLBACK(gtk_widget_destroyed), &w->chooser);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(dialog);
    gtk_file_chooser_set_local_only(chooser, TRUE);
    if (w->file) {
        g_autofree gchar *dir = g_path_get_dirname(cw_file_path(w->file));
        gtk_file_chooser_set_current_folder(chooser, dir);
    }
    if (saving) {
        gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);
        g_autofree gchar *name = w->file ? g_filename_display_basename(cw_file_path(w->file)) :
                                          g_strdup("Crumbs.txt");
        gtk_file_chooser_set_current_name(chooser, name);
    }
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    g_signal_connect(dialog, "response", G_CALLBACK(chooser_response), w);
    gtk_widget_show(dialog);
}

static void cmd_open(CwWindow *w) { choose_file(w, FALSE); }
static void cmd_save_as(CwWindow *w) { choose_file(w, TRUE); }
static void cmd_window(CwWindow *w) { cw_window_new(w->app); }
static void cmd_close(CwWindow *w) { gtk_window_close(GTK_WINDOW(w->window)); }
static void cmd_find(CwWindow *w) { cw_search_find(w->search, FALSE); }
static void cmd_find_backward(CwWindow *w) { cw_search_find(w->search, TRUE); }
static void cmd_replace(CwWindow *w) { cw_search_show(w->search, TRUE); }
static void cmd_next(CwWindow *w) { cw_search_step(w->search, FALSE); }
static void cmd_previous(CwWindow *w) { cw_search_step(w->search, TRUE); }
static void cmd_number(CwWindow *w) { cw_search_number(w->search); }
static void cmd_settings(CwWindow *w) { cw_preferences_show(w->app, GTK_WINDOW(w->window)); }
static void cmd_shortcuts(CwWindow *w) { cw_shortcuts_show(GTK_WINDOW(w->window)); }
static void cmd_menu(CwWindow *w) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w->menu_button), TRUE); }

static void
cmd_append(CwWindow *w)
{
    cw_typography_finish(w->typography);
    cw_editor_append(w->editor);
}

static void
cmd_copy_crumb(CwWindow *w)
{
    cw_typography_finish(w->typography);
    cw_editor_copy_crumb(w->editor);
}

static void
cmd_delete_crumb(CwWindow *w)
{
    cw_typography_finish(w->typography);
    cw_editor_delete_crumb(w->editor);
}

static gboolean
window_key(GtkWidget *window, GdkEventKey *event, CwWindow *w)
{
    (void)window;
    if (cw_search_handle_key(w->search, event))
        return TRUE;
    if (event->keyval == GDK_KEY_Escape &&
        !(event->state & gtk_accelerator_get_default_mod_mask()) &&
        gtk_text_buffer_get_has_selection(w->buffer)) {
        GtkTextIter cursor;
        gtk_text_buffer_get_iter_at_mark(w->buffer, &cursor, gtk_text_buffer_get_insert(w->buffer));
        gtk_text_buffer_place_cursor(w->buffer, &cursor);
        return TRUE;
    }
    return FALSE;
}

static void
cmd_undo(CwWindow *w)
{
    GtkSourceBuffer *buffer = GTK_SOURCE_BUFFER(w->buffer);
    if (gtk_source_buffer_can_undo(buffer))
        gtk_source_buffer_undo(buffer);
    cw_reveal_cursor(GTK_TEXT_VIEW(w->view));
}

static void
cmd_redo(CwWindow *w)
{
    GtkSourceBuffer *buffer = GTK_SOURCE_BUFFER(w->buffer);
    if (gtk_source_buffer_can_redo(buffer))
        gtk_source_buffer_redo(buffer);
    cw_reveal_cursor(GTK_TEXT_VIEW(w->view));
}

static void
cmd_sidebar(CwWindow *w)
{
    gtk_widget_set_visible(w->sidebar, !gtk_widget_get_visible(w->sidebar));
    gtk_widget_grab_focus(GTK_WIDGET(w->view));
}

static void
cmd_focus(CwWindow *w)
{
    if (gtk_widget_has_focus(GTK_WIDGET(w->view))) {
        gtk_widget_show(w->sidebar);
        cw_editor_focus_history(w->editor);
    } else
        gtk_widget_grab_focus(GTK_WIDGET(w->view));
}

static void
cmd_genres(CwWindow *w)
{
    gtk_widget_show(w->sidebar);
    cw_editor_focus_genres(w->editor);
}

typedef struct {
    const gchar *name, *shortcut;
    void (*run)(CwWindow *);
} Command;

static const Command commands[] = {
    {"open", "<Primary>o", cmd_open},             {"save", "<Primary>s", save_command},
    {"save-as", "<Primary><Shift>s", cmd_save_as},{"new-window", "<Primary><Shift>n", cmd_window},
    {"new-crumb", "<Primary>n", cmd_append},      {"close", "<Primary>w", cmd_close},
    {"copy-crumb", "<Primary>q", cmd_copy_crumb}, {"delete-crumb", "<Primary>y", cmd_delete_crumb},
    {"find", "<Primary>f", cmd_find},             {"replace", "<Primary>h", cmd_replace},
    {"find-next", "F3", cmd_next},                {"find-previous", "<Shift>F3", cmd_previous},
    {"find-backward", "<Primary>g", cmd_find_backward}, {"go-to", NULL, cmd_number},
    {"settings", "<Primary>comma", cmd_settings},
    {"history", "F6", cmd_focus},                 {"genres", "F7", cmd_genres},
    {"sidebar", "F9", cmd_sidebar},               {"shortcuts", "F1", cmd_shortcuts},
    {"menu", "F10", cmd_menu},                    {"undo", "<Primary>z", cmd_undo},
    {"redo", "<Primary><Shift>z", cmd_redo}
};

void
cw_window_register_shortcuts(GtkApplication *app)
{
    for (guint i = 0; i < G_N_ELEMENTS(commands); i++) {
        g_autofree gchar *name = g_strconcat("win.", commands[i].name, NULL);
        const gchar *accels[] = {commands[i].shortcut, NULL};
        gtk_application_set_accels_for_action(app, name, accels);
    }
}

static void
activate_command(GSimpleAction *action, GVariant *parameter, CwWindow *w)
{
    (void)parameter;
    const Command *command = g_object_get_data(G_OBJECT(action), "command");
    command->run(w);
}

static void
install_commands(CwWindow *w)
{
    for (guint i = 0; i < G_N_ELEMENTS(commands); i++) {
        g_autoptr(GSimpleAction) action = g_simple_action_new(commands[i].name, NULL);
        g_object_set_data(G_OBJECT(action), "command", (gpointer)&commands[i]);
        g_signal_connect(action, "activate", G_CALLBACK(activate_command), w);
        g_action_map_add_action(G_ACTION_MAP(w->window), G_ACTION(action));
    }
}

static GMenu *
window_menu(void)
{
    GMenu *menu = g_menu_new();
    g_autoptr(GMenu) file = g_menu_new();
    g_menu_append(file, "New Crumb", "win.new-crumb");
    g_menu_append(file, "New Document", "win.new-window");
    g_menu_append(file, "Open…", "win.open");
    g_menu_append(file, "Save", "win.save");
    g_menu_append(file, "Save As…", "win.save-as");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(file));
    g_autoptr(GMenu) edit = g_menu_new();
    g_menu_append(edit, "Undo", "win.undo");
    g_menu_append(edit, "Redo", "win.redo");
    g_menu_append(edit, "Select and Copy Current Crumb", "win.copy-crumb");
    g_menu_append(edit, "Delete Current Crumb", "win.delete-crumb");
    g_menu_append(edit, "Find Forward…", "win.find");
    g_menu_append(edit, "Find Backward…", "win.find-backward");
    g_menu_append(edit, "Replace…", "win.replace");
    g_menu_append(edit, "Go to Crumb…", "win.go-to");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(edit));
    g_autoptr(GMenu) view = g_menu_new();
    g_menu_append(view, "Focus Visit History", "win.history");
    g_menu_append(view, "Choose a Genre", "win.genres");
    g_menu_append(view, "Show or Hide Sidebar", "win.sidebar");
    g_menu_append(view, "Settings…", "win.settings");
    g_menu_append(view, "Keyboard Shortcuts…", "win.shortcuts");
    g_menu_append(view, "Close Window", "win.close");
    g_menu_append_section(menu, NULL, G_MENU_MODEL(view));
    return menu;
}

static GtkWidget *
action_button(const gchar *label, const gchar *icon, const gchar *action, const gchar *tip)
{
    GtkWidget *button = gtk_button_new_with_label(label);
    gtk_button_set_image(GTK_BUTTON(button), gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
    gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
    gtk_actionable_set_action_name(GTK_ACTIONABLE(button), action);
    gtk_widget_set_tooltip_text(button, tip);
    return button;
}

static GtkWidget *
make_toolbar(CwWindow *w)
{
    GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    style(bar, "writer-toolbar");
    gtk_box_pack_start(GTK_BOX(bar), action_button("Open", "document-open-symbolic",
                       "win.open", "Open a manuscript (Ctrl+O)"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bar), action_button("New Crumb", "list-add-symbolic",
                       "win.new-crumb", "Append a crumb (Ctrl+N)"), FALSE, FALSE, 0);
    w->menu_button = gtk_menu_button_new();
    gtk_button_set_image(GTK_BUTTON(w->menu_button),
                         gtk_image_new_from_icon_name("open-menu-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_relief(GTK_BUTTON(w->menu_button), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(w->menu_button, "Document menu (F10) · Keyboard shortcuts (F1)");
    atk_object_set_name(gtk_widget_get_accessible(w->menu_button), "Document menu");
    g_autoptr(GMenu) menu = window_menu();
    gtk_menu_button_set_menu_model(GTK_MENU_BUTTON(w->menu_button), G_MENU_MODEL(menu));
    gtk_box_pack_end(GTK_BOX(bar), w->menu_button, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(bar), action_button("Find", "edit-find-symbolic",
                     "win.find", "Find forward (Ctrl+F) or backward (Ctrl+G)"), FALSE, FALSE, 0);
    return bar;
}

static GtkWidget *
make_notice(CwWindow *w)
{
    w->notice = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 7);
    style(w->notice, "writer-notice");
    w->notice_text = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(w->notice_text), 0);
    gtk_label_set_line_wrap(GTK_LABEL(w->notice_text), TRUE);
    gtk_box_pack_start(GTK_BOX(w->notice), w->notice_text, TRUE, TRUE, 0);
    w->notice_save = gtk_button_new_with_mnemonic("_Save and Close");
    w->notice_discard = gtk_button_new_with_mnemonic("Dis_card");
    w->notice_stay = gtk_button_new_with_mnemonic("_Keep Editing");
    gtk_box_pack_start(GTK_BOX(w->notice), w->notice_save, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(w->notice), w->notice_discard, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(w->notice), w->notice_stay, FALSE, FALSE, 0);
    g_signal_connect(w->notice_save, "clicked", G_CALLBACK(close_save), w);
    g_signal_connect(w->notice_discard, "clicked", G_CALLBACK(discard), w);
    g_signal_connect(w->notice_stay, "clicked", G_CALLBACK(stay), w);
    return w->notice;
}

static void
window_destroyed(GtkWidget *window, CwWindow *w)
{
    cancel_timer(w);
    cw_app_write_preferences(w->app);
    if (w->chooser)
        gtk_widget_destroy(w->chooser);
    g_object_set_data(G_OBJECT(window), "cw-window", NULL);
    g_signal_handlers_disconnect_by_data(window, w);
    g_signal_handlers_disconnect_by_data(w->buffer, w);
    cw_typography_free(w->typography);
    cw_search_free(w->search);
    cw_editor_free(w->editor);
    cw_file_free(w->file);
    g_object_unref(w->font);
    g_free(w->draft);
    g_free(w);
}

void
cw_window_apply_preferences(CwWindow *w)
{
    cw_preferences_font(w->app, w->font);
    cw_typography_enable(w->typography, cw_preference_enabled(w->app, "smart-punctuation"));
    w->live = cw_preference_enabled(w->app, "live-save");
    w->save_delay = cw_preference_integer(w->app, "save-delay", 1, 1, 30);
    g_autofree gchar *mode = w->live ? g_strdup_printf("Live save · %d s", w->save_delay) :
                                     g_strdup("Manual save");
    gtk_label_set_text(GTK_LABEL(w->save_mode), mode);
    cancel_timer(w);
    if (gtk_text_buffer_get_modified(w->buffer))
        schedule_save(w);
}

static void
set_initial_size(GtkWindow *window)
{
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(window));
    gint width = 1060, height = 720;
    /* KWin can maximize a new client whose request fills a work area. The
     * compositor chooses the output, so leave frame room on every candidate. */
    for (gint i = 0; i < gdk_display_get_n_monitors(display); i++) {
        GdkRectangle area;
        gdk_monitor_get_workarea(gdk_display_get_monitor(display, i), &area);
        if (area.width > 0 && area.height > 0) {
            width = MIN(width, area.width * 4 / 5);
            height = MIN(height, area.height * 4 / 5);
        }
    }
    gtk_window_set_default_size(window, width, height);
}

static CwWindow *
build_window(CwApp *app)
{
    CwWindow *w = g_new0(CwWindow, 1);
    w->app = app;
    w->window = gtk_application_window_new(app->gtk);
    gtk_window_set_role(GTK_WINDOW(w->window), "manuscript");
    set_initial_size(GTK_WINDOW(w->window));
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(w->window), FALSE);
    g_object_set_data(G_OBJECT(w->window), "cw-window", w);
    install_commands(w);
    GtkWidget *root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(w->window), root);
    gtk_box_pack_start(GTK_BOX(root), make_toolbar(w), FALSE, FALSE, 0);

    w->view = GTK_SOURCE_VIEW(gtk_source_view_new());
    w->buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(w->view));
    gtk_source_buffer_set_highlight_matching_brackets(GTK_SOURCE_BUFFER(w->buffer), FALSE);
    gtk_source_buffer_set_max_undo_levels(GTK_SOURCE_BUFFER(w->buffer), 1000);
    gtk_source_view_set_indent_on_tab(w->view, FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(w->view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(w->view), GTK_WRAP_WORD_CHAR);
    gtk_text_view_set_left_margin(GTK_TEXT_VIEW(w->view), 24);
    gtk_text_view_set_right_margin(GTK_TEXT_VIEW(w->view), 24);
    gtk_text_view_set_top_margin(GTK_TEXT_VIEW(w->view), 18);
    gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(w->view), 18);
    gtk_text_view_set_pixels_below_lines(GTK_TEXT_VIEW(w->view), 6);
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(w->view)), "Manuscript");
    w->font = gtk_css_provider_new();
    gtk_style_context_add_provider(gtk_widget_get_style_context(GTK_WIDGET(w->view)),
             GTK_STYLE_PROVIDER(w->font), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
    w->editor = cw_editor_new(w->view);
    w->search = cw_search_new(w->view, app->preferences, cw_app_write_preferences, app);
    w->typography = cw_typography_new(w->view);
    gtk_box_pack_start(GTK_BOX(root), cw_search_widget(w->search), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), make_notice(w), FALSE, FALSE, 0);
    GtkWidget *panes = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), GTK_WIDGET(w->view));
    cw_reveal_configure(GTK_TEXT_VIEW(w->view));
    /* Keep all sidebar controls reachable in a shallow tile without imposing
     * their combined minimum height on the manuscript's top-level window. */
    w->sidebar = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(w->sidebar), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_propagate_natural_width(GTK_SCROLLED_WINDOW(w->sidebar), TRUE);
    gtk_container_add(GTK_CONTAINER(w->sidebar), cw_editor_sidebar(w->editor));
    gtk_paned_pack1(GTK_PANED(panes), scroll, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(panes), w->sidebar, FALSE, TRUE);
    gtk_box_pack_start(GTK_BOX(root), panes, TRUE, TRUE, 0);
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    style(footer, "writer-footer");
    w->status = gtk_label_new("Untitled · F1 for keyboard shortcuts");
    w->save_mode = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(w->status), 0);
    gtk_label_set_ellipsize(GTK_LABEL(w->status), PANGO_ELLIPSIZE_END);
    style(w->save_mode, "subtle");
    gtk_box_pack_start(GTK_BOX(footer), w->status, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(footer), w->save_mode, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(root), footer, FALSE, FALSE, 0);

    cw_editor_load(w->editor, "");
    cw_window_apply_preferences(w);
    update_title(w);
    update_undo_actions(w);
    g_signal_connect(w->buffer, "changed", G_CALLBACK(buffer_changed), w);
    g_signal_connect(w->buffer, "mark-set", G_CALLBACK(cursor_used), w);
    g_signal_connect_swapped(w->buffer, "modified-changed", G_CALLBACK(update_title), w);
    g_signal_connect_swapped(w->buffer, "notify::can-undo", G_CALLBACK(update_undo_actions), w);
    g_signal_connect_swapped(w->buffer, "notify::can-redo", G_CALLBACK(update_undo_actions), w);
    g_signal_connect_after(w->window, "set-focus", G_CALLBACK(window_focus_changed), w);
    g_signal_connect(w->window, "notify::is-active", G_CALLBACK(update_manuscript_appearance), w);
    update_manuscript_appearance(GTK_WINDOW(w->window), NULL, w);
    g_signal_connect(w->window, "key-press-event", G_CALLBACK(window_key), w);
    g_signal_connect(w->window, "delete-event", G_CALLBACK(request_close), w);
    g_signal_connect(w->window, "destroy", G_CALLBACK(window_destroyed), w);
    gtk_widget_show_all(root);
    gtk_widget_hide(w->notice);
    cw_search_hide(w->search);
    return w;
}

static void
show_manuscript(CwWindow *w)
{
    gtk_widget_grab_focus(GTK_WIDGET(w->view));
    if (gtk_widget_get_visible(w->window))
        gtk_window_present(GTK_WINDOW(w->window));
    else
        gtk_widget_show(w->window);
    cw_reveal_cursor(GTK_TEXT_VIEW(w->view));
}

CwWindow *
cw_window_new(CwApp *app)
{
    CwWindow *w = build_window(app);
    show_manuscript(w);
    return w;
}

static CwWindow *
empty_or_new_window(CwApp *app)
{
    GtkWindow *active = gtk_application_get_active_window(app->gtk);
    CwWindow *w = active ? g_object_get_data(G_OBJECT(active), "cw-window") : NULL;
    if (w && !w->file && !w->draft && !gtk_text_buffer_get_modified(w->buffer) &&
        gtk_text_buffer_get_char_count(w->buffer) == 0)
        return w;
    return build_window(app);
}

void
cw_window_open(CwApp *app, const gchar *path)
{
    CwWindow *existing = path ? find_open(app, path) : NULL;
    if (existing) {
        cw_preferences_remember_document(app, cw_file_path(existing->file));
        cw_editor_go_to_end(existing->editor);
        show_manuscript(existing);
        return;
    }
    CwWindow *w = empty_or_new_window(app);
    if (!path) {
        show_notice(w, "Choose a local manuscript file.", FALSE);
        show_manuscript(w);
        return;
    }
    g_autoptr(GError) error = NULL;
    g_autofree gchar *text = NULL;
    CwFile *file = cw_file_open(path, cw_preference_enabled(app, "backup"), &text, &error);
    if (!file) {
        show_notice(w, error->message, FALSE);
        show_manuscript(w);
        return;
    }
    w->loading = TRUE;
    w->file = file;
    cw_search_reset(w->search);
    cw_editor_load_at(w->editor, text, -1, 0);
    gtk_text_buffer_set_modified(w->buffer, FALSE);
    w->loading = FALSE;
    cw_preferences_remember_document(app, cw_file_path(file));
    hide_notice(w);
    update_title(w);
    file_tooltip(w);
    status(w, cw_file_backup_path(file) ? "Opened · backup created" : "Opened");
    show_manuscript(w);
}

void
cw_window_recover(CwApp *app, const gchar *draft_path)
{
    g_autofree gchar *text = NULL;
    gsize length;
    if (!g_file_get_contents(draft_path, &text, &length, NULL) || !g_utf8_validate(text, length, NULL)) {
        g_warning("Could not read recovery draft %s", draft_path);
        return;
    }
    CwWindow *w = build_window(app);
    w->draft = g_strdup(draft_path);
    w->loading = TRUE;
    cw_editor_load_at(w->editor, text, -1, 0);
    gtk_text_buffer_set_modified(w->buffer, TRUE);
    w->loading = FALSE;
    show_notice(w, "Recovered an unsaved manuscript. Ctrl+S saves it under a filename.", FALSE);
    status(w, "Recovered draft");
    show_manuscript(w);
}

#include "search.h"
#include "reveal.h"
#include "crumb.h"

enum { TEXT_MODE, REGEX_MODE, NUMBER_MODE };
enum { MODE_MASK = 3, MATCH_CASE = 4, WHOLE_WORD = 8, WRAP = 16 };
enum { COL_QUERY, COL_OPTIONS, COL_DESCRIPTION, N_COLUMNS };

struct CwSearch {
    GtkSourceView *view;
    GtkTextBuffer *buffer;
    GtkSourceSearchSettings *settings;
    GtkSourceSearchContext *context;
    GKeyFile *prefs;
    void (*prefs_changed)(gpointer);
    gpointer user_data;
    GtkWidget *panel, *label, *stack, *find, *number, *mode;
    GtkWidget *previous, *next, *match_case, *whole_word, *wrap;
    GtkWidget *replace_row, *replacement, *replace_one, *replace_all;
    GtkWidget *recent, *recent_popover, *recent_pages, *status;
    GtkListStore *history;
    gchar *saved_history;
    gint pending_direction;
    gint last_text_mode;
    gboolean show_replace, confirm_all, updating, replacing, wrapped;
    gchar *notice;
};

static gint
mode(CwSearch *s)
{
    return gtk_combo_box_get_active(GTK_COMBO_BOX(s->mode));
}

static GtkEntry *
active_entry(CwSearch *s)
{
    return GTK_ENTRY(mode(s) == NUMBER_MODE ? s->number : s->find);
}

static guint
options(CwSearch *s)
{
    guint result = (guint)mode(s);
    if (result == NUMBER_MODE)
        return result;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->match_case)))
        result |= MATCH_CASE;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->whole_word)))
        result |= WHOLE_WORD;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->wrap)))
        result |= WRAP;
    return result;
}

static void
show_status(CwSearch *s)
{
    if (s->updating || s->replacing || !gtk_widget_get_visible(s->panel))
        return;

    g_autofree gchar *summary = NULL;
    g_autoptr(GError) error = gtk_source_search_context_get_regex_error(s->context);
    const gchar *message = s->notice;
    gboolean text_mode = mode(s) != NUMBER_MODE;
    gint count = gtk_source_search_context_get_occurrences_count(s->context);

    if (text_mode && error)
        message = error->message;
    if (!message && text_mode && *gtk_entry_get_text(GTK_ENTRY(s->find))) {
        if (count < 0)
            message = "Searching…";
        else if (count == 0)
            message = "No matches";
        else {
            GtkTextIter start, end;
            gint position = 0;
            /* GtkSourceView counts previous matches one by one to determine an ordinal. */
            if (count <= 2000 && gtk_text_buffer_get_selection_bounds(s->buffer, &start, &end))
                position = gtk_source_search_context_get_occurrence_position(s->context, &start, &end);
            if (position > 0)
                summary = g_strdup_printf("%d of %d%s", position, count,
                                          s->wrapped ? " · wrapped" : "");
            else
                summary = g_strdup_printf("%d %s%s", count, count == 1 ? "match" : "matches",
                                          s->wrapped ? " · wrapped" : "");
            message = summary;
        }
    }
    gtk_label_set_text(GTK_LABEL(s->status), message ? message : "");
    gtk_widget_set_tooltip_text(s->status, message);
    gboolean editable = gtk_text_view_get_editable(GTK_TEXT_VIEW(s->view));
    gboolean can_replace = text_mode && !error && count != 0 && editable;
    gtk_widget_set_sensitive(s->replace_one, can_replace);
    gtk_widget_set_sensitive(s->replace_all, can_replace && count > 0);
}

static void
clear_feedback(CwSearch *s)
{
    g_clear_pointer(&s->notice, g_free);
    s->wrapped = FALSE;
    if (s->confirm_all) {
        s->confirm_all = FALSE;
        gtk_button_set_label(GTK_BUTTON(s->replace_all), "Replace _all");
    }
}

static void
set_notice(CwSearch *s, const gchar *message)
{
    g_free(s->notice);
    s->notice = g_strdup(message);
    show_status(s);
}

static void
apply_options(CwSearch *s)
{
    if (s->updating)
        return;
    s->updating = TRUE;
    s->pending_direction = 0;
    clear_feedback(s);
    guint flags = options(s);
    gboolean by_number = (flags & MODE_MASK) == NUMBER_MODE;
    if (!by_number)
        s->last_text_mode = mode(s);

    gtk_stack_set_visible_child(GTK_STACK(s->stack), GTK_WIDGET(active_entry(s)));
    gtk_label_set_text_with_mnemonic(GTK_LABEL(s->label), by_number ? "_Number" : "_Find");
    gtk_label_set_mnemonic_widget(GTK_LABEL(s->label), GTK_WIDGET(active_entry(s)));
    gtk_widget_set_visible(s->previous, !by_number);
    gtk_button_set_label(GTK_BUTTON(s->next), by_number ? "_Go" : "_Next");
    gtk_widget_set_visible(s->match_case, !by_number);
    gtk_widget_set_visible(s->whole_word, !by_number);
    gtk_widget_set_visible(s->wrap, !by_number);
    gtk_widget_set_visible(s->replace_row, s->show_replace && !by_number);

    const gchar *query = gtk_entry_get_text(GTK_ENTRY(s->find));
    gboolean regex = (flags & MODE_MASK) == REGEX_MODE;
    g_autofree gchar *grouped = NULL;
    if (regex && (flags & WHOLE_WORD) && *query) {
        /* GtkSourceView adds word boundaries around this entire expression. */
        grouped = g_strdup_printf("(?:%s)", query);
        query = grouped;
    }
    g_object_freeze_notify(G_OBJECT(s->settings));
    gtk_source_search_settings_set_case_sensitive(s->settings, (flags & MATCH_CASE) != 0);
    gtk_source_search_settings_set_at_word_boundaries(s->settings, (flags & WHOLE_WORD) != 0);
    gtk_source_search_settings_set_regex_enabled(s->settings, regex);
    gtk_source_search_settings_set_wrap_around(s->settings, (flags & WRAP) != 0);
    gtk_source_search_settings_set_search_text(s->settings, !by_number && *query ? query : NULL);
    g_object_thaw_notify(G_OBJECT(s->settings));
    s->updating = FALSE;
    show_status(s);
}

static void
on_options(GtkWidget *widget, CwSearch *s)
{
    (void)widget;
    apply_options(s);
}

static void
on_status(GObject *object, GParamSpec *property, CwSearch *s)
{
    (void)object;
    (void)property;
    show_status(s);
    if (s->pending_direction && gtk_source_search_context_get_occurrences_count(s->context) >= 0)
        cw_search_step(s, s->pending_direction < 0);
}

static void
on_mark(GtkTextBuffer *buffer, GtkTextIter *location, GtkTextMark *mark, CwSearch *s)
{
    (void)location;
    if (mark == gtk_text_buffer_get_insert(buffer)) {
        s->pending_direction = 0;
        show_status(s);
    }
}

static void
on_edit(GtkTextBuffer *buffer, CwSearch *s)
{
    (void)buffer;
    s->pending_direction = 0;
    if (!s->replacing && gtk_widget_get_visible(s->panel)) {
        clear_feedback(s);
        show_status(s);
    }
}

static void
add_history(CwSearch *s, const gchar *text, guint flags, gint position)
{
    const gchar *kind = (flags & MODE_MASK) == REGEX_MODE ? "Regex" : "Text";
    g_autofree gchar *description = NULL;
    if ((flags & MODE_MASK) == NUMBER_MODE)
        description = g_strdup("Crumb number");
    else
        description = g_strconcat(kind, flags & WHOLE_WORD ? " · whole words" : "",
                                  flags & MATCH_CASE ? " · match case" : "",
                                  flags & WRAP ? "" : " · no wrap", NULL);

    GtkTreeIter iter;
    gtk_list_store_insert_with_values(s->history, &iter, position,
                                      COL_QUERY, text, COL_OPTIONS, flags,
                                      COL_DESCRIPTION, description, -1);
}

static void
read_history(CwSearch *s)
{
    g_autofree gchar *raw = g_key_file_get_string(s->prefs, "search", "recent", NULL);
    if (g_strcmp0(raw, s->saved_history) == 0)
        return;
    g_free(s->saved_history);
    s->saved_history = g_strdup(raw);
    gtk_list_store_clear(s->history);
    if (!raw)
        return;
    g_autoptr(GVariant) entries = g_variant_parse(G_VARIANT_TYPE("a(us)"), raw, NULL, NULL, NULL);
    if (!entries)
        return;
    GVariantIter iter;
    guint flags;
    const gchar *query;
    g_variant_iter_init(&iter, entries);
    while (g_variant_iter_next(&iter, "(u&s)", &flags, &query)) {
        if (*query && flags < 32 && (flags & MODE_MASK) <= NUMBER_MODE)
            add_history(s, query, flags, -1);
    }
}

static void
remember(CwSearch *s)
{
    const gchar *query = gtk_entry_get_text(active_entry(s));
    if (!*query)
        return;
    /* Other document windows share this preference and may have added searches. */
    read_history(s);
    guint flags = options(s);
    GtkTreeModel *model = GTK_TREE_MODEL(s->history);
    GtkTreeIter row;
    gboolean first = TRUE;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row)) {
        g_autofree gchar *text = NULL;
        guint stored;
        gtk_tree_model_get(model, &row, COL_QUERY, &text, COL_OPTIONS, &stored, -1);
        if (stored == flags && g_str_equal(text, query)) {
            if (first)
                return;
            gtk_list_store_remove(s->history, &row);
            break;
        }
        first = FALSE;
    }

    add_history(s, query, flags, 0);
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a(us)"));
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row)) {
        g_autofree gchar *text = NULL;
        guint stored;
        gtk_tree_model_get(model, &row, COL_QUERY, &text, COL_OPTIONS, &stored, -1);
        g_variant_builder_add(&builder, "(us)", stored, text);
    }
    g_autoptr(GVariant) entries = g_variant_ref_sink(g_variant_builder_end(&builder));
    g_autofree gchar *raw = g_variant_print(entries, FALSE);
    g_key_file_set_string(s->prefs, "search", "recent", raw);
    g_free(s->saved_history);
    s->saved_history = g_steal_pointer(&raw);
    if (s->prefs_changed)
        s->prefs_changed(s->user_data);
}

static void
on_history_open(GtkWidget *popover, CwSearch *s)
{
    (void)popover;
    read_history(s);
    gboolean has_history = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(s->history), NULL) > 0;
    gtk_stack_set_visible_child_name(GTK_STACK(s->recent_pages), has_history ? "list" : "empty");
}

static void
on_recent(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column, CwSearch *s)
{
    (void)column;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(model, &row, path))
        return;
    g_autofree gchar *query = NULL;
    guint flags;
    gtk_tree_model_get(model, &row, COL_QUERY, &query, COL_OPTIONS, &flags, -1);

    s->updating = TRUE;
    gtk_combo_box_set_active(GTK_COMBO_BOX(s->mode), flags & MODE_MASK);
    if ((flags & MODE_MASK) != NUMBER_MODE) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->match_case), (flags & MATCH_CASE) != 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->whole_word), (flags & WHOLE_WORD) != 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->wrap), (flags & WRAP) != 0);
    }
    gtk_entry_set_text(active_entry(s), query);
    s->updating = FALSE;
    apply_options(s);
    gtk_popover_popdown(GTK_POPOVER(s->recent_popover));
    gtk_widget_grab_focus(GTK_WIDGET(active_entry(s)));
    cw_search_step(s, FALSE);
}

static gboolean
line_for_number(CwSearch *s, gint line, guint64 number, GtkTextIter *location)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_line(s->buffer, &start, line);
    end = start;
    if (!gtk_text_iter_ends_line(&end))
        gtk_text_iter_forward_to_line_end(&end);
    g_autofree gchar *text = gtk_text_buffer_get_text(s->buffer, &start, &end, TRUE);
    CwCrumb crumb = { 0 };
    gboolean found = cw_crumb_parse(text, line == 0, &crumb) && crumb.number == number;
    if (found)
        gtk_text_buffer_get_iter_at_line_offset(s->buffer, location, line, crumb.header_start);
    cw_crumb_clear(&crumb);
    return found;
}

static void
go_to_number(CwSearch *s)
{
    g_autofree gchar *input = g_strdup(gtk_entry_get_text(GTK_ENTRY(s->number)));
    guint64 number;
    if (!g_ascii_string_to_unsigned(g_strstrip(input), 10, 1, G_MAXUINT64, &number, NULL)) {
        set_notice(s, "Enter a positive crumb number.");
        return;
    }
    GtkTextIter match;
    gint lines = gtk_text_buffer_get_line_count(s->buffer);
    gint usual = number <= (guint64)lines ? (gint)number - 1 : -1;
    gboolean found = usual >= 0 && line_for_number(s, usual, number, &match);
    for (gint line = 0; !found && line < lines; line++) {
        if (line != usual)
            found = line_for_number(s, line, number, &match);
    }
    if (!found) {
        remember(s);
        g_autofree gchar *message = g_strdup_printf("No crumb numbered %" G_GUINT64_FORMAT, number);
        set_notice(s, message);
        return;
    }
    gtk_text_buffer_place_cursor(s->buffer, &match);
    cw_reveal_cursor(GTK_TEXT_VIEW(s->view));
    cw_search_hide(s);
}

static void
reveal(CwSearch *s)
{
    gtk_widget_show(s->panel);
    gtk_source_search_context_set_highlight(s->context, mode(s) != NUMBER_MODE);
}

static void
present_match(CwSearch *s, gboolean backwards, gboolean found, gboolean wrapped,
              const GtkTextIter *start, const GtkTextIter *end)
{
    if (!found) {
        reveal(s);
        set_notice(s, gtk_source_search_context_get_occurrences_count(s->context) == 0 ?
                   "No matches" : backwards ? "No earlier match" : "No later match");
        return;
    }
    gtk_text_buffer_select_range(s->buffer, start, end);
    cw_reveal_cursor(GTK_TEXT_VIEW(s->view));
    s->wrapped = wrapped;
    show_status(s);
}

void
cw_search_step(CwSearch *s, gboolean backwards)
{
    s->pending_direction = 0;
    clear_feedback(s);
    if (mode(s) == NUMBER_MODE) {
        go_to_number(s);
        return;
    }
    if (!*gtk_entry_get_text(GTK_ENTRY(s->find))) {
        cw_search_show(s, FALSE);
        return;
    }
    g_autoptr(GError) error = gtk_source_search_context_get_regex_error(s->context);
    if (error) {
        reveal(s);
        set_notice(s, error->message);
        return;
    }

    remember(s);
    /* Let the background scan finish before navigating a freshly searched long document. */
    if (gtk_text_buffer_get_char_count(s->buffer) > 32768 &&
        gtk_source_search_context_get_occurrences_count(s->context) < 0) {
        s->pending_direction = backwards ? -1 : 1;
        return;
    }
    GtkTextIter origin, start, end;
    if (gtk_text_buffer_get_selection_bounds(s->buffer, &start, &end))
        origin = backwards ? start : end;
    else
        gtk_text_buffer_get_iter_at_mark(s->buffer, &origin, gtk_text_buffer_get_insert(s->buffer));
    gboolean wrapped = FALSE;
    gboolean found = backwards ?
        gtk_source_search_context_backward(s->context, &origin, &start, &end, &wrapped) :
        gtk_source_search_context_forward(s->context, &origin, &start, &end, &wrapped);
    present_match(s, backwards, found, wrapped, &start, &end);
}

static void
on_step(GtkWidget *widget, CwSearch *s)
{
    cw_search_step(s, widget == s->previous);
}

static void
on_replace_one(GtkWidget *widget, CwSearch *s)
{
    (void)widget;
    if (!gtk_text_view_get_editable(GTK_TEXT_VIEW(s->view)))
        return;
    clear_feedback(s);
    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(s->buffer, &start, &end)) {
        const gchar *text = gtk_entry_get_text(GTK_ENTRY(s->replacement));
        g_autoptr(GError) error = NULL;
        s->replacing = TRUE;
        gtk_text_buffer_begin_user_action(s->buffer);
        gboolean replaced = gtk_source_search_context_replace(s->context, &start, &end, text, -1, &error);
        gtk_text_buffer_end_user_action(s->buffer);
        s->replacing = FALSE;
        if (error) {
            set_notice(s, error->message);
            return;
        }
        if (replaced)
            gtk_text_buffer_place_cursor(s->buffer, &end);
    }
    cw_search_step(s, FALSE);
}

static void
on_replace_all(GtkButton *button, CwSearch *s)
{
    if (!gtk_text_view_get_editable(GTK_TEXT_VIEW(s->view)))
        return;
    gint count = gtk_source_search_context_get_occurrences_count(s->context);
    if (count <= 0)
        return;
    if (!s->confirm_all) {
        s->confirm_all = TRUE;
        gtk_button_set_label(button, "Confirm _all");
        g_autofree gchar *question = g_strdup_printf("Replace %d %s?", count, count == 1 ? "match" : "matches");
        set_notice(s, question);
        return;
    }
    clear_feedback(s);
    remember(s);
    const gchar *text = gtk_entry_get_text(GTK_ENTRY(s->replacement));
    g_autoptr(GError) error = NULL;
    s->replacing = TRUE;
    gtk_text_buffer_begin_user_action(s->buffer);
    guint changed = gtk_source_search_context_replace_all(s->context, text, -1, &error);
    gtk_text_buffer_end_user_action(s->buffer);
    s->replacing = FALSE;
    g_autofree gchar *message = g_strdup_printf("Replaced %u %s", changed, changed == 1 ? "match" : "matches");
    set_notice(s, error ? error->message : message);
}

static gboolean
on_key(GtkWidget *widget, GdkEventKey *event, CwSearch *s)
{
    if (event->keyval == GDK_KEY_Escape) {
        cw_search_hide(s);
        return GDK_EVENT_STOP;
    }
    if ((widget == s->find || widget == s->number) && (event->state & GDK_SHIFT_MASK) &&
        (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)) {
        cw_search_step(s, TRUE);
        return GDK_EVENT_STOP;
    }
    return GDK_EVENT_PROPAGATE;
}

static void
on_close(GtkButton *button, CwSearch *s)
{
    (void)button;
    cw_search_hide(s);
}

static void
select_entry(GtkWidget *entry)
{
    gtk_widget_grab_focus(entry);
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
}

static gboolean
selected_match(CwSearch *s, const GtkTextIter *start, const GtkTextIter *end)
{
    if (!gtk_source_search_settings_get_search_text(s->settings))
        return FALSE;
    if (gtk_source_search_context_get_occurrences_count(s->context) < 0)
        return TRUE;
    GtkTextIter match_start, match_end;
    return gtk_source_search_context_forward(s->context, start, &match_start, &match_end, NULL) &&
           gtk_text_iter_equal(&match_start, start) && gtk_text_iter_equal(&match_end, end);
}

void
cw_search_show(CwSearch *s, gboolean replace)
{
    if (mode(s) == NUMBER_MODE)
        gtk_combo_box_set_active(GTK_COMBO_BOX(s->mode), s->last_text_mode);
    GtkTextIter start, end;
    if (!gtk_widget_get_visible(s->panel) &&
        gtk_text_buffer_get_selection_bounds(s->buffer, &start, &end) &&
        gtk_text_iter_get_line(&start) == gtk_text_iter_get_line(&end) &&
        !selected_match(s, &start, &end)) {
        g_autofree gchar *text = gtk_text_buffer_get_text(s->buffer, &start, &end, TRUE);
        gtk_combo_box_set_active(GTK_COMBO_BOX(s->mode), TEXT_MODE);
        gtk_entry_set_text(GTK_ENTRY(s->find), text);
    }
    s->show_replace = replace;
    reveal(s);
    apply_options(s);
    select_entry(s->find);
}

void
cw_search_number(CwSearch *s)
{
    gtk_combo_box_set_active(GTK_COMBO_BOX(s->mode), NUMBER_MODE);
    reveal(s);
    apply_options(s);
    select_entry(s->number);
}

void
cw_search_hide(CwSearch *s)
{
    s->pending_direction = 0;
    if (gtk_widget_get_visible(s->panel))
        remember(s);
    gtk_widget_hide(s->panel);
    gtk_source_search_context_set_highlight(s->context, FALSE);
    if (mode(s) == NUMBER_MODE)
        gtk_combo_box_set_active(GTK_COMBO_BOX(s->mode), s->last_text_mode);
    clear_feedback(s);
    gtk_widget_grab_focus(GTK_WIDGET(s->view));
}

void
cw_search_reset(CwSearch *s)
{
    s->pending_direction = 0;
    gtk_widget_hide(s->panel);
    gtk_source_search_context_set_highlight(s->context, FALSE);
    s->updating = TRUE;
    s->show_replace = FALSE;
    gtk_entry_set_text(GTK_ENTRY(s->find), "");
    gtk_entry_set_text(GTK_ENTRY(s->number), "");
    gtk_entry_set_text(GTK_ENTRY(s->replacement), "");
    gtk_combo_box_set_active(GTK_COMBO_BOX(s->mode), TEXT_MODE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->match_case), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->whole_word), FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->wrap), TRUE);
    s->updating = FALSE;
    apply_options(s);
}

static GtkWidget *
new_row(CwSearch *s)
{
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_pack_start(GTK_BOX(s->panel), row, FALSE, FALSE, 0);
    return row;
}

static void
add(GtkWidget *row, GtkWidget *widget, gboolean expand)
{
    gtk_box_pack_start(GTK_BOX(row), widget, expand, expand, 0);
}

static GtkWidget *
button(GtkWidget *row, const gchar *label, const gchar *hint, GCallback callback, CwSearch *s)
{
    GtkWidget *widget = gtk_button_new_with_mnemonic(label);
    gtk_widget_set_tooltip_text(widget, hint);
    g_signal_connect(widget, "clicked", callback, s);
    add(row, widget, FALSE);
    return widget;
}

static GtkWidget *
toggle(GtkWidget *row, const gchar *label, const gchar *name, CwSearch *s)
{
    GtkWidget *widget = gtk_check_button_new_with_mnemonic(label);
    gtk_widget_set_name(widget, name);
    g_signal_connect(widget, "toggled", G_CALLBACK(on_options), s);
    add(row, widget, FALSE);
    return widget;
}

static void
make_history(CwSearch *s, GtkWidget *row)
{
    s->history = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_UINT, G_TYPE_STRING);
    read_history(s);
    s->recent = gtk_menu_button_new();
    gtk_widget_set_name(s->recent, "cw-search-history");
    gtk_button_set_label(GTK_BUTTON(s->recent), "Recen_t");
    gtk_button_set_use_underline(GTK_BUTTON(s->recent), TRUE);
    gtk_widget_set_tooltip_text(s->recent, "Recall a search and its options");
    add(row, s->recent, FALSE);

    s->recent_popover = gtk_popover_new(s->recent);
    /* The history stays attached to the panel while arrows and Enter operate the list. */
    GtkWidget *scroller = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scroller), 360);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 256);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), TRUE);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s->history));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(tree), FALSE);
    gtk_tree_view_set_activate_on_single_click(GTK_TREE_VIEW(tree), TRUE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(tree), COL_QUERY);
    atk_object_set_name(gtk_widget_get_accessible(tree), "Recent searches");
    GtkCellRenderer *query_cell = gtk_cell_renderer_text_new();
    g_object_set(query_cell, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 14, NULL);
    GtkTreeViewColumn *query_column = gtk_tree_view_column_new_with_attributes(
        "Search", query_cell, "text", COL_QUERY, NULL);
    gtk_tree_view_column_set_expand(query_column, TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(tree), query_column);
    GtkCellRenderer *description_cell = gtk_cell_renderer_text_new();
    g_object_set(description_cell, "scale", PANGO_SCALE_SMALL, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Options",
                                                description_cell, "text", COL_DESCRIPTION, NULL);
    gtk_container_add(GTK_CONTAINER(scroller), tree);
    s->recent_pages = gtk_stack_new();
    gtk_stack_set_homogeneous(GTK_STACK(s->recent_pages), FALSE);
    GtkWidget *empty = gtk_label_new("No recent searches");
    gtk_container_set_border_width(GTK_CONTAINER(s->recent_pages), 8);
    gtk_stack_add_named(GTK_STACK(s->recent_pages), empty, "empty");
    gtk_stack_add_named(GTK_STACK(s->recent_pages), scroller, "list");
    gtk_container_add(GTK_CONTAINER(s->recent_popover), s->recent_pages);
    gtk_widget_show_all(s->recent_pages);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(s->recent), s->recent_popover);
    g_signal_connect(s->recent_popover, "show", G_CALLBACK(on_history_open), s);
    g_signal_connect(tree, "row-activated", G_CALLBACK(on_recent), s);
}

static void
make_query_row(CwSearch *s)
{
    GtkWidget *row = new_row(s);
    s->label = gtk_label_new_with_mnemonic("_Find");
    add(row, s->label, FALSE);

    s->stack = gtk_stack_new();
    s->find = gtk_entry_new();
    s->number = gtk_entry_new();
    gtk_widget_set_name(s->find, "cw-find");
    gtk_widget_set_name(s->number, "cw-crumb-number");
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->find), "Text to find");
    gtk_entry_set_placeholder_text(GTK_ENTRY(s->number), "Crumb number");
    gtk_entry_set_input_purpose(GTK_ENTRY(s->number), GTK_INPUT_PURPOSE_DIGITS);
    gtk_stack_add_named(GTK_STACK(s->stack), s->find, "text");
    gtk_stack_add_named(GTK_STACK(s->stack), s->number, "number");
    add(row, s->stack, TRUE);
    make_history(s, row);
    s->previous = button(row, "_Previous", "Previous match (Shift+F3)", G_CALLBACK(on_step), s);
    s->next = button(row, "_Next", "Find next or jump to number (Enter)", G_CALLBACK(on_step), s);
    GtkWidget *close = gtk_button_new_from_icon_name("window-close-symbolic", GTK_ICON_SIZE_MENU);
    gtk_button_set_relief(GTK_BUTTON(close), GTK_RELIEF_NONE);
    gtk_widget_set_tooltip_text(close, "Close search (Esc)");
    atk_object_set_name(gtk_widget_get_accessible(close), "Close search");
    g_signal_connect(close, "clicked", G_CALLBACK(on_close), s);
    add(row, close, FALSE);

    g_signal_connect(s->find, "activate", G_CALLBACK(on_step), s);
    g_signal_connect(s->number, "activate", G_CALLBACK(on_step), s);
    g_signal_connect(s->find, "key-press-event", G_CALLBACK(on_key), s);
    g_signal_connect(s->number, "key-press-event", G_CALLBACK(on_key), s);
    g_signal_connect(s->find, "changed", G_CALLBACK(on_options), s);
    g_signal_connect(s->number, "changed", G_CALLBACK(on_options), s);
}

static void
make_options_row(CwSearch *s)
{
    GtkWidget *row = new_row(s);
    s->mode = gtk_combo_box_text_new();
    gtk_widget_set_name(s->mode, "cw-search-mode");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->mode), "Plain text");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->mode), "Regular expression");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(s->mode), "Crumb number");
    atk_object_set_name(gtk_widget_get_accessible(s->mode), "Search mode");
    gtk_widget_set_tooltip_text(s->mode, "Search mode");
    add(row, s->mode, FALSE);
    s->match_case = toggle(row, "Match _case", "cw-search-case", s);
    s->whole_word = toggle(row, "Whole _words", "cw-search-word", s);
    s->wrap = toggle(row, "W_rap", "cw-search-wrap", s);
    s->status = gtk_label_new("");
    gtk_widget_set_name(s->status, "cw-search-status");
    gtk_label_set_ellipsize(GTK_LABEL(s->status), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(s->status), 1.0f);
    add(row, s->status, TRUE);
    g_signal_connect(s->mode, "changed", G_CALLBACK(on_options), s);
}

static void
make_replace_row(CwSearch *s)
{
    GtkWidget *row = s->replace_row = new_row(s);
    GtkWidget *label = gtk_label_new_with_mnemonic("Rep_lace with");
    add(row, label, FALSE);
    s->replacement = gtk_entry_new();
    gtk_widget_set_name(s->replacement, "cw-replacement");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), s->replacement);
    gtk_widget_set_tooltip_text(s->replacement, "Leave empty to delete a match. Regex replacements accept captures such as \\1.");
    add(row, s->replacement, TRUE);
    s->replace_one = button(row, "Replace _one", "Replace the selected match and find the next",
                            G_CALLBACK(on_replace_one), s);
    s->replace_all = button(row, "Replace _all", "Confirm the match count before replacing all",
                            G_CALLBACK(on_replace_all), s);
    gtk_widget_set_name(s->replace_one, "cw-replace-one");
    gtk_widget_set_name(s->replace_all, "cw-replace-all");
    g_signal_connect(s->replacement, "activate", G_CALLBACK(on_replace_one), s);
    g_signal_connect(s->replacement, "changed", G_CALLBACK(on_options), s);
}

CwSearch *
cw_search_new(GtkSourceView *view, GKeyFile *preferences, void (*prefs_changed)(gpointer), gpointer user_data)
{
    CwSearch *s = g_new0(CwSearch, 1);
    s->view = g_object_ref(view);
    s->buffer = g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)));
    s->prefs = g_key_file_ref(preferences);
    s->prefs_changed = prefs_changed;
    s->user_data = user_data;
    s->settings = gtk_source_search_settings_new();
    s->context = gtk_source_search_context_new(GTK_SOURCE_BUFFER(s->buffer), s->settings);
    s->panel = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 4));
    gtk_widget_set_name(s->panel, "cw-search-panel");
    gtk_widget_set_margin_start(s->panel, 12);
    gtk_widget_set_margin_end(s->panel, 12);
    gtk_widget_set_margin_top(s->panel, 6);
    gtk_widget_set_margin_bottom(s->panel, 6);
    make_query_row(s);
    make_options_row(s);
    make_replace_row(s);
    g_signal_connect(s->panel, "key-press-event", G_CALLBACK(on_key), s);
    g_signal_connect(s->context, "notify::occurrences-count", G_CALLBACK(on_status), s);
    g_signal_connect(s->context, "notify::regex-error", G_CALLBACK(on_status), s);
    g_signal_connect(s->view, "notify::editable", G_CALLBACK(on_status), s);
    g_signal_connect(s->buffer, "mark-set", G_CALLBACK(on_mark), s);
    g_signal_connect(s->buffer, "changed", G_CALLBACK(on_edit), s);
    gtk_widget_show_all(s->panel);
    gtk_widget_set_no_show_all(s->panel, TRUE);
    cw_search_reset(s);
    return s;
}

GtkWidget *
cw_search_widget(CwSearch *s)
{
    return s->panel;
}

void
cw_search_free(CwSearch *s)
{
    if (!s)
        return;
    g_signal_handlers_disconnect_by_data(s->context, s);
    g_signal_handlers_disconnect_by_data(s->buffer, s);
    g_signal_handlers_disconnect_by_data(s->view, s);
    gtk_widget_destroy(s->panel);
    g_object_unref(s->panel);
    g_object_unref(s->history);
    g_object_unref(s->context);
    g_object_unref(s->settings);
    g_object_unref(s->buffer);
    g_object_unref(s->view);
    g_key_file_unref(s->prefs);
    g_free(s->notice);
    g_free(s->saved_history);
    g_free(s);
}

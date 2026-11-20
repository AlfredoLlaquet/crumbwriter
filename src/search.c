#include "search.h"
#include "reveal.h"
#include "crumb.h"

#include <string.h>

enum { TEXT_MODE, REGEX_MODE, NUMBER_MODE };
enum { MODE_MASK = 3, MATCH_CASE = 4, WHOLE_WORD = 8, WRAP = 16, NUMBER_NO_WRAP = 32 };
enum { COL_QUERY, COL_OPTIONS, COL_DESCRIPTION, N_COLUMNS };
static const gchar *const mode_titles[] = {"Plain text", "Regular expression", "Crumb number"};

typedef struct {
    CwSearch *owner;
    GCancellable *cancel;
    GtkTextBuffer *buffer;
    gboolean backwards;
} Preview;

struct CwSearch {
    GtkSourceView *view;
    GtkTextBuffer *buffer;
    GtkTextMark *anchor;
    GtkSourceSearchSettings *settings;
    GtkSourceSearchContext *context;
    GKeyFile *prefs;
    void (*prefs_changed)(gpointer);
    gpointer user_data;
    GtkWidget *panel, *label, *stack, *find, *number, *mode;
    GtkWidget *mode_popover, *mode_choices[3];
    GtkWidget *previous, *next, *match_case, *whole_word, *wrap;
    GtkWidget *replace_row, *replacement, *replace_one, *replace_all;
    GtkWidget *recent, *recent_popover, *recent_pages, *recent_tree, *recent_scroll, *recent_empty, *status;
    GtkListStore *history;
    gchar *saved_history;
    gint pending_direction;
    gint result_end; /* Character offset of the last hit, cleared when the manuscript changes or the caret moves. */
    gint mode_id, last_text_mode;
    Preview *preview;
    gboolean backwards, from_anchor, selecting;
    gboolean show_replace, confirm_all, updating, replacing, wrapped;
    gchar *notice;
};

static gint
mode(CwSearch *s)
{
    return s->mode_id;
}

static void preview_at_cursor(CwSearch *s);
static void select_entry(GtkWidget *entry);

static void
cancel_preview(CwSearch *s)
{
    if (s->preview) {
        Preview *request = s->preview;
        s->preview = NULL;
        request->owner = NULL;
        g_cancellable_cancel(request->cancel);
    }
}

static void
set_mode(CwSearch *s, gint target)
{
    static const gchar *const captions[] = {"Text ▾", "Regex ▾", "Number ▾"};
    s->mode_id = target;
    gtk_button_set_label(GTK_BUTTON(s->mode), captions[target]);
    for (guint i = 0; i < G_N_ELEMENTS(s->mode_choices); i++)
        g_object_set(s->mode_choices[i], "active", (gint)i == target, NULL);
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
    if (result == NUMBER_MODE) {
        /* Older saved numeric searches had no option bits and wrapped. */
        return result | (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->wrap)) ? 0 : NUMBER_NO_WRAP);
    }
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
        gtk_button_set_label(GTK_BUTTON(s->replace_all), "_All");
    }
}

static void
set_notice(CwSearch *s, const gchar *message)
{
    g_free(s->notice);
    s->notice = g_strdup(message);
    show_status(s);
}

/* Preserve PCRE start directives such as (*UTF) before the inline option.
 * The added grouping is noncapturing, so replacement group numbers survive. */
static gchar *
regex_pattern(const gchar *query, gboolean whole_words)
{
    const gchar *body = query;
    while (g_str_has_prefix(body, "(*")) {
        const gchar *end = body + 2 + strspn(body + 2, "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_=");
        if (*end != ')')
            break;
        body = end + 1;
    }
    GString *pattern = g_string_new_len(query, body - query);
    g_string_append(pattern, "(?U)");
    if (whole_words)
        g_string_append(pattern, "\\b(?:");
    g_string_append(pattern, body);
    if (whole_words)
        g_string_append(pattern, ")\\b");
    return g_string_free(pattern, FALSE);
}

static void
apply_options(CwSearch *s)
{
    if (s->updating)
        return;
    s->updating = TRUE;
    cancel_preview(s);
    s->pending_direction = 0;
    clear_feedback(s);
    guint flags = options(s);
    gboolean by_number = (flags & MODE_MASK) == NUMBER_MODE;
    if (!by_number)
        s->last_text_mode = mode(s);

    gtk_stack_set_visible_child(GTK_STACK(s->stack), GTK_WIDGET(active_entry(s)));
    gtk_label_set_text_with_mnemonic(GTK_LABEL(s->label), by_number ? "_Number" :
                                     s->backwards ? "_Find ↑" : "_Find ↓");
    atk_object_set_name(gtk_widget_get_accessible(s->find),
                        s->backwards ? "Find backward" : "Find forward");
    gtk_source_search_context_set_highlight(s->context, !by_number && gtk_widget_get_visible(s->panel));
    gtk_label_set_mnemonic_widget(GTK_LABEL(s->label), GTK_WIDGET(active_entry(s)));
    gtk_widget_set_visible(s->previous, !by_number);
    GtkWidget *image = gtk_button_get_image(GTK_BUTTON(s->next));
    gtk_image_set_from_icon_name(GTK_IMAGE(image), by_number ? "go-jump-symbolic" : "go-down-symbolic",
                                 GTK_ICON_SIZE_MENU);
    atk_object_set_name(gtk_widget_get_accessible(s->next), by_number ? "Go to crumb" : "Next match");
    gtk_widget_set_tooltip_text(s->previous, s->backwards ? "Previous match (F3)" : "Previous match (Shift+F3)");
    gtk_widget_set_tooltip_text(s->next, by_number ? "Go to crumb (Enter)" :
                                s->backwards ? "Next match (Shift+F3)" : "Next match (F3)");
    gtk_widget_set_visible(s->match_case, !by_number);
    gtk_widget_set_visible(s->whole_word, !by_number);
    gtk_widget_show(s->wrap);
    gtk_widget_set_visible(s->replace_row, s->show_replace && !by_number);

    const gchar *query = gtk_entry_get_text(GTK_ENTRY(s->find));
    gboolean regex = (flags & MODE_MASK) == REGEX_MODE;
    gboolean whole_words = (flags & WHOLE_WORD) != 0;
    g_autofree gchar *compiled = regex && *query ? regex_pattern(query, whole_words) : NULL;
    g_object_freeze_notify(G_OBJECT(s->settings));
    gtk_source_search_settings_set_case_sensitive(s->settings, (flags & MATCH_CASE) != 0);
    gtk_source_search_settings_set_at_word_boundaries(s->settings, !regex && whole_words);
    gtk_source_search_settings_set_regex_enabled(s->settings, regex);
    gtk_source_search_settings_set_wrap_around(s->settings, (flags & WRAP) != 0);
    gtk_source_search_settings_set_search_text(s->settings, by_number || !*query ? NULL :
                                              compiled ? compiled : query);
    g_object_thaw_notify(G_OBJECT(s->settings));
    s->updating = FALSE;
    show_status(s);
}

static void
on_options(GtkWidget *widget, CwSearch *s)
{
    if (s->updating)
        return;
    if (widget == s->replacement) {
        clear_feedback(s);
        show_status(s);
        return;
    }
    s->from_anchor = TRUE;
    apply_options(s);
    preview_at_cursor(s);
}

static void
on_status(GObject *object, GParamSpec *property, CwSearch *s)
{
    (void)object;
    (void)property;
    show_status(s);
    if (s->pending_direction && !s->preview && gtk_source_search_context_get_occurrences_count(s->context) >= 0)
        cw_search_step(s, s->pending_direction < 0);
}

static void
on_mark(GtkTextBuffer *buffer, GtkTextIter *location, GtkTextMark *mark, CwSearch *s)
{
    if (mark == gtk_text_buffer_get_insert(buffer)) {
        if (!s->selecting) {
            cancel_preview(s);
            s->result_end = -1;
            s->from_anchor = TRUE;
            s->pending_direction = 0;
            if (gtk_widget_get_visible(s->panel))
                gtk_text_buffer_move_mark(buffer, s->anchor, location);
        }
        show_status(s);
    }
}

static void
on_edit(GtkTextBuffer *buffer, CwSearch *s)
{
    cancel_preview(s);
    s->result_end = -1;
    s->from_anchor = TRUE;
    s->pending_direction = 0;
    if (gtk_widget_has_focus(GTK_WIDGET(s->view)) && gtk_widget_get_visible(s->panel)) {
        GtkTextIter cursor;
        gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
        gtk_text_buffer_move_mark(buffer, s->anchor, &cursor);
    }
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
        description = g_strdup(flags & NUMBER_NO_WRAP ? "Crumb number · no wrap" : "Crumb number");
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
        if (*query && ((flags < 32 && (flags & MODE_MASK) <= NUMBER_MODE) ||
                       flags == (NUMBER_MODE | NUMBER_NO_WRAP)))
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
    GtkWidget *window = gtk_widget_get_toplevel(s->panel);
    gint room = gtk_widget_get_allocated_width(window) - 52;
    gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(s->recent_scroll),
                                              CLAMP(room, 140, 360));
    gtk_stack_set_visible_child_name(GTK_STACK(s->recent_pages), has_history ? "list" : "empty");
    if (has_history) {
        GtkTreePath *first = gtk_tree_path_new_first();
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(s->recent_tree), first, NULL, FALSE);
        gtk_tree_path_free(first);
        gtk_widget_grab_focus(s->recent_tree);
    } else
        gtk_widget_grab_focus(s->recent_empty);
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
    set_mode(s, flags & MODE_MASK);
    if ((flags & MODE_MASK) != NUMBER_MODE) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->match_case), (flags & MATCH_CASE) != 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->whole_word), (flags & WHOLE_WORD) != 0);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->wrap), (flags & WRAP) != 0);
    } else
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->wrap), !(flags & NUMBER_NO_WRAP));
    gtk_entry_set_text(active_entry(s), query);
    s->updating = FALSE;
    s->from_anchor = TRUE;
    apply_options(s);
    gtk_popover_popdown(GTK_POPOVER(s->recent_popover));
    s->selecting = TRUE;
    gtk_widget_grab_focus(GTK_WIDGET(active_entry(s)));
    s->selecting = FALSE;
    if (mode(s) == TEXT_MODE) {
        remember(s);
        preview_at_cursor(s);
    } else
        cw_search_step(s, s->backwards);
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

static gboolean
number_between(CwSearch *s, guint64 number, const GtkTextIter *origin, gboolean backwards,
               gboolean wrapped, GtkTextIter *match)
{
    gint here = gtk_text_iter_get_line(origin);
    gint edge = backwards ? gtk_text_buffer_get_line_count(s->buffer) - 1 : 0;
    gint far = backwards ? 0 : gtk_text_buffer_get_line_count(s->buffer) - 1;
    gint step = backwards ? -1 : 1;
    gint first = wrapped ? edge : here, last = wrapped ? here : far;
    for (gint line = first; ; line += step) {
        if (line_for_number(s, line, number, match)) {
            gint side = gtk_text_iter_compare(match, origin);
            if (wrapped || (backwards ? side <= 0 : side >= 0))
                return TRUE;
        }
        if (line == last)
            return FALSE;
    }
}

static void
go_to_number(CwSearch *s, gboolean backwards)
{
    g_autofree gchar *input = g_strdup(gtk_entry_get_text(GTK_ENTRY(s->number)));
    guint64 number;
    if (!g_ascii_string_to_unsigned(g_strstrip(input), 10, 1, G_MAXUINT64, &number, NULL)) {
        set_notice(s, "Enter a positive crumb number.");
        return;
    }
    GtkTextIter origin, match;
    gtk_text_buffer_get_iter_at_mark(s->buffer, &origin, s->anchor);
    gboolean found = number_between(s, number, &origin, backwards, FALSE, &match);
    gboolean wrap = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->wrap));
    if (!found && wrap)
        found = number_between(s, number, &origin, backwards, TRUE, &match);
    if (!found) {
        remember(s);
        const gchar *scope = wrap ? "" : backwards ? " earlier" : " later";
        g_autofree gchar *message = g_strdup_printf("No%s crumb numbered %" G_GUINT64_FORMAT, scope, number);
        set_notice(s, message);
        return;
    }
    gtk_text_buffer_place_cursor(s->buffer, &match);
    cw_search_hide(s);
    cw_reveal_center_line(GTK_TEXT_VIEW(s->view), &match, 0);
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
    s->selecting = TRUE;
    gtk_text_buffer_select_range(s->buffer, start, end);
    s->selecting = FALSE;
    s->result_end = gtk_text_iter_get_offset(end);
    s->from_anchor = FALSE;
    cw_reveal_center_line(GTK_TEXT_VIEW(s->view), start, 0);
    s->wrapped = wrapped;
    show_status(s);
}

static void
show_preview(CwSearch *s, gboolean found, gboolean wrapped, const GtkTextIter *start, const GtkTextIter *end)
{
    if (found) {
        present_match(s, s->backwards, TRUE, wrapped, start, end);
        return;
    }
    if (s->result_end >= 0 || gtk_text_buffer_get_has_selection(s->buffer)) {
        GtkTextIter original;
        gtk_text_buffer_get_iter_at_mark(s->buffer, &original, s->anchor);
        s->selecting = TRUE;
        gtk_text_buffer_place_cursor(s->buffer, &original);
        s->selecting = FALSE;
        s->result_end = -1;
        s->from_anchor = TRUE;
        cw_reveal_cursor(GTK_TEXT_VIEW(s->view));
    }
    if (*gtk_entry_get_text(GTK_ENTRY(s->find)) &&
        !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->wrap)) &&
        gtk_source_search_context_get_occurrences_count(s->context) != 0)
        set_notice(s, s->backwards ? "No earlier match" : "No later match");
}

static void
preview_finished(GObject *source, GAsyncResult *result, gpointer data)
{
    Preview *request = data;
    /* GtkSourceView 4's task removes private marks at finalization. The
     * buffer must outlive the task, including when the window has closed. */
    g_object_set_data_full(G_OBJECT(result), "cw-preview-buffer", request->buffer, g_object_unref);
    GtkTextIter start, end;
    g_autoptr(GError) error = NULL;
    GtkSourceSearchContext *context = GTK_SOURCE_SEARCH_CONTEXT(source);
    gboolean wrapped = FALSE;
    gboolean found = request->backwards ?
        gtk_source_search_context_backward_finish(context, result, &start, &end, &wrapped, &error) :
        gtk_source_search_context_forward_finish(context, result, &start, &end, &wrapped, &error);
    CwSearch *s = request->owner;
    if (s) {
        s->preview = NULL;
        gint queued = s->pending_direction;
        s->pending_direction = 0;
        if (!error) {
            show_preview(s, found, wrapped, &start, &end);
            if (queued)
                cw_search_step(s, queued < 0);
        }
    }
    g_object_unref(request->cancel);
    g_free(request);
}

static void
preview_at_cursor(CwSearch *s)
{
    if (mode(s) != TEXT_MODE || !gtk_widget_get_visible(s->panel))
        return;
    if (!*gtk_entry_get_text(GTK_ENTRY(s->find))) {
        show_preview(s, FALSE, FALSE, NULL, NULL);
        return;
    }
    GtkTextIter origin, start, end;
    gtk_text_buffer_get_iter_at_mark(s->buffer, &origin, s->anchor);
    if (gtk_text_buffer_get_char_count(s->buffer) > 32768 &&
        gtk_source_search_context_get_occurrences_count(s->context) < 0) {
        Preview *request = g_new0(Preview, 1);
        request->owner = s;
        request->cancel = g_cancellable_new();
        request->buffer = g_object_ref(s->buffer);
        request->backwards = s->backwards;
        s->preview = request;
        if (request->backwards)
            gtk_source_search_context_backward_async(s->context, &origin, request->cancel, preview_finished, request);
        else
            gtk_source_search_context_forward_async(s->context, &origin, request->cancel, preview_finished, request);
        return;
    }
    gboolean wrapped = FALSE;
    gboolean found = s->backwards ?
        gtk_source_search_context_backward(s->context, &origin, &start, &end, &wrapped) :
        gtk_source_search_context_forward(s->context, &origin, &start, &end, &wrapped);
    show_preview(s, found, wrapped, &start, &end);
}

void
cw_search_step(CwSearch *s, gboolean backwards)
{
    s->pending_direction = 0;
    clear_feedback(s);
    if (mode(s) == NUMBER_MODE) {
        go_to_number(s, backwards);
        return;
    }
    if (!*gtk_entry_get_text(GTK_ENTRY(s->find))) {
        cw_search_find(s, s->backwards);
        return;
    }
    g_autoptr(GError) error = gtk_source_search_context_get_regex_error(s->context);
    if (error) {
        reveal(s);
        set_notice(s, error->message);
        return;
    }

    remember(s);
    if (s->preview) {
        s->pending_direction = backwards ? -1 : 1;
        return;
    }
    /* Let the background scan finish before navigating a freshly searched long document. */
    if (gtk_text_buffer_get_char_count(s->buffer) > 32768 &&
        gtk_source_search_context_get_occurrences_count(s->context) < 0) {
        s->pending_direction = backwards ? -1 : 1;
        return;
    }
    GtkTextIter origin, start, end;
    if (s->from_anchor && gtk_widget_get_visible(s->panel))
        gtk_text_buffer_get_iter_at_mark(s->buffer, &origin, s->anchor);
    /* Escape can collapse a hit at its start. Forward repetition still
     * resumes at its end until a real edit or caret movement intervenes. */
    else if (!s->from_anchor && !backwards && s->result_end >= 0)
        gtk_text_buffer_get_iter_at_offset(s->buffer, &origin, s->result_end);
    else
        gtk_text_buffer_get_iter_at_mark(s->buffer, &origin, gtk_text_buffer_get_insert(s->buffer));
    gboolean wrapped = FALSE;
    gboolean found = backwards ?
        gtk_source_search_context_backward(s->context, &origin, &start, &end, &wrapped) :
        gtk_source_search_context_forward(s->context, &origin, &start, &end, &wrapped);
    present_match(s, backwards, found, wrapped, &start, &end);
}

void
cw_search_repeat(CwSearch *s, gboolean opposite)
{
    cw_search_step(s, opposite ? !s->backwards : s->backwards);
}

static void
on_step(GtkWidget *widget, CwSearch *s)
{
    gboolean backwards = s->backwards;
    if (widget == s->previous)
        backwards = TRUE;
    else if (widget == s->next && mode(s) != NUMBER_MODE)
        backwards = FALSE;
    cw_search_step(s, backwards);
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

static void
choose_mode(CwSearch *s, gint target)
{
    s->selecting = TRUE;
    set_mode(s, target);
    s->from_anchor = TRUE;
    gtk_popover_popdown(GTK_POPOVER(s->mode_popover));
    gtk_popover_popdown(GTK_POPOVER(s->recent_popover));
    apply_options(s);
    select_entry(GTK_WIDGET(active_entry(s)));
    s->selecting = FALSE;
    preview_at_cursor(s);
}

gboolean
cw_search_handle_key(CwSearch *s, GdkEventKey *event)
{
    GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
    guint key = gdk_keyval_to_lower(event->keyval);
    if (key == GDK_KEY_F3 && (mods == 0 || mods == GDK_SHIFT_MASK)) {
        cw_search_repeat(s, mods == GDK_SHIFT_MASK);
        return TRUE;
    }
    gboolean visible = gtk_widget_get_visible(s->panel);
    if (key == GDK_KEY_Escape && mods == 0) {
        gboolean selected = gtk_text_buffer_get_has_selection(s->buffer);
        if (!visible && (s->result_end < 0 || !selected))
            return FALSE;
        if (visible)
            cw_search_hide(s);
        if (selected) {
            GtkTextIter cursor;
            gtk_text_buffer_get_iter_at_mark(s->buffer, &cursor, gtk_text_buffer_get_insert(s->buffer));
            /* Keep the match centered as closing Find expands the paper. */
            gtk_text_buffer_move_mark(s->buffer, gtk_text_buffer_get_selection_bound(s->buffer), &cursor);
        }
        return TRUE;
    }
    if (!visible)
        return FALSE;
    if (mods == GDK_CONTROL_MASK) {
        switch (key) {
        case GDK_KEY_p: choose_mode(s, TEXT_MODE); return TRUE;
        case GDK_KEY_r: choose_mode(s, REGEX_MODE); return TRUE;
        case GDK_KEY_n: choose_mode(s, NUMBER_MODE); return TRUE;
        default: break;
        }
    }
    if (mods == GDK_MOD1_MASK && (key == GDK_KEY_Down || key == GDK_KEY_KP_Down)) {
        gtk_popover_popdown(GTK_POPOVER(s->mode_popover));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->recent), TRUE);
        return TRUE;
    }
    GtkWidget *option = NULL;
    if (mods == GDK_CONTROL_MASK && key == GDK_KEY_m)
        option = s->match_case;
    else if (mods == GDK_CONTROL_MASK && key == GDK_KEY_w)
        option = s->whole_word;
    else if (mods == GDK_MOD1_MASK && key == GDK_KEY_w)
        option = s->wrap;
    if (option) {
        GtkToggleButton *toggle = GTK_TOGGLE_BUTTON(option);
        gtk_toggle_button_set_active(toggle, !gtk_toggle_button_get_active(toggle));
        return TRUE;
    }
    return FALSE;
}

static gboolean
on_key(GtkWidget *widget, GdkEventKey *event, CwSearch *s)
{
    if (cw_search_handle_key(s, event))
        return GDK_EVENT_STOP;
    if ((widget == s->find || widget == s->number) &&
        (event->state & gtk_accelerator_get_default_mod_mask()) == GDK_SHIFT_MASK &&
        (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter)) {
        cw_search_repeat(s, TRUE);
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

static void
begin_at_cursor(CwSearch *s, gboolean backwards)
{
    cancel_preview(s);
    s->pending_direction = 0;
    s->backwards = backwards;
    s->from_anchor = TRUE;
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(s->buffer, &cursor, gtk_text_buffer_get_insert(s->buffer));
    gtk_text_buffer_move_mark(s->buffer, s->anchor, &cursor);
}

static void
open_search(CwSearch *s, gboolean replace, gboolean backwards)
{
    begin_at_cursor(s, backwards);
    s->selecting = TRUE;
    gtk_popover_popdown(GTK_POPOVER(s->mode_popover));
    gtk_popover_popdown(GTK_POPOVER(s->recent_popover));
    if (mode(s) == NUMBER_MODE)
        set_mode(s, s->last_text_mode);
    GtkTextIter start, end;
    if (!gtk_widget_get_visible(s->panel) &&
        gtk_text_buffer_get_selection_bounds(s->buffer, &start, &end) &&
        gtk_text_iter_get_line(&start) == gtk_text_iter_get_line(&end) &&
        !selected_match(s, &start, &end)) {
        g_autofree gchar *text = gtk_text_buffer_get_text(s->buffer, &start, &end, TRUE);
        set_mode(s, TEXT_MODE);
        gtk_entry_set_text(GTK_ENTRY(s->find), text);
    }
    s->show_replace = replace;
    reveal(s);
    apply_options(s);
    select_entry(s->find);
    s->selecting = FALSE;
}

void
cw_search_find(CwSearch *s, gboolean backwards)
{
    open_search(s, FALSE, backwards);
}

void
cw_search_show(CwSearch *s, gboolean replace)
{
    open_search(s, replace, FALSE);
}

void
cw_search_number(CwSearch *s)
{
    begin_at_cursor(s, FALSE);
    s->selecting = TRUE;
    gtk_popover_popdown(GTK_POPOVER(s->mode_popover));
    gtk_popover_popdown(GTK_POPOVER(s->recent_popover));
    set_mode(s, NUMBER_MODE);
    reveal(s);
    apply_options(s);
    select_entry(s->number);
    s->selecting = FALSE;
}

void
cw_search_hide(CwSearch *s)
{
    cancel_preview(s);
    s->pending_direction = 0;
    if (gtk_widget_get_visible(s->panel))
        remember(s);
    gtk_popover_popdown(GTK_POPOVER(s->recent_popover));
    gtk_popover_popdown(GTK_POPOVER(s->mode_popover));
    gtk_widget_hide(s->panel);
    gtk_source_search_context_set_highlight(s->context, FALSE);
    if (mode(s) == NUMBER_MODE) {
        set_mode(s, s->last_text_mode);
        apply_options(s);
    }
    clear_feedback(s);
    gtk_widget_grab_focus(GTK_WIDGET(s->view));
}

void
cw_search_reset(CwSearch *s)
{
    cancel_preview(s);
    s->result_end = -1;
    s->backwards = FALSE;
    s->from_anchor = TRUE;
    s->pending_direction = 0;
    gtk_widget_hide(s->panel);
    gtk_popover_popdown(GTK_POPOVER(s->mode_popover));
    gtk_popover_popdown(GTK_POPOVER(s->recent_popover));
    gtk_source_search_context_set_highlight(s->context, FALSE);
    s->updating = TRUE;
    s->show_replace = FALSE;
    gtk_entry_set_text(GTK_ENTRY(s->find), "");
    gtk_entry_set_text(GTK_ENTRY(s->number), "");
    gtk_entry_set_text(GTK_ENTRY(s->replacement), "");
    set_mode(s, TEXT_MODE);
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

static GtkWidget *
navigation_button(GtkWidget *row, const gchar *icon, const gchar *name, CwSearch *s)
{
    GtkWidget *widget = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_MENU);
    atk_object_set_name(gtk_widget_get_accessible(widget), name);
    g_signal_connect(widget, "clicked", G_CALLBACK(on_step), s);
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
    gtk_widget_set_tooltip_text(s->recent, "Recall a search and its options (Alt+Down)");
    add(row, s->recent, FALSE);

    s->recent_popover = gtk_popover_new(s->recent);
    /* The history stays attached to the panel while arrows and Enter operate the list. */
    GtkWidget *scroller = s->recent_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroller), 256);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroller), TRUE);
    GtkWidget *tree = s->recent_tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(s->history));
    gtk_widget_set_name(tree, "cw-recent-list");
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
    g_object_set(description_cell, "scale", PANGO_SCALE_SMALL, "ellipsize", PANGO_ELLIPSIZE_END,
                 "width-chars", 9, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree), -1, "Options",
                                                description_cell, "text", COL_DESCRIPTION, NULL);
    gtk_container_add(GTK_CONTAINER(scroller), tree);
    s->recent_pages = gtk_stack_new();
    gtk_stack_set_homogeneous(GTK_STACK(s->recent_pages), FALSE);
    GtkWidget *empty = s->recent_empty = gtk_label_new("No recent searches");
    gtk_widget_set_can_focus(empty, TRUE);
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
    gtk_entry_set_width_chars(GTK_ENTRY(s->find), 4);
    gtk_entry_set_width_chars(GTK_ENTRY(s->number), 4);
    gtk_entry_set_input_purpose(GTK_ENTRY(s->number), GTK_INPUT_PURPOSE_DIGITS);
    gtk_stack_add_named(GTK_STACK(s->stack), s->find, "text");
    gtk_stack_add_named(GTK_STACK(s->stack), s->number, "number");
    add(row, s->stack, TRUE);
    make_history(s, row);
    s->previous = navigation_button(row, "go-up-symbolic", "Previous match", s);
    s->next = navigation_button(row, "go-down-symbolic", "Next match", s);
    gtk_widget_set_name(s->previous, "cw-search-previous");
    gtk_widget_set_name(s->next, "cw-search-next");
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
on_mode(GtkButton *button, CwSearch *s)
{
    gint target = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "cw-mode"));
    choose_mode(s, target);
}

static void
make_mode_picker(GtkWidget *row, CwSearch *s)
{
    static const gchar *const ids[] = {"cw-mode-plain", "cw-mode-regex", "cw-mode-number"};
    static const gchar *const keys[] = {"Ctrl+P", "Ctrl+R", "Ctrl+N"};
    s->mode = gtk_menu_button_new();
    gtk_widget_set_name(s->mode, "cw-search-mode");
    atk_object_set_name(gtk_widget_get_accessible(s->mode), "Search mode");
    gtk_widget_set_tooltip_text(s->mode, "Search mode");
    add(row, s->mode, FALSE);
    s->mode_popover = gtk_popover_new(s->mode);
    gtk_popover_set_constrain_to(GTK_POPOVER(s->mode_popover), GTK_POPOVER_CONSTRAINT_NONE);
    GtkWidget *choices = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(choices), 4);
    for (guint i = 0; i < G_N_ELEMENTS(mode_titles); i++) {
        GtkWidget *choice = s->mode_choices[i] = gtk_model_button_new();
        gtk_widget_set_name(choice, ids[i]);
        g_autofree gchar *label = g_strdup_printf("%s    %s", mode_titles[i], keys[i]);
        g_object_set(choice, "role", GTK_BUTTON_ROLE_RADIO, "text", label, NULL);
        gtk_box_pack_start(GTK_BOX(choices), choice, FALSE, FALSE, 0);
        g_object_set_data(G_OBJECT(choice), "cw-mode", GINT_TO_POINTER((gint)i));
        g_signal_connect(choice, "clicked", G_CALLBACK(on_mode), s);
    }
    gtk_container_add(GTK_CONTAINER(s->mode_popover), choices);
    gtk_widget_show_all(choices);
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(s->mode), s->mode_popover);
}

static void
make_options_row(CwSearch *s)
{
    GtkWidget *row = new_row(s);
    make_mode_picker(row, s);
    s->match_case = toggle(row, "Match _case", "cw-search-case", s);
    s->whole_word = toggle(row, "Whole _words", "cw-search-word", s);
    s->wrap = toggle(row, "W_rap", "cw-search-wrap", s);
    gtk_widget_set_tooltip_text(s->match_case, "Match case (Ctrl+M)");
    gtk_widget_set_tooltip_text(s->whole_word, "Match whole words (Ctrl+W)");
    gtk_widget_set_tooltip_text(s->wrap, "Wrap around the document (Alt+W)");
    atk_object_set_name(gtk_widget_get_accessible(s->match_case), "Match case");
    atk_object_set_name(gtk_widget_get_accessible(s->whole_word), "Whole words");
    atk_object_set_name(gtk_widget_get_accessible(s->wrap), "Wrap around");
    s->status = gtk_label_new("");
    gtk_widget_set_name(s->status, "cw-search-status");
    gtk_label_set_ellipsize(GTK_LABEL(s->status), PANGO_ELLIPSIZE_END);
    gtk_label_set_xalign(GTK_LABEL(s->status), 1.0f);
    add(row, s->status, TRUE);
}

static void
make_replace_row(CwSearch *s)
{
    GtkWidget *row = s->replace_row = new_row(s);
    GtkWidget *label = gtk_label_new_with_mnemonic("Rep_lace with");
    add(row, label, FALSE);
    s->replacement = gtk_entry_new();
    gtk_entry_set_width_chars(GTK_ENTRY(s->replacement), 4);
    gtk_widget_set_name(s->replacement, "cw-replacement");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), s->replacement);
    gtk_widget_set_tooltip_text(s->replacement, "Leave empty to delete a match. Regex replacements accept captures such as \\1.");
    add(row, s->replacement, TRUE);
    s->replace_one = button(row, "_One", "Replace the selected match and find the next",
                            G_CALLBACK(on_replace_one), s);
    s->replace_all = button(row, "_All", "Confirm the match count before replacing all",
                            G_CALLBACK(on_replace_all), s);
    gtk_widget_set_name(s->replace_one, "cw-replace-one");
    gtk_widget_set_name(s->replace_all, "cw-replace-all");
    atk_object_set_name(gtk_widget_get_accessible(s->replace_one), "Replace one");
    atk_object_set_name(gtk_widget_get_accessible(s->replace_all), "Replace all");
    g_signal_connect(s->replacement, "activate", G_CALLBACK(on_replace_one), s);
    g_signal_connect(s->replacement, "changed", G_CALLBACK(on_options), s);
}

CwSearch *
cw_search_new(GtkSourceView *view, GKeyFile *preferences, void (*prefs_changed)(gpointer), gpointer user_data)
{
    CwSearch *s = g_new0(CwSearch, 1);
    s->view = g_object_ref(view);
    s->buffer = g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)));
    GtkTextIter start;
    gtk_text_buffer_get_start_iter(s->buffer, &start);
    s->anchor = gtk_text_buffer_create_mark(s->buffer, NULL, &start, TRUE);
    s->prefs = g_key_file_ref(preferences);
    s->prefs_changed = prefs_changed;
    s->user_data = user_data;
    s->settings = gtk_source_search_settings_new();
    s->context = gtk_source_search_context_new(GTK_SOURCE_BUFFER(s->buffer), s->settings);
    s->panel = g_object_ref_sink(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2));
    gtk_widget_set_name(s->panel, "cw-search-panel");
    gtk_widget_set_margin_start(s->panel, 7);
    gtk_widget_set_margin_end(s->panel, 7);
    gtk_widget_set_margin_top(s->panel, 2);
    gtk_widget_set_margin_bottom(s->panel, 2);
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
    cancel_preview(s);
    g_signal_handlers_disconnect_by_data(s->context, s);
    g_signal_handlers_disconnect_by_data(s->buffer, s);
    gtk_text_buffer_delete_mark(s->buffer, s->anchor);
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

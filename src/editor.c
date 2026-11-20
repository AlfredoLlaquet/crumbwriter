#include "editor.h"

#include "crumb.h"

typedef struct {
    CwCrumb crumb;
    guint words;
    GSequenceIter *position;
} DocumentLine;

enum {
    HISTORY_ID,
    HISTORY_NUMBER,
    HISTORY_TITLE,
    HISTORY_VISIBLE,
    HISTORY_COLUMNS
};

struct _CwEditor {
    GtkSourceView *view;
    GtkTextBuffer *buffer;
    GtkTextTag *header_tag;
    GtkTextTag *title_tag;

    GtkWidget *sidebar;
    GtkWidget *current_number;
    GtkWidget *current_title;
    GtkWidget *words;
    GtkWidget *new_crumbs;
    GtkTreeView *history;
    GtkListStore *history_store;

    GSequence *lines;          /* One cached record per physical buffer line. */
    GHashTable *numbers;       /* Number -> array of DocumentLine pointers. */
    GHashTable *opening_ids;   /* Set of distinct numbers in the opened file. */
    GHashTable *visits;        /* Number -> stable GtkListStore iterator. */
    guint new_count;
    gint shown_line;
    gboolean active_number_set;
    guint64 active_number;

    gboolean loading;
    gboolean editing;
    gint edit_first;
    gint edit_last;
};

static guint64 *
number_key(guint64 number)
{
    guint64 *key = g_new(guint64, 1);
    *key = number;
    return key;
}

static void
free_line(gpointer data)
{
    DocumentLine *line = data;
    cw_crumb_clear(&line->crumb);
    g_free(line);
}

static GtkTextIter
line_end(GtkTextIter start)
{
    if (!gtk_text_iter_ends_line(&start))
        gtk_text_iter_forward_to_line_end(&start);
    return start;
}

static DocumentLine *
numbered_line(CwEditor *editor, guint64 number)
{
    GPtrArray *matches = g_hash_table_lookup(editor->numbers, &number);
    if (!matches)
        return NULL;

    /* A partially edited file can temporarily contain duplicate numbers. */
    DocumentLine *first = g_ptr_array_index(matches, 0);
    for (guint i = 1; i < matches->len; i++) {
        DocumentLine *candidate = g_ptr_array_index(matches, i);
        if (g_sequence_iter_compare(candidate->position, first->position) < 0)
            first = candidate;
    }
    return first;
}

static void
index_number(CwEditor *editor, DocumentLine *line)
{
    guint64 number = line->crumb.number;
    GPtrArray *matches = g_hash_table_lookup(editor->numbers, &number);
    if (!matches) {
        matches = g_ptr_array_new();
        g_hash_table_insert(editor->numbers, number_key(number), matches);
        if (editor->loading)
            g_hash_table_add(editor->opening_ids, number_key(number));
        else if (!g_hash_table_contains(editor->opening_ids, &number))
            editor->new_count++;
    }
    g_ptr_array_add(matches, line);
}

static void
unindex_number(CwEditor *editor, DocumentLine *line)
{
    guint64 number = line->crumb.number;
    GPtrArray *matches = g_hash_table_lookup(editor->numbers, &number);
    g_ptr_array_remove_fast(matches, line);
    if (matches->len == 0) {
        g_hash_table_remove(editor->numbers, &number);
        if (!g_hash_table_contains(editor->opening_ids, &number))
            editor->new_count--;
    }
}

static void
style_span(CwEditor *editor, GtkTextTag *tag, GtkTextIter start,
           gint left, gint right)
{
    if (left < 0 || right <= left)
        return;
    GtkTextIter end = start;
    gtk_text_iter_set_line_offset(&start, left);
    gtk_text_iter_set_line_offset(&end, right);
    gtk_text_buffer_apply_tag(editor->buffer, tag, &start, &end);
}

static DocumentLine *
index_line(CwEditor *editor, GSequenceIter *before, gint number)
{
    GtkTextIter start;
    gtk_text_buffer_get_iter_at_line(editor->buffer, &start, number);
    GtkTextIter end = line_end(start);
    gchar *text = gtk_text_buffer_get_text(editor->buffer, &start, &end, TRUE);
    DocumentLine *line = g_new0(DocumentLine, 1);
    line->position = g_sequence_insert_before(before, line);

    if (cw_crumb_parse(text, number == 0, &line->crumb)) {
        line->words = cw_crumb_words(text, number == 0);
        index_number(editor, line);
        style_span(editor, editor->header_tag, start,
                   line->crumb.header_start, line->crumb.header_end);
        style_span(editor, editor->title_tag, start,
                   line->crumb.title_start, line->crumb.title_end);
    }
    g_free(text);
    return line;
}

static const gchar *
display_title(const CwCrumb *crumb)
{
    return crumb->title && *crumb->title ? crumb->title : "(Untitled)";
}

static void
refresh_visit(CwEditor *editor, guint64 number)
{
    GtkTreeIter *row = g_hash_table_lookup(editor->visits, &number);
    if (!row)
        return;
    DocumentLine *line = numbered_line(editor, number);
    if (line)
        gtk_list_store_set(editor->history_store, row, HISTORY_VISIBLE, TRUE,
                           HISTORY_TITLE, display_title(&line->crumb), -1);
    else
        gtk_list_store_set(editor->history_store, row, HISTORY_VISIBLE, FALSE, -1);
}

static void
visit(CwEditor *editor, DocumentLine *line)
{
    guint64 number = line->crumb.number;
    if (editor->active_number_set && editor->active_number == number)
        return;

    GtkTreeIter *row = g_hash_table_lookup(editor->visits, &number);
    if (row) {
        gtk_list_store_move_after(editor->history_store, row, NULL);
        gtk_list_store_set(editor->history_store, row,
                           HISTORY_TITLE, display_title(&line->crumb), HISTORY_VISIBLE, TRUE, -1);
    } else {
        gchar *digits = g_strdup_printf("%" G_GUINT64_FORMAT, number);
        row = g_new(GtkTreeIter, 1);
        gtk_list_store_insert_with_values(editor->history_store, row, 0,
                                          HISTORY_ID, number, HISTORY_NUMBER, digits,
                                          HISTORY_TITLE, display_title(&line->crumb),
                                          HISTORY_VISIBLE, TRUE, -1);
        g_hash_table_insert(editor->visits, number_key(number), row);
        g_free(digits);
    }
    editor->active_number = number;
    editor->active_number_set = TRUE;
}

static void
set_metric(GtkWidget *label, guint value)
{
    gchar digits[32];
    g_snprintf(digits, sizeof digits, "%u", value);
    gtk_label_set_text(GTK_LABEL(label), digits);
}

static void
refresh_sidebar(CwEditor *editor, gboolean force)
{
    if (editor->editing || editor->loading)
        return;
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(editor->buffer, &cursor,
                                     gtk_text_buffer_get_insert(editor->buffer));
    gint number = gtk_text_iter_get_line(&cursor);
    if (!force && number == editor->shown_line)
        return;
    editor->shown_line = number;
    GSequenceIter *position = g_sequence_get_iter_at_pos(editor->lines, number);
    DocumentLine *line = g_sequence_iter_is_end(position) ? NULL : g_sequence_get(position);

    if (line && line->crumb.valid) {
        gchar *header = g_strdup_printf("%s %" G_GUINT64_FORMAT,
                                        line->crumb.label, line->crumb.number);
        gtk_label_set_text(GTK_LABEL(editor->current_number), header);
        gtk_widget_set_tooltip_text(editor->current_number, header);
        g_free(header);
        gtk_label_set_text(GTK_LABEL(editor->current_title), display_title(&line->crumb));
        gtk_widget_set_tooltip_text(editor->current_title, display_title(&line->crumb));
        set_metric(editor->words, line->words);
        visit(editor, line);
    } else {
        gtk_label_set_text(GTK_LABEL(editor->current_number), "—");
        gtk_widget_set_tooltip_text(editor->current_number, NULL);
        gtk_label_set_text(GTK_LABEL(editor->current_title), "No crumb at cursor");
        gtk_widget_set_tooltip_text(editor->current_title, NULL);
        gtk_label_set_text(GTK_LABEL(editor->words), "—");
        editor->active_number_set = FALSE;
    }
    set_metric(editor->new_crumbs, editor->new_count);
}

static void
cursor_moved(GtkTextBuffer *buffer, GtkTextIter *location, GtkTextMark *mark,
             CwEditor *editor)
{
    (void)location;
    if (mark == gtk_text_buffer_get_insert(buffer))
        refresh_sidebar(editor, FALSE);
}

static void
edit_started(CwEditor *editor, GtkTextIter *first, GtkTextIter *last)
{
    if (!editor->loading) {
        editor->editing = TRUE;
        editor->edit_first = gtk_text_iter_get_line(first);
        editor->edit_last = gtk_text_iter_get_line(last);
    }
}

static void
before_insert(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint length,
              CwEditor *editor)
{
    (void)buffer;
    (void)text;
    (void)length;
    edit_started(editor, location, location);
}

static void
before_delete(GtkTextBuffer *buffer, GtkTextIter *first, GtkTextIter *last,
              CwEditor *editor)
{
    (void)buffer;
    edit_started(editor, first, last);
}

static void
note_visited_number(CwEditor *editor, GHashTable *changed, DocumentLine *line)
{
    if (line->crumb.valid && g_hash_table_contains(editor->visits, &line->crumb.number))
        g_hash_table_add(changed, number_key(line->crumb.number));
}

static void
edit_finished(CwEditor *editor)
{
    if (editor->loading)
        return;

    /* Unaffected records retain their positions across arbitrary insertions, deletes,
     * undo and redo. Restyle just the physical lines touched by this edit. */
    gint total = gtk_text_buffer_get_line_count(editor->buffer);
    gint new_last = editor->edit_last + total - g_sequence_get_length(editor->lines);
    GSequenceIter *first = g_sequence_get_iter_at_pos(editor->lines, editor->edit_first);
    GSequenceIter *after = g_sequence_get_iter_at_pos(editor->lines, editor->edit_last + 1);
    GHashTable *changed = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
    for (GSequenceIter *item = first; item != after; item = g_sequence_iter_next(item)) {
        DocumentLine *line = g_sequence_get(item);
        if (line->crumb.valid) {
            note_visited_number(editor, changed, line);
            unindex_number(editor, line);
        }
    }
    g_sequence_remove_range(first, after);

    GtkTextIter start, end;
    gtk_text_buffer_get_iter_at_line(editor->buffer, &start, editor->edit_first);
    if (new_last + 1 < total)
        gtk_text_buffer_get_iter_at_line(editor->buffer, &end, new_last + 1);
    else
        gtk_text_buffer_get_end_iter(editor->buffer, &end);
    gtk_text_buffer_remove_tag(editor->buffer, editor->header_tag, &start, &end);
    gtk_text_buffer_remove_tag(editor->buffer, editor->title_tag, &start, &end);

    for (gint n = editor->edit_first; n <= new_last; n++) {
        DocumentLine *line = index_line(editor, after, n);
        note_visited_number(editor, changed, line);
    }
    GHashTableIter visits;
    gpointer key;
    g_hash_table_iter_init(&visits, changed);
    while (g_hash_table_iter_next(&visits, &key, NULL))
        refresh_visit(editor, *(guint64 *)key);
    g_hash_table_unref(changed);
    editor->editing = FALSE;
    refresh_sidebar(editor, TRUE);
}

static void
after_insert(GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint length,
             CwEditor *editor)
{
    (void)buffer;
    (void)location;
    (void)text;
    (void)length;
    edit_finished(editor);
}

static void
after_delete(GtkTextBuffer *buffer, GtkTextIter *first, GtkTextIter *last,
             CwEditor *editor)
{
    (void)buffer;
    (void)first;
    (void)last;
    edit_finished(editor);
}

static void
go_to(CwEditor *editor, gint line, gint column)
{
    GtkTextIter location;
    gtk_text_buffer_get_iter_at_line_offset(editor->buffer, &location, line, column);
    gtk_text_buffer_place_cursor(editor->buffer, &location);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(editor->view),
                                       gtk_text_buffer_get_insert(editor->buffer));
}

/* Select the nearest field strictly before/after the caret, including the
 * header of the first record after an optional work-title prefix. */
static void
navigate(CwEditor *editor, gboolean forward, gboolean fields)
{
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(editor->buffer, &cursor,
                                     gtk_text_buffer_get_insert(editor->buffer));
    gint current = gtk_text_iter_get_line(&cursor);
    gint column = gtk_text_iter_get_line_offset(&cursor);
    gint limit = g_sequence_get_length(editor->lines);
    gint step = forward ? 1 : -1;
    gint n = fields ? current : current + step;
    for (; n >= 0 && n < limit; n += step) {
        DocumentLine *line = g_sequence_get(g_sequence_get_iter_at_pos(editor->lines, n));
        if (!line->crumb.valid)
            continue;
        gint stops[] = { line->crumb.header_start, line->crumb.title_start, line->crumb.body_start };
        if (!fields) {
            go_to(editor, n, stops[0]);
            return;
        }
        for (gint i = forward ? 0 : 2; i >= 0 && i < 3; i += step) {
            if (stops[i] >= 0 && (n != current ||
                (forward ? stops[i] > column : stops[i] < column))) {
                go_to(editor, n, stops[i]);
                return;
            }
        }
    }
}

static gboolean
editor_key(GtkWidget *view, GdkEventKey *event, CwEditor *editor)
{
    (void)view;
    GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
    if (event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_ISO_Left_Tab ||
        event->keyval == GDK_KEY_KP_Tab) {
        if (mods == 0 || mods == GDK_SHIFT_MASK) {
            navigate(editor, mods == 0 && event->keyval != GDK_KEY_ISO_Left_Tab, TRUE);
            return TRUE;
        }
    }
    if (mods == GDK_CONTROL_MASK) {
        if (event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up) {
            navigate(editor, FALSE, FALSE);
            return TRUE;
        }
        if (event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down) {
            navigate(editor, TRUE, FALSE);
            return TRUE;
        }
    }
    return FALSE;
}

static void
history_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column,
                  CwEditor *editor)
{
    (void)column;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(model, &row, path))
        return;
    guint64 number;
    gtk_tree_model_get(model, &row, HISTORY_ID, &number, -1);
    DocumentLine *line = numbered_line(editor, number);
    if (line) {
        gtk_widget_grab_focus(GTK_WIDGET(editor->view));
        go_to(editor, g_sequence_iter_get_position(line->position), line->crumb.header_start);
    }
}

static gboolean
history_tooltip(GtkWidget *widget, gint x, gint y, gboolean keyboard,
                GtkTooltip *tooltip, gpointer data)
{
    (void)data;
    GtkTreeView *view = GTK_TREE_VIEW(widget);
    GtkTreeModel *model;
    GtkTreeIter row;
    GtkTreePath *path;
    if (!gtk_tree_view_get_tooltip_context(view, &x, &y, keyboard, &model, &path, &row))
        return FALSE;
    gchar *title;
    gtk_tree_model_get(model, &row, HISTORY_TITLE, &title, -1);
    gtk_tooltip_set_text(tooltip, title);
    gtk_tree_view_set_tooltip_row(view, tooltip, path);
    g_free(title);
    gtk_tree_path_free(path);
    return TRUE;
}

static GtkWidget *
sidebar_label(const gchar *text, const gchar *style)
{
    GtkWidget *widget = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(widget), 0);
    if (style)
        gtk_style_context_add_class(gtk_widget_get_style_context(widget), style);
    return widget;
}

static GtkWidget *
add_metric(GtkWidget *row, const gchar *title, const gchar *name)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget *value = sidebar_label("0", "metric-number");
    gtk_widget_set_name(value, name);
    atk_object_set_name(gtk_widget_get_accessible(value), title);
    gtk_box_pack_start(GTK_BOX(box), value, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), sidebar_label(title, "subtle"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), box, TRUE, TRUE, 0);
    return value;
}

static GtkWidget *
build_sidebar(CwEditor *editor)
{
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_style_context_add_class(gtk_widget_get_style_context(sidebar), "sidebar");
    gtk_widget_set_size_request(sidebar, 200, -1);
    GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 9);
    gtk_container_set_border_width(GTK_CONTAINER(content), 16);
    gtk_box_pack_start(GTK_BOX(sidebar), content, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(content), sidebar_label("Current crumb", "section-label"),
                       FALSE, FALSE, 0);

    editor->current_number = sidebar_label("—", "crumb-number");
    gtk_widget_set_name(editor->current_number, "crumb-number");
    gtk_label_set_ellipsize(GTK_LABEL(editor->current_number), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(content), editor->current_number, FALSE, FALSE, 0);

    editor->current_title = sidebar_label("", NULL);
    gtk_widget_set_name(editor->current_title, "crumb-title");
    gtk_label_set_line_wrap(GTK_LABEL(editor->current_title), TRUE);
    gtk_label_set_line_wrap_mode(GTK_LABEL(editor->current_title), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_ellipsize(GTK_LABEL(editor->current_title), PANGO_ELLIPSIZE_END);
    gtk_label_set_lines(GTK_LABEL(editor->current_title), 4);
    gtk_label_set_max_width_chars(GTK_LABEL(editor->current_title), 26);
    gtk_box_pack_start(GTK_BOX(content), editor->current_title, FALSE, FALSE, 0);

    GtkWidget *metrics = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(metrics, 8);
    gtk_widget_set_margin_bottom(metrics, 8);
    editor->words = add_metric(metrics, "Words", "crumb-words");
    editor->new_crumbs = add_metric(metrics, "New this session", "session-crumbs");
    gtk_box_pack_start(GTK_BOX(content), metrics, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(content), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                       FALSE, FALSE, 0);
    GtkWidget *heading = sidebar_label("Visit history", "section-label");
    gtk_box_pack_start(GTK_BOX(content), heading, FALSE, FALSE, 0);

    editor->history_store = gtk_list_store_new(HISTORY_COLUMNS, G_TYPE_UINT64, G_TYPE_STRING,
                                                G_TYPE_STRING, G_TYPE_BOOLEAN);
    GtkTreeModel *visible = gtk_tree_model_filter_new(GTK_TREE_MODEL(editor->history_store), NULL);
    gtk_tree_model_filter_set_visible_column(GTK_TREE_MODEL_FILTER(visible), HISTORY_VISIBLE);
    GtkWidget *tree = gtk_tree_view_new_with_model(visible);
    g_object_unref(visible);
    editor->history = GTK_TREE_VIEW(g_object_ref(tree));
    gtk_widget_set_name(tree, "crumb-history");
    atk_object_set_name(gtk_widget_get_accessible(tree), "Crumb visit history");
    gtk_tree_view_set_headers_visible(editor->history, FALSE);
    gtk_tree_view_set_activate_on_single_click(editor->history, TRUE);
    gtk_tree_view_set_search_column(editor->history, HISTORY_TITLE);
    gtk_widget_set_has_tooltip(tree, TRUE);
    g_signal_connect(tree, "query-tooltip", G_CALLBACK(history_tooltip), NULL);
    GtkCellRenderer *id = gtk_cell_renderer_text_new();
    g_object_set(id, "weight", PANGO_WEIGHT_BOLD, "xpad", 4, "ypad", 5, NULL);
    GtkTreeViewColumn *numbers = gtk_tree_view_column_new_with_attributes(
        "Number", id, "text", HISTORY_NUMBER, NULL);
    gtk_tree_view_append_column(editor->history, numbers);
    GtkCellRenderer *title = gtk_cell_renderer_text_new();
    g_object_set(title, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 8, NULL);
    GtkTreeViewColumn *titles = gtk_tree_view_column_new_with_attributes(
        "Title", title, "text", HISTORY_TITLE, NULL);
    gtk_tree_view_column_set_expand(titles, TRUE);
    gtk_tree_view_append_column(editor->history, titles);
    g_signal_connect(tree, "row-activated", G_CALLBACK(history_activated), editor);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_box_pack_start(GTK_BOX(content), scroll, TRUE, TRUE, 0);
    return sidebar;
}

CwEditor *
cw_editor_new(GtkSourceView *view)
{
    CwEditor *editor = g_new0(CwEditor, 1);
    editor->view = g_object_ref(view);
    editor->buffer = g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)));
    editor->lines = g_sequence_new(free_line);
    editor->numbers = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free,
                                            (GDestroyNotify)g_ptr_array_unref);
    editor->opening_ids = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, NULL);
    editor->visits = g_hash_table_new_full(g_int64_hash, g_int64_equal, g_free, g_free);
    editor->sidebar = g_object_ref_sink(build_sidebar(editor));
    editor->header_tag = gtk_text_buffer_create_tag(editor->buffer, "crumb-header",
                                                     "style", PANGO_STYLE_ITALIC, NULL);
    editor->title_tag = gtk_text_buffer_create_tag(editor->buffer, "crumb-title",
                                                    "weight", PANGO_WEIGHT_BOLD, NULL);

    editor->loading = TRUE;
    for (gint n = 0; n < gtk_text_buffer_get_line_count(editor->buffer); n++)
        index_line(editor, g_sequence_get_end_iter(editor->lines), n);
    editor->loading = FALSE;
    editor->shown_line = -1;
    refresh_sidebar(editor, TRUE);

    g_signal_connect(editor->buffer, "mark-set", G_CALLBACK(cursor_moved), editor);
    g_signal_connect(editor->buffer, "insert-text", G_CALLBACK(before_insert), editor);
    g_signal_connect_after(editor->buffer, "insert-text", G_CALLBACK(after_insert), editor);
    g_signal_connect(editor->buffer, "delete-range", G_CALLBACK(before_delete), editor);
    g_signal_connect_after(editor->buffer, "delete-range", G_CALLBACK(after_delete), editor);
    g_signal_connect(view, "key-press-event", G_CALLBACK(editor_key), editor);
    return editor;
}

GtkWidget *
cw_editor_sidebar(CwEditor *editor)
{
    return editor->sidebar;
}

void
cw_editor_load_at(CwEditor *editor, const gchar *text, gint line, gint column)
{
    editor->loading = TRUE;
    editor->editing = FALSE;
    editor->new_count = 0;
    editor->active_number_set = FALSE;
    editor->shown_line = -1;
    gtk_list_store_clear(editor->history_store);
    g_hash_table_remove_all(editor->visits);
    g_hash_table_remove_all(editor->opening_ids);
    g_hash_table_remove_all(editor->numbers);
    g_sequence_remove_range(g_sequence_get_begin_iter(editor->lines),
                            g_sequence_get_end_iter(editor->lines));

    GtkSourceBuffer *buffer = GTK_SOURCE_BUFFER(editor->buffer);
    gtk_source_buffer_begin_not_undoable_action(buffer);
    gtk_text_buffer_set_text(editor->buffer, text, -1);
    gtk_source_buffer_end_not_undoable_action(buffer);
    gint total = gtk_text_buffer_get_line_count(editor->buffer);
    gint last_crumb = -1;
    for (gint n = 0; n < total; n++) {
        DocumentLine *record = index_line(editor, g_sequence_get_end_iter(editor->lines), n);
        if (record->crumb.valid)
            last_crumb = n;
    }
    if (line < 0 || line >= total) {
        line = last_crumb >= 0 ? last_crumb : total - 1;
        column = G_MAXINT;
    }
    GtkTextIter position;
    gtk_text_buffer_get_iter_at_line(editor->buffer, &position, line);
    GtkTextIter end = line_end(position);
    column = CLAMP(column, 0, gtk_text_iter_get_line_offset(&end));
    go_to(editor, line, column);
    gtk_text_buffer_set_modified(editor->buffer, FALSE);
    editor->loading = FALSE;
    refresh_sidebar(editor, TRUE);
}

void
cw_editor_load(CwEditor *editor, const gchar *text)
{
    cw_editor_load_at(editor, text, 0, 0);
}

static gboolean
immediately_after_work_title(GtkTextIter position)
{
    return gtk_text_iter_get_line(&position) == 0 &&
           gtk_text_iter_backward_char(&position) && gtk_text_iter_get_char(&position) == '/' &&
           gtk_text_iter_backward_char(&position) && gtk_text_iter_get_char(&position) == '/';
}

void
cw_editor_append(CwEditor *editor)
{
    const gchar *label = "Crumb";
    guint64 largest = 0;
    GSequenceIter *end = g_sequence_get_end_iter(editor->lines);
    for (GSequenceIter *it = g_sequence_get_begin_iter(editor->lines); it != end;
         it = g_sequence_iter_next(it)) {
        DocumentLine *line = g_sequence_get(it);
        if (line->crumb.valid) {
            label = line->crumb.label;
            largest = MAX(largest, line->crumb.number);
        }
    }
    if (largest == G_MAXUINT64) {
        gtk_widget_error_bell(GTK_WIDGET(editor->view));
        return;
    }

    /* Insert before the existing line terminators. Both a final LF and its
     * absence survive appending; extra blank lines in a draft survive as well. */
    GtkTextIter here;
    gtk_text_buffer_get_end_iter(editor->buffer, &here);
    GtkTextIter previous = here;
    while (gtk_text_iter_backward_char(&previous)) {
        gunichar c = gtk_text_iter_get_char(&previous);
        if (c != '\n' && c != '\r')
            break;
        here = previous;
    }
    gboolean first = gtk_text_iter_is_start(&here) ||
                     (largest == 0 && immediately_after_work_title(here));
    const gchar *separator = first ? "" : "\n";
    gchar *snippet = g_strdup_printf("%s%s %" G_GUINT64_FORMAT ": : ",
                                     separator, label, largest + 1);
    gtk_widget_grab_focus(GTK_WIDGET(editor->view));
    gtk_text_buffer_begin_user_action(editor->buffer);
    gtk_text_buffer_insert(editor->buffer, &here, snippet, -1);
    gtk_text_buffer_place_cursor(editor->buffer, &here);
    gtk_text_buffer_end_user_action(editor->buffer);
    gtk_text_view_scroll_mark_onscreen(GTK_TEXT_VIEW(editor->view),
                                       gtk_text_buffer_get_insert(editor->buffer));
    g_free(snippet);
}

void
cw_editor_focus_history(CwEditor *editor)
{
    GtkTreeModel *model = gtk_tree_view_get_model(editor->history);
    GtkTreeIter first;
    gtk_widget_grab_focus(GTK_WIDGET(editor->history));
    if (gtk_tree_model_get_iter_first(model, &first)) {
        GtkTreePath *path = gtk_tree_model_get_path(model, &first);
        gtk_tree_view_set_cursor(editor->history, path, NULL, FALSE);
        gtk_tree_path_free(path);
    }
}

void
cw_editor_free(CwEditor *editor)
{
    if (!editor)
        return;
    g_signal_handlers_disconnect_by_data(editor->view, editor);
    g_signal_handlers_disconnect_by_data(editor->buffer, editor);
    g_signal_handlers_disconnect_by_data(editor->history, editor);
    GtkTextTagTable *tags = gtk_text_buffer_get_tag_table(editor->buffer);
    gtk_text_tag_table_remove(tags, editor->header_tag);
    gtk_text_tag_table_remove(tags, editor->title_tag);
    g_hash_table_unref(editor->visits);
    g_hash_table_unref(editor->numbers);
    g_hash_table_unref(editor->opening_ids);
    g_sequence_free(editor->lines);
    g_object_unref(editor->history);
    g_object_unref(editor->history_store);
    g_object_unref(editor->sidebar);
    g_object_unref(editor->buffer);
    g_object_unref(editor->view);
    g_free(editor);
}

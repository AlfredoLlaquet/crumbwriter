#include "editor.h"
#include "reveal.h"

#include "crumb.h"
#include "genres.h"

#define WARNING_SPAN "<span foreground='" CW_NUMBER_WARNING_COLOR "' weight='bold'>"

typedef struct {
    gchar *key;
    GSequence *occurrences;
} Genre;

typedef struct {
    Genre *genre;
    gchar *name;
    guint64 ordinal;
    GSequenceIter *line_position;
    guint component;          /* One-based order within a compound title. */
    GSequenceIter *position;
    gint digits_start;
    gint digits_end;
    gboolean overflow;
    gboolean warning;
} Occurrence;

typedef struct {
    CwCrumb crumb;
    GPtrArray *genres;        /* Occurrences owned by this physical line. */
    guint words;
    GSequenceIter *position;
    gboolean header_warning;
} DocumentLine;

enum {
    HISTORY_ID,
    HISTORY_NUMBER,
    HISTORY_TITLE,
    HISTORY_VISIBLE,
    HISTORY_WARNING,
    HISTORY_TITLE_MARKUP,
    HISTORY_COLUMNS
};

struct _CwEditor {
    GtkSourceView *view;
    GtkTextBuffer *buffer;
    GtkTextTag *header_tag;
    GtkTextTag *title_tag;
    GtkTextTag *warning_tag;

    GtkWidget *sidebar;
    GtkWidget *current_number;
    GtkWidget *current_title;
    GtkWidget *words;
    GtkWidget *new_crumbs;
    GtkTreeView *history;
    GtkListStore *history_store;
    CwGenres *genres;

    GSequence *lines;          /* One cached record per physical buffer line. */
    GHashTable *numbers;       /* Number -> array of DocumentLine pointers. */
    GHashTable *opening_ids;   /* Set of distinct numbers in the opened file. */
    GHashTable *visits;        /* Number -> stable GtkListStore iterator. */
    GHashTable *genre_index;   /* Normalized genre name -> Genre. */
    GHashTable *changed_genres;
    GHashTable *check_occurrences;
    guint new_count;
    gint shown_line;
    gboolean active_number_set;
    guint64 active_number;
    gchar *fallback_label;

    gboolean loading;
    gboolean editing;
    gboolean hash_key;
    gboolean typed_hash;
    gint edit_first;
    gint edit_last;
};

static void append_with_title(CwEditor *editor, const gchar *title);
static void refresh_visit(CwEditor *editor, guint64 number);

static guint64 *
number_key(guint64 number)
{
    guint64 *key = g_new(guint64, 1);
    *key = number;
    return key;
}

static void
free_occurrence(gpointer data)
{
    Occurrence *occurrence = data;
    g_free(occurrence->name);
    g_free(occurrence);
}

static void
free_line(gpointer data)
{
    DocumentLine *line = data;
    cw_crumb_clear(&line->crumb);
    g_clear_pointer(&line->genres, g_ptr_array_unref);
    g_free(line);
}

static void
free_genre(gpointer data)
{
    Genre *genre = data;
    g_sequence_free(genre->occurrences);
    g_free(genre->key);
    g_free(genre);
}

static gint
compare_occurrences(gconstpointer a, gconstpointer b, gpointer data)
{
    (void)data;
    const Occurrence *left = a, *right = b;
    gint lines = g_sequence_iter_compare(left->line_position, right->line_position);
    if (lines != 0)
        return lines;
    return (left->component > right->component) - (left->component < right->component);
}

typedef struct {
    CwEditor *editor;
    DocumentLine *line;
} IndexTarget;

/* Only the edited occurrence and its immediately following occurrence can
 * change validity. An unchanged following ordinal establishes the next baseline. */
static void
check_successor(CwEditor *editor, Occurrence *occurrence)
{
    GSequenceIter *next = g_sequence_iter_next(occurrence->position);
    if (!g_sequence_iter_is_end(next))
        g_hash_table_add(editor->check_occurrences, g_sequence_get(next));
}

static void
index_genre(const CwGenreComponent *part, gpointer data)
{
    IndexTarget *target = data;
    CwEditor *editor = target->editor;
    DocumentLine *line = target->line;
    Genre *genre = g_hash_table_lookup(editor->genre_index, part->key);
    if (!genre) {
        genre = g_new0(Genre, 1);
        genre->key = g_strdup(part->key);
        genre->occurrences = g_sequence_new(NULL);
        g_hash_table_insert(editor->genre_index, genre->key, genre);
    }
    if (!line->genres)
        line->genres = g_ptr_array_new_with_free_func(free_occurrence);
    Occurrence *occurrence = g_new0(Occurrence, 1);
    occurrence->genre = genre;
    occurrence->name = g_strdup(part->name);
    occurrence->ordinal = part->ordinal;
    occurrence->overflow = part->overflow;
    occurrence->digits_start = part->digits_start;
    occurrence->digits_end = part->digits_end;
    occurrence->line_position = line->position;
    occurrence->component = line->genres->len + 1;
    occurrence->position = g_sequence_insert_sorted(genre->occurrences, occurrence,
                                                     compare_occurrences, NULL);
    g_ptr_array_add(line->genres, occurrence);
    g_hash_table_add(editor->changed_genres, genre);
    g_hash_table_add(editor->check_occurrences, occurrence);
    check_successor(editor, occurrence);
}

/* Publish a genre once per edit even when a replaced range contains many
 * occurrences. Its latest entry follows physical position, including after undo. */
static void
update_genres(CwEditor *editor)
{
    GHashTableIter iter;
    gpointer item;
    g_hash_table_iter_init(&iter, editor->changed_genres);
    while (g_hash_table_iter_next(&iter, &item, NULL)) {
        Genre *genre = item;
        g_hash_table_iter_remove(&iter);
        GSequenceIter *end = g_sequence_get_end_iter(genre->occurrences);
        if (g_sequence_iter_is_begin(end)) {
            cw_genres_update(editor->genres, genre->key, NULL, 0);
            g_hash_table_remove(editor->genre_index, genre->key);
        } else {
            Occurrence *last = g_sequence_get(g_sequence_iter_prev(end));
            cw_genres_update(editor->genres, genre->key, last->name, last->ordinal);
            cw_genres_set_warning(editor->genres, genre->key, last->warning);
        }
    }
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
        if (!g_hash_table_size(editor->numbers)) {
            g_free(editor->fallback_label);
            editor->fallback_label = g_strdup(line->crumb.label);
        }
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

static void
set_number_warning(CwEditor *editor, gint line, gint start, gint end,
                   gboolean invalid, gboolean *was_invalid)
{
    if (*was_invalid == invalid)
        return;
    *was_invalid = invalid;
    GtkTextIter a, b;
    gtk_text_buffer_get_iter_at_line_offset(editor->buffer, &a, line, start);
    gtk_text_buffer_get_iter_at_line_offset(editor->buffer, &b, line, end);
    if (invalid)
        gtk_text_buffer_apply_tag(editor->buffer, editor->warning_tag, &a, &b);
    else
        gtk_text_buffer_remove_tag(editor->buffer, editor->warning_tag, &a, &b);
}

static void
check_header(CwEditor *editor, DocumentLine *line, gint index)
{
    CwCrumb *crumb = &line->crumb;
    if (crumb->header_end <= crumb->digits_start)
        return;
    gboolean invalid = !crumb->valid || crumb->number != (guint64)index + 1;
    if (line->header_warning != invalid) {
        set_number_warning(editor, index, crumb->digits_start, crumb->header_end,
                           invalid, &line->header_warning);
        if (crumb->valid)
            refresh_visit(editor, crumb->number);
    }
}

static void
check_genre_numbers(CwEditor *editor)
{
    GHashTableIter checks;
    gpointer item;
    g_hash_table_iter_init(&checks, editor->check_occurrences);
    while (g_hash_table_iter_next(&checks, &item, NULL)) {
        Occurrence *occurrence = item;
        g_hash_table_iter_remove(&checks);
        guint64 previous = 0;
        if (!g_sequence_iter_is_begin(occurrence->position)) {
            Occurrence *prior = g_sequence_get(g_sequence_iter_prev(occurrence->position));
            previous = prior->ordinal;
        }
        gboolean invalid = occurrence->overflow || previous == G_MAXUINT64 ||
                           occurrence->ordinal != previous + 1;
        if (occurrence->warning == invalid)
            continue;
        DocumentLine *line = g_sequence_get(occurrence->line_position);
        gint index = g_sequence_iter_get_position(occurrence->line_position);
        set_number_warning(editor, index, line->crumb.title_start + occurrence->digits_start,
                           line->crumb.title_start + occurrence->digits_end,
                           invalid, &occurrence->warning);
        g_hash_table_add(editor->changed_genres, occurrence->genre);
        refresh_visit(editor, line->crumb.number);
    }
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
        IndexTarget target = { editor, line };
        cw_genre_foreach(line->crumb.title, index_genre, &target);
        index_number(editor, line);
        style_span(editor, editor->header_tag, start,
                   line->crumb.header_start, line->crumb.header_end);
        style_span(editor, editor->title_tag, start,
                   line->crumb.title_start, line->crumb.title_end);
    }
    check_header(editor, line, number);
    g_free(text);
    return line;
}

static const gchar *
display_title(const CwCrumb *crumb)
{
    return crumb->title && *crumb->title ? crumb->title : "(Untitled)";
}

static gchar *
title_markup(DocumentLine *line)
{
    const gchar *title = display_title(&line->crumb);
    const gchar *from = title;
    GString *markup = g_string_new(NULL);
    for (guint i = 0; line->genres && i < line->genres->len; i++) {
        Occurrence *occurrence = g_ptr_array_index(line->genres, i);
        if (!occurrence->warning)
            continue;
        const gchar *digits = g_utf8_offset_to_pointer(title, occurrence->digits_start);
        const gchar *end = digits + (occurrence->digits_end - occurrence->digits_start);
        g_autofree gchar *escaped = g_markup_escape_text(from, digits - from);
        g_string_append(markup, escaped);
        g_string_append(markup, WARNING_SPAN);
        g_string_append_len(markup, digits, end - digits); /* Decimal ASCII digits. */
        g_string_append(markup, "</span>");
        from = end;
    }
    g_autofree gchar *tail = g_markup_escape_text(from, -1);
    g_string_append(markup, tail);
    return g_string_free(markup, FALSE);
}

static void
update_visit(CwEditor *editor, GtkTreeIter *row, DocumentLine *line)
{
    g_autofree gchar *markup = title_markup(line);
    gtk_list_store_set(editor->history_store, row, HISTORY_VISIBLE, TRUE,
                       HISTORY_WARNING, line->header_warning,
                       HISTORY_TITLE, display_title(&line->crumb), HISTORY_TITLE_MARKUP, markup, -1);
}

static void
refresh_visit(CwEditor *editor, guint64 number)
{
    GtkTreeIter *row = g_hash_table_lookup(editor->visits, &number);
    if (!row)
        return;
    DocumentLine *line = numbered_line(editor, number);
    if (line)
        update_visit(editor, row, line);
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
    } else {
        gchar *digits = g_strdup_printf("%" G_GUINT64_FORMAT, number);
        row = g_new(GtkTreeIter, 1);
        gtk_list_store_insert_with_values(editor->history_store, row, 0,
                                          HISTORY_ID, number, HISTORY_NUMBER, digits, -1);
        g_hash_table_insert(editor->visits, number_key(number), row);
        g_free(digits);
    }
    update_visit(editor, row, line);
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
        if (line->header_warning) {
            g_autofree gchar *markup = g_markup_printf_escaped("%s " WARNING_SPAN
                "%" G_GUINT64_FORMAT "</span>", line->crumb.label, line->crumb.number);
            gtk_label_set_markup(GTK_LABEL(editor->current_number), markup);
        } else {
            gtk_label_set_text(GTK_LABEL(editor->current_number), header);
        }
        gtk_widget_set_tooltip_text(editor->current_number, header);
        g_free(header);
        g_autofree gchar *title = title_markup(line);
        gtk_label_set_markup(GTK_LABEL(editor->current_title), title);
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
    if (editor->hash_key && !editor->loading && length == 1 && text[0] == '#') {
        GtkTextIter cursor;
        gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
        editor->typed_hash = gtk_text_iter_equal(location, &cursor);
    }
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
            if (line->genres) {
                for (guint i = 0; i < line->genres->len; i++) {
                    Occurrence *occurrence = g_ptr_array_index(line->genres, i);
                    g_hash_table_add(editor->changed_genres, occurrence->genre);
                    check_successor(editor, occurrence);
                    g_hash_table_remove(editor->check_occurrences, occurrence);
                    g_sequence_remove(occurrence->position);
                }
            }
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
    gtk_text_buffer_remove_tag(editor->buffer, editor->warning_tag, &start, &end);

    for (gint n = editor->edit_first; n <= new_last; n++) {
        DocumentLine *line = index_line(editor, after, n);
        note_visited_number(editor, changed, line);
    }
    if (new_last != editor->edit_last) {
        gint n = new_last + 1;
        for (GSequenceIter *it = after; !g_sequence_iter_is_end(it); it = g_sequence_iter_next(it))
            check_header(editor, g_sequence_get(it), n++);
    }
    check_genre_numbers(editor);
    GHashTableIter visits;
    gpointer key;
    g_hash_table_iter_init(&visits, changed);
    while (g_hash_table_iter_next(&visits, &key, NULL))
        refresh_visit(editor, *(guint64 *)key);
    g_hash_table_unref(changed);
    update_genres(editor);
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
    cw_reveal_cursor(GTK_TEXT_VIEW(editor->view));
}

/* A terminal line delimiter belongs to the physical line that precedes it. */
static GtkTextIter
document_end(CwEditor *editor)
{
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(editor->buffer, &end);
    if (gtk_text_iter_get_line(&end) > 0 && gtk_text_iter_starts_line(&end)) {
        gtk_text_iter_backward_line(&end);
        end = line_end(end);
    }
    return end;
}

void
cw_editor_go_to_end(CwEditor *editor)
{
    GtkTextIter end = document_end(editor);
    gtk_widget_grab_focus(GTK_WIDGET(editor->view));
    gtk_text_buffer_place_cursor(editor->buffer, &end);
    cw_reveal_cursor(GTK_TEXT_VIEW(editor->view));
}

static DocumentLine *
current_record(CwEditor *editor, GtkTextIter *cursor)
{
    gtk_text_buffer_get_iter_at_mark(editor->buffer, cursor, gtk_text_buffer_get_insert(editor->buffer));
    gint n = gtk_text_iter_get_line(cursor);
    DocumentLine *line = g_sequence_get(g_sequence_get_iter_at_pos(editor->lines, n));
    return line->crumb.header_end > line->crumb.digits_start ? line : NULL;
}

gboolean
cw_editor_copy_crumb(CwEditor *editor)
{
    GtkTextIter start;
    DocumentLine *line = current_record(editor, &start);
    if (!line)
        return FALSE;
    gtk_text_iter_set_line_offset(&start, line->crumb.record_start);
    GtkTextIter end = line_end(start);
    g_autofree gchar *text = gtk_text_buffer_get_text(editor->buffer, &start, &end, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(editor->view));
    gtk_text_buffer_select_range(editor->buffer, &end, &start);
    GtkClipboard *clipboard = gtk_widget_get_clipboard(GTK_WIDGET(editor->view), GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text(clipboard, text, -1);
    cw_reveal_cursor(GTK_TEXT_VIEW(editor->view));
    return TRUE;
}

gboolean
cw_editor_delete_crumb(CwEditor *editor)
{
    GtkTextIter start;
    DocumentLine *line = current_record(editor, &start);
    if (!line || !gtk_text_view_get_editable(GTK_TEXT_VIEW(editor->view)))
        return FALSE;
    gint n = gtk_text_iter_get_line(&start);
    /* Keep the original gap after the work-title separator on the first line. */
    gtk_text_iter_set_line_offset(&start, line->crumb.record_start ? line->crumb.header_start : 0);
    GtkTextIter end = line_end(start);
    if (!gtk_text_iter_is_end(&end)) {
        GtkTextIter next = end;
        gtk_text_iter_forward_line(&next);
        if (n > 0 || !gtk_text_iter_is_end(&next))
            end = next;
    } else if (n > 0) {
        /* An unterminated final line takes the preceding delimiter with it. */
        gtk_text_buffer_get_iter_at_line(editor->buffer, &start, n - 1);
        start = line_end(start);
    }
    gtk_widget_grab_focus(GTK_WIDGET(editor->view));
    gtk_text_buffer_begin_user_action(editor->buffer);
    gtk_text_buffer_delete(editor->buffer, &start, &end);
    if (gtk_text_iter_is_end(&start))
        start = document_end(editor);
    gtk_text_buffer_place_cursor(editor->buffer, &start);
    gtk_text_buffer_end_user_action(editor->buffer);
    cw_reveal_cursor(GTK_TEXT_VIEW(editor->view));
    return TRUE;
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

static GtkTextIter
crumb_edge(CwEditor *editor, gboolean at_end)
{
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(editor->buffer, &cursor, gtk_text_buffer_get_insert(editor->buffer));
    if (gtk_text_iter_is_end(&cursor) && gtk_text_iter_starts_line(&cursor)) {
        /* From the empty position after a terminal delimiter, return to the
         * last content line, including an unfinished draft. */
        while (gtk_text_iter_starts_line(&cursor) && !gtk_text_iter_is_start(&cursor)) {
            gtk_text_iter_backward_line(&cursor);
            cursor = line_end(cursor);
        }
    }
    if (at_end)
        return line_end(cursor);
    gint n = gtk_text_iter_get_line(&cursor);
    DocumentLine *line = g_sequence_get(g_sequence_get_iter_at_pos(editor->lines, n));
    /* Unrecognized lines have header_start zero. */
    gtk_text_iter_set_line_offset(&cursor, line->crumb.header_start);
    return cursor;
}

static gboolean
editor_key(GtkWidget *view, GdkEventKey *event, CwEditor *editor)
{
    /* Leave the key to GTK's input method, including Shift and AltGr layouts.
     * A command or composition that never inserts # cannot complete a title. */
    if (event->keyval == GDK_KEY_numbersign && !editor->hash_key &&
        gtk_text_view_get_editable(GTK_TEXT_VIEW(view))) {
        editor->hash_key = TRUE;
        editor->typed_hash = FALSE;
        gtk_text_buffer_begin_user_action(editor->buffer);
    }
    GdkModifierType mods = event->state & gtk_accelerator_get_default_mod_mask();
    if (event->keyval == GDK_KEY_Tab || event->keyval == GDK_KEY_ISO_Left_Tab ||
        event->keyval == GDK_KEY_KP_Tab) {
        if (mods == 0 || mods == GDK_SHIFT_MASK) {
            navigate(editor, mods == 0 && event->keyval != GDK_KEY_ISO_Left_Tab, TRUE);
            return TRUE;
        }
    }
    if (mods != GDK_CONTROL_MASK)
        return FALSE;
    GtkTextIter target;
    switch (event->keyval) {
    case GDK_KEY_Up: case GDK_KEY_KP_Up:
        navigate(editor, FALSE, FALSE);
        return TRUE;
    case GDK_KEY_Down: case GDK_KEY_KP_Down:
        navigate(editor, TRUE, FALSE);
        return TRUE;
    case GDK_KEY_Home: case GDK_KEY_Begin:
    case GDK_KEY_KP_Home: case GDK_KEY_KP_Begin:
        target = crumb_edge(editor, FALSE);
        break;
    case GDK_KEY_End: case GDK_KEY_KP_End:
        target = crumb_edge(editor, TRUE);
        break;
    case GDK_KEY_Page_Down: case GDK_KEY_KP_Page_Down: /* Next */
        gtk_text_buffer_get_start_iter(editor->buffer, &target);
        break;
    case GDK_KEY_Page_Up: case GDK_KEY_KP_Page_Up: /* Prior */
        gtk_text_buffer_get_end_iter(editor->buffer, &target);
        break;
    default:
        return FALSE;
    }
    gtk_text_buffer_place_cursor(editor->buffer, &target);
    cw_reveal_cursor(GTK_TEXT_VIEW(editor->view));
    return TRUE;
}

static void
complete_genre(CwEditor *editor)
{
    GtkTextIter cursor;
    gtk_text_buffer_get_iter_at_mark(editor->buffer, &cursor, gtk_text_buffer_get_insert(editor->buffer));
    GSequenceIter *it = g_sequence_get_iter_at_pos(editor->lines, gtk_text_iter_get_line(&cursor));
    DocumentLine *current = g_sequence_get(it);
    gint column = gtk_text_iter_get_line_offset(&cursor);
    if (!current->crumb.valid || column <= current->crumb.title_start ||
        column > current->crumb.title_end)
        return;
    const gchar *hash = g_utf8_offset_to_pointer(current->crumb.title,
                                                 column - current->crumb.title_start - 1);
    g_autofree gchar *key = cw_genre_key_at(current->crumb.title, hash);
    Genre *genre = key ? g_hash_table_lookup(editor->genre_index, key) : NULL;
    if (!genre)
        return;
    /* Component zero precedes every numbered component of the current line. */
    Occurrence here = { .line_position = it };
    GSequenceIter *next = g_sequence_search(genre->occurrences, &here, compare_occurrences, NULL);
    if (g_sequence_iter_is_begin(next))
        return;
    Occurrence *previous = g_sequence_get(g_sequence_iter_prev(next));
    if (previous->ordinal < G_MAXUINT64) {
        gchar digits[32];
        g_snprintf(digits, sizeof digits, "%" G_GUINT64_FORMAT, previous->ordinal + 1);
        gtk_text_buffer_insert_at_cursor(editor->buffer, digits, -1);
    }
}

static void
after_key(GtkWidget *view, GdkEvent *event, CwEditor *editor)
{
    (void)view;
    if (event->type == GDK_KEY_PRESS && editor->hash_key) {
        editor->hash_key = FALSE;
        if (editor->typed_hash)
            complete_genre(editor);
        gtk_text_buffer_end_user_action(editor->buffer);
    }
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

static void
genre_activated(const gchar *name, guint64 ordinal, gpointer data)
{
    CwEditor *editor = data;
    if (ordinal == G_MAXUINT64) {
        gtk_widget_error_bell(GTK_WIDGET(editor->view));
        return;
    }
    g_autofree gchar *title = g_strdup_printf("%s #%" G_GUINT64_FORMAT, name, ordinal + 1);
    append_with_title(editor, title);
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
    GtkWidget *lists = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(content), lists, TRUE, TRUE, 0);
    GtkWidget *history_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_paned_pack1(GTK_PANED(lists), history_box, TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(history_box), sidebar_label("Visit history", "section-label"),
                       FALSE, FALSE, 0);

    editor->history_store = gtk_list_store_new(HISTORY_COLUMNS, G_TYPE_UINT64, G_TYPE_STRING,
                                                G_TYPE_STRING, G_TYPE_BOOLEAN,
                                                G_TYPE_BOOLEAN, G_TYPE_STRING);
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
    g_object_set(id, "weight", PANGO_WEIGHT_BOLD, "xpad", 4, "ypad", 5,
                 "foreground", CW_NUMBER_WARNING_COLOR, NULL);
    GtkTreeViewColumn *numbers = gtk_tree_view_column_new_with_attributes(
        "Number", id, "text", HISTORY_NUMBER, "foreground-set", HISTORY_WARNING, NULL);
    gtk_tree_view_append_column(editor->history, numbers);
    GtkCellRenderer *title = gtk_cell_renderer_text_new();
    g_object_set(title, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 8, NULL);
    GtkTreeViewColumn *titles = gtk_tree_view_column_new_with_attributes(
        "Title", title, "markup", HISTORY_TITLE_MARKUP, NULL);
    gtk_tree_view_column_set_expand(titles, TRUE);
    gtk_tree_view_append_column(editor->history, titles);
    g_signal_connect(tree, "row-activated", G_CALLBACK(history_activated), editor);

    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER,
                                    GTK_POLICY_AUTOMATIC);
    gtk_container_add(GTK_CONTAINER(scroll), tree);
    gtk_widget_set_size_request(scroll, -1, 48);
    gtk_box_pack_start(GTK_BOX(history_box), scroll, TRUE, TRUE, 0);

    GtkWidget *genres_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(genres_box, 8);
    gtk_paned_pack2(GTK_PANED(lists), genres_box, TRUE, FALSE);
    gtk_box_pack_start(GTK_BOX(genres_box), sidebar_label("Genres", "section-label"),
                       FALSE, FALSE, 0);
    editor->genres = cw_genres_new(genre_activated, editor);
    GtkWidget *genre_list = cw_genres_widget(editor->genres);
    gtk_widget_set_size_request(genre_list, -1, 48);
    gtk_box_pack_start(GTK_BOX(genres_box), genre_list, TRUE, TRUE, 0);
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
    editor->genre_index = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, free_genre);
    editor->changed_genres = g_hash_table_new(g_direct_hash, g_direct_equal);
    editor->check_occurrences = g_hash_table_new(g_direct_hash, g_direct_equal);
    editor->sidebar = g_object_ref_sink(build_sidebar(editor));
    editor->header_tag = gtk_text_buffer_create_tag(editor->buffer, "crumb-header",
                                                     "style", PANGO_STYLE_ITALIC, NULL);
    editor->title_tag = gtk_text_buffer_create_tag(editor->buffer, "crumb-title",
                                                    "weight", PANGO_WEIGHT_BOLD, NULL);
    editor->warning_tag = gtk_text_buffer_create_tag(editor->buffer, "crumb-number-warning",
                                                      "foreground", CW_NUMBER_WARNING_COLOR,
                                                      "weight", PANGO_WEIGHT_BOLD, NULL);

    editor->loading = TRUE;
    for (gint n = 0; n < gtk_text_buffer_get_line_count(editor->buffer); n++)
        index_line(editor, g_sequence_get_end_iter(editor->lines), n);
    check_genre_numbers(editor);
    update_genres(editor);
    editor->loading = FALSE;
    editor->shown_line = -1;
    refresh_sidebar(editor, TRUE);

    g_signal_connect(editor->buffer, "mark-set", G_CALLBACK(cursor_moved), editor);
    g_signal_connect(editor->buffer, "insert-text", G_CALLBACK(before_insert), editor);
    g_signal_connect_after(editor->buffer, "insert-text", G_CALLBACK(after_insert), editor);
    g_signal_connect(editor->buffer, "delete-range", G_CALLBACK(before_delete), editor);
    g_signal_connect_after(editor->buffer, "delete-range", G_CALLBACK(after_delete), editor);
    g_signal_connect(view, "key-press-event", G_CALLBACK(editor_key), editor);
    g_signal_connect_after(view, "event-after", G_CALLBACK(after_key), editor);
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
    g_clear_pointer(&editor->fallback_label, g_free);
    g_hash_table_remove_all(editor->visits);
    g_hash_table_remove_all(editor->opening_ids);
    g_hash_table_remove_all(editor->numbers);
    g_hash_table_remove_all(editor->changed_genres);
    g_hash_table_remove_all(editor->check_occurrences);
    g_hash_table_remove_all(editor->genre_index);
    cw_genres_clear(editor->genres);
    g_sequence_remove_range(g_sequence_get_begin_iter(editor->lines),
                            g_sequence_get_end_iter(editor->lines));

    GtkSourceBuffer *buffer = GTK_SOURCE_BUFFER(editor->buffer);
    gtk_source_buffer_begin_not_undoable_action(buffer);
    gtk_text_buffer_set_text(editor->buffer, text, -1);
    gtk_source_buffer_end_not_undoable_action(buffer);
    gint total = gtk_text_buffer_get_line_count(editor->buffer);
    for (gint n = 0; n < total; n++)
        index_line(editor, g_sequence_get_end_iter(editor->lines), n);
    check_genre_numbers(editor);
    update_genres(editor);
    if (line < 0 || line >= total) {
        GtkTextIter end = document_end(editor);
        line = gtk_text_iter_get_line(&end);
        column = gtk_text_iter_get_line_offset(&end);
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
    if (gtk_text_iter_get_line(&position) != 0)
        return FALSE;
    GtkTextIter previous = position;
    while (gtk_text_iter_backward_char(&previous) && g_unichar_isspace(gtk_text_iter_get_char(&previous)))
        position = previous;
    return gtk_text_iter_backward_char(&position) && gtk_text_iter_get_char(&position) == '/' &&
           gtk_text_iter_backward_char(&position) && gtk_text_iter_get_char(&position) == '/';
}

static void
append_with_title(CwEditor *editor, const gchar *title)
{
    const gchar *label = editor->fallback_label ? editor->fallback_label : "Crumb";
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
    gchar *snippet = g_strdup_printf("%s%s %" G_GUINT64_FORMAT ": %s: ",
                                     separator, label, largest + 1, title);
    gtk_widget_grab_focus(GTK_WIDGET(editor->view));
    gtk_text_buffer_begin_user_action(editor->buffer);
    gtk_text_buffer_insert(editor->buffer, &here, snippet, -1);
    gtk_text_buffer_place_cursor(editor->buffer, &here);
    gtk_text_buffer_end_user_action(editor->buffer);
    cw_reveal_cursor(GTK_TEXT_VIEW(editor->view));
    g_free(snippet);
}

void
cw_editor_append(CwEditor *editor)
{
    append_with_title(editor, "");
}

void
cw_editor_focus_genres(CwEditor *editor)
{
    cw_genres_focus(editor->genres);
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
    if (editor->hash_key)
        gtk_text_buffer_end_user_action(editor->buffer);
    g_signal_handlers_disconnect_by_data(editor->view, editor);
    g_signal_handlers_disconnect_by_data(editor->buffer, editor);
    g_signal_handlers_disconnect_by_data(editor->history, editor);
    GtkTextTagTable *tags = gtk_text_buffer_get_tag_table(editor->buffer);
    gtk_text_tag_table_remove(tags, editor->header_tag);
    gtk_text_tag_table_remove(tags, editor->title_tag);
    gtk_text_tag_table_remove(tags, editor->warning_tag);
    g_hash_table_unref(editor->visits);
    g_hash_table_unref(editor->changed_genres);
    g_hash_table_unref(editor->check_occurrences);
    g_hash_table_unref(editor->genre_index);
    g_hash_table_unref(editor->numbers);
    g_hash_table_unref(editor->opening_ids);
    g_free(editor->fallback_label);
    g_sequence_free(editor->lines);
    cw_genres_free(editor->genres);
    g_object_unref(editor->history);
    g_object_unref(editor->history_store);
    g_object_unref(editor->sidebar);
    g_object_unref(editor->buffer);
    g_object_unref(editor->view);
    g_free(editor);
}

#include "genres.h"

#include <string.h>

static const gchar *
trim_end(const gchar *start, const gchar *end)
{
    while (end > start) {
        const gchar *previous = g_utf8_prev_char(end);
        if (!g_unichar_isspace(g_utf8_get_char(previous)))
            break;
        end = previous;
    }
    return end;
}

static gboolean
name_bounds(const gchar *title, const gchar *hash, const gchar **begin, const gchar **end)
{
    if (hash == title || !g_unichar_isspace(g_utf8_get_char(g_utf8_prev_char(hash))))
        return FALSE;

    const gchar *first = title;
    while (first < hash && g_unichar_isspace(g_utf8_get_char(first)))
        first = g_utf8_next_char(first);
    if (first == hash)
        return FALSE;
    *begin = first;
    *end = trim_end(first, hash);
    return TRUE;
}

static gchar *
normalized_key(const gchar *first, const gchar *last)
{
    g_autofree gchar *folded = g_utf8_casefold(first, last - first);
    g_autofree gchar *normalized = g_utf8_normalize(folded, -1, G_NORMALIZE_NFC);
    GString *key = g_string_sized_new(last - first);
    gboolean gap = FALSE;
    for (const gchar *p = normalized; *p; p = g_utf8_next_char(p)) {
        gunichar c = g_utf8_get_char(p);
        if (g_unichar_isspace(c)) {
            gap = TRUE;
        } else {
            if (gap)
                g_string_append_c(key, ' ');
            g_string_append_unichar(key, c);
            gap = FALSE;
        }
    }
    return g_string_free(key, FALSE);
}

gchar *
cw_genre_key_at(const gchar *title, const gchar *typed_hash)
{
    if (!title || !typed_hash || *typed_hash != '#' || !g_utf8_validate(title, -1, NULL))
        return NULL;

    const gchar *suffix = typed_hash + 1;
    while (*suffix && g_unichar_isspace(g_utf8_get_char(suffix)))
        suffix = g_utf8_next_char(suffix);
    if (*suffix && *suffix != ',' && *suffix != ')')
        return NULL;

    const gchar *component = typed_hash;
    while (component > title && !strchr(",()", component[-1]))
        component--;
    const gchar *first, *last;
    if (!name_bounds(component, typed_hash, &first, &last))
        return NULL;
    return normalized_key(first, last);
}

void
cw_genre_foreach(const gchar *title,
                 void (*found)(const gchar *key, const gchar *name, guint64 ordinal, gpointer data),
                 gpointer data)
{
    if (!title || !found || !strchr(title, '#') || !g_utf8_validate(title, -1, NULL))
        return;

    /* A title may be "Negation #10, Confession #3", or "Fingers (Haiku
     * #157)". Test each complete component; plain title text is skipped. */
    for (const gchar *start = title; *start;) {
        const gchar *end = start + strcspn(start, ",()");
        const gchar *last = trim_end(start, end);
        const gchar *digits = last;
        while (digits > start && g_ascii_isdigit(digits[-1]))
            digits--;
        const gchar *first_name, *last_name;
        if (digits != last && digits > start && digits[-1] == '#' &&
            name_bounds(start, digits - 1, &first_name, &last_name)) {
            g_autofree gchar *key = normalized_key(first_name, last_name);
            g_autofree gchar *name = g_strndup(first_name, last_name - first_name);
            found(key, name, g_ascii_strtoull(digits, NULL, 10), data);
        }
        if (!*end)
            break;
        start = end + 1;
    }
}

enum { GENRE_NAME, GENRE_NUMBER, GENRE_ORDINAL, GENRE_KEY, GENRE_TOOLTIP, GENRE_AVAILABLE, GENRE_COLUMNS };

typedef struct {
    GtkTreeIter iter;       /* GtkListStore iterators stay valid across sorting. */
    gchar *name;
    guint64 ordinal;
} GenreRow;

struct CwGenres {
    GtkWidget *scroller;
    GtkTreeView *tree;
    GtkListStore *store;
    GHashTable *rows;
    void (*activate)(const gchar *name, guint64 ordinal, gpointer data);
    gpointer data;
};

static void
free_row(gpointer value)
{
    GenreRow *row = value;
    g_free(row->name);
    g_free(row);
}

static void
genre_activated(GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *column, CwGenres *genres)
{
    (void)column;
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter row;
    if (!gtk_tree_model_get_iter(model, &row, path))
        return;
    g_autofree gchar *name = NULL;
    guint64 ordinal;
    gtk_tree_model_get(model, &row, GENRE_NAME, &name, GENRE_ORDINAL, &ordinal, -1);
    if (ordinal == G_MAXUINT64) {
        gtk_widget_error_bell(GTK_WIDGET(view));
        return;
    }
    if (genres->activate)
        genres->activate(name, ordinal, genres->data);
}

CwGenres *
cw_genres_new(void (*activate)(const gchar *name, guint64 ordinal, gpointer data), gpointer data)
{
    CwGenres *genres = g_new0(CwGenres, 1);
    genres->activate = activate;
    genres->data = data;
    genres->rows = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_row);
    genres->store = gtk_list_store_new(GENRE_COLUMNS, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT64,
                                        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
    gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(genres->store), GENRE_KEY, GTK_SORT_ASCENDING);
    GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(genres->store));
    genres->tree = GTK_TREE_VIEW(g_object_ref_sink(tree));
    gtk_widget_set_name(tree, "crumb-genres");
    atk_object_set_name(gtk_widget_get_accessible(tree), "Genres");
    atk_object_set_description(gtk_widget_get_accessible(tree),
                               "Numbered genres in alphabetical order. Activate a genre to append its next crumb.");
    gtk_tree_view_set_headers_clickable(genres->tree, FALSE);
    gtk_tree_view_set_activate_on_single_click(genres->tree, TRUE);
    gtk_tree_view_set_search_column(genres->tree, GENRE_NAME);
    gtk_tree_view_set_tooltip_column(genres->tree, GENRE_TOOLTIP);

    GtkCellRenderer *label = gtk_cell_renderer_text_new();
    g_object_set(label, "ellipsize", PANGO_ELLIPSIZE_END, "width-chars", 8, NULL);
    GtkTreeViewColumn *names = gtk_tree_view_column_new_with_attributes(
        "Genre", label, "text", GENRE_NAME, "sensitive", GENRE_AVAILABLE, NULL);
    gtk_tree_view_column_set_expand(names, TRUE);
    gtk_tree_view_append_column(genres->tree, names);

    GtkCellRenderer *number = gtk_cell_renderer_text_new();
    g_object_set(number, "xalign", 1.0f, "ellipsize", PANGO_ELLIPSIZE_START, NULL);
    GtkTreeViewColumn *numbers = gtk_tree_view_column_new_with_attributes(
        "Last #", number, "text", GENRE_NUMBER, "sensitive", GENRE_AVAILABLE, NULL);
    gtk_tree_view_column_set_alignment(numbers, 1.0f);
    gtk_tree_view_column_set_max_width(numbers, 124);
    gtk_tree_view_append_column(genres->tree, numbers);
    g_signal_connect(tree, "row-activated", G_CALLBACK(genre_activated), genres);

    genres->scroller = g_object_ref_sink(gtk_scrolled_window_new(NULL, NULL));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(genres->scroller),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(genres->scroller, TRUE);
    gtk_container_add(GTK_CONTAINER(genres->scroller), tree);
    return genres;
}

GtkWidget *
cw_genres_widget(CwGenres *genres)
{
    return genres->scroller;
}

void
cw_genres_focus(CwGenres *genres)
{
    gtk_widget_grab_focus(GTK_WIDGET(genres->tree));
    GtkTreeSelection *selection = gtk_tree_view_get_selection(genres->tree);
    if (gtk_tree_selection_count_selected_rows(selection) == 0 &&
        gtk_tree_model_iter_n_children(GTK_TREE_MODEL(genres->store), NULL) != 0) {
        GtkTreePath *first = gtk_tree_path_new_first();
        gtk_tree_view_set_cursor(genres->tree, first, NULL, FALSE);
        gtk_tree_path_free(first);
    }
}

void
cw_genres_update(CwGenres *genres, const gchar *key, const gchar *name, guint64 ordinal)
{
    GenreRow *row = g_hash_table_lookup(genres->rows, key);
    if (!name) {
        if (row) {
            gtk_list_store_remove(genres->store, &row->iter);
            g_hash_table_remove(genres->rows, key);
        }
        return;
    }
    if (row && row->ordinal == ordinal && g_str_equal(row->name, name))
        return;

    gboolean available = ordinal != G_MAXUINT64;
    g_autofree gchar *number = g_strdup_printf("#%" G_GUINT64_FORMAT, ordinal);
    g_autofree gchar *hint = available ?
        g_markup_printf_escaped("%s %s\nAppend %s #%" G_GUINT64_FORMAT, name, number, name, ordinal + 1) :
        g_markup_printf_escaped("%s %s\nThe next number exceeds the supported range.", name, number);
    if (!row) {
        row = g_new0(GenreRow, 1);
        gtk_list_store_insert_with_values(genres->store, &row->iter, -1,
            GENRE_KEY, key, GENRE_NAME, name, GENRE_ORDINAL, ordinal, GENRE_NUMBER, number,
            GENRE_TOOLTIP, hint, GENRE_AVAILABLE, available, -1);
        g_hash_table_insert(genres->rows, g_strdup(key), row);
    } else {
        gtk_list_store_set(genres->store, &row->iter, GENRE_NAME, name, GENRE_ORDINAL, ordinal,
                           GENRE_NUMBER, number, GENRE_TOOLTIP, hint, GENRE_AVAILABLE, available, -1);
    }
    g_free(row->name);
    row->name = g_strdup(name);
    row->ordinal = ordinal;
}

void
cw_genres_clear(CwGenres *genres)
{
    gtk_list_store_clear(genres->store);
    g_hash_table_remove_all(genres->rows);
}

void
cw_genres_free(CwGenres *genres)
{
    if (!genres)
        return;
    g_signal_handlers_disconnect_by_data(genres->tree, genres);
    gtk_widget_destroy(genres->scroller);
    g_object_unref(genres->tree);
    g_object_unref(genres->scroller);
    g_object_unref(genres->store);
    g_hash_table_unref(genres->rows);
    g_free(genres);
}

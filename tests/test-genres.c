#include "genres.h"

#include <string.h>

typedef struct {
    const gchar *key, *name;
    guint64 ordinal;
} Expected;

typedef struct {
    const Expected *items;
    guint count, seen;
} Sequence;

static void
check_component(const CwGenreComponent *part, gpointer data)
{
    Sequence *seq = data;
    g_assert_cmpuint(seq->seen, <, seq->count);
    const Expected *item = &seq->items[seq->seen++];
    g_assert_cmpstr(part->key, ==, item->key);
    g_assert_cmpstr(part->name, ==, item->name);
    g_assert_true(part->ordinal == item->ordinal);
}

static void
check_title(const gchar *title, const Expected *expected, guint count)
{
    Sequence seq = {expected, count, 0};
    cw_genre_foreach(title, check_component, &seq);
    g_assert_cmpuint(seq.seen, ==, count);
}

static void
parse_names(void)
{
    const Expected first[] = {{"éloge du crumb", "E\u0301loge\u2002 du  Crumb", 42}};
    check_title("  E\u0301loge\u2002 du  Crumb   #0042\t", first, G_N_ELEMENTS(first));
    const Expected others[] = {
        {"strasse", "Straße", 9}, {"c# lessons", "C# Lessons", 0},
        {"dumb²", "Dumb²", G_MAXUINT64}
    };
    check_title("Straße #9, C# Lessons #0, Dumb² #184467440737095516151",
                others, G_N_ELEMENTS(others));
}

static void
incomplete_titles(void)
{
    const gchar *titles[] = {
        NULL, "", "   #2", "#1515", "Learning C#42", "Haiku #", "Haiku #-3",
        "Haiku #+2", "Haiku # 2", "Haiku #2.1", "Haiku #2 amended", "Haiku 3"
    };
    for (guint i = 0; i < G_N_ELEMENTS(titles); i++)
        check_title(titles[i], NULL, 0);
}

static void
compound_titles(void)
{
    const Expected components[] = {
        {"void-jolt", "Void-jolt", 12}, {"negation", "Negation", 17}, {"onslaught", "Onslaught", 45}
    };
    check_title("Void-jolt #12, Negation #17, Onslaught #45", components, G_N_ELEMENTS(components));
    const Expected report[] = {{"report", "Report", 9}};
    check_title("This One Is Very Good, Report #9", report, G_N_ELEMENTS(report));
    const Expected haiku[] = {{"haiku", "Haiku", 157}};
    check_title("Fingers (Haiku #157)", haiku, G_N_ELEMENTS(haiku));
    const Expected same[] = {{"scene", "Scene", 11}, {"scene", "SCENE", 6}};
    check_title("Scene #11, SCENE #6", same, G_N_ELEMENTS(same));
}

static void
typed_components(void)
{
    const struct { const gchar *title, *at, *key; } cases[] = {
        {"ÉLOGE  DU CRUMB #", "#", "éloge du crumb"},
        {"Fingers (Haiku #)", "#)", "haiku"},
        {"Other #4, True Myth #, Next #5", "#,", "true myth"},
        {"Heading (  Scene  #  ), ending", "#  )", "scene"},
        {"No number # followed by prose", "#", NULL},
        {"Learning C#", "#", NULL},
        {"  #", "#", NULL},
        {"Story (#3)", "#3", NULL},
        {"Genre #21", "#21", NULL}
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++) {
        const gchar *hash = strstr(cases[i].title, cases[i].at);
        g_assert_nonnull(hash);
        g_autofree gchar *key = cw_genre_key_at(cases[i].title, hash);
        g_assert_cmpstr(key, ==, cases[i].key);
    }
}

typedef struct {
    gchar *name;
    guint64 ordinal;
    guint activations;
} Choice;

static void
choose(const gchar *name, guint64 ordinal, gpointer data)
{
    Choice *choice = data;
    g_free(choice->name);
    choice->name = g_strdup(name);
    choice->ordinal = ordinal;
    choice->activations++;
}

static void
row_changed(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *row, guint *changes)
{
    (void)model;
    (void)path;
    (void)row;
    (*changes)++;
}

static void
check_row(GtkTreeModel *model, gint index, const gchar *name, const gchar *number)
{
    GtkTreeIter row;
    g_assert_true(gtk_tree_model_iter_nth_child(model, &row, NULL, index));
    g_autofree gchar *actual_name = NULL;
    g_autofree gchar *actual_number = NULL;
    gtk_tree_model_get(model, &row, 0, &actual_name, 1, &actual_number, -1);
    g_assert_cmpstr(actual_name, ==, name);
    g_assert_cmpstr(actual_number, ==, number);
}

static void
list_changes(void)
{
    Choice choice = {0};
    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    CwGenres *genres = cw_genres_new(choose, &choice);
    GtkWidget *scroller = cw_genres_widget(genres);
    gtk_container_add(GTK_CONTAINER(window), scroller);
    gtk_widget_show_all(window);
    GtkTreeView *view = GTK_TREE_VIEW(gtk_bin_get_child(GTK_BIN(scroller)));
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    cw_genres_update(genres, "portrait", "Portrait", 4);
    cw_genres_update(genres, "haiku", "Haiku", 173);
    cw_genres_update(genres, "aflection", "Aflection", 3);
    check_row(model, 0, "Aflection", "#3");
    check_row(model, 1, "Haiku", "#173");
    check_row(model, 2, "Portrait", "#4");

    guint changes = 0;
    g_signal_connect(model, "row-changed", G_CALLBACK(row_changed), &changes);
    cw_genres_update(genres, "haiku", "Haiku", 173);
    g_assert_cmpuint(changes, ==, 0);
    cw_genres_update(genres, "haiku", "HAIKU", 90);
    check_row(model, 1, "HAIKU", "#90");
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 3);
    cw_genres_focus(genres);
    g_assert_cmpint(gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(view)), ==, 1);
    GtkTreePath *second = gtk_tree_path_new_from_indices(1, -1);
    gtk_tree_view_row_activated(view, second, gtk_tree_view_get_column(view, 0));
    g_assert_cmpstr(choice.name, ==, "HAIKU");
    g_assert_cmpuint(choice.ordinal, ==, 90);
    cw_genres_update(genres, "haiku", "HAIKU", G_MAXUINT64);
    gtk_tree_view_row_activated(view, second, gtk_tree_view_get_column(view, 0));
    g_assert_cmpuint(choice.activations, ==, 1);
    gtk_tree_path_free(second);
    cw_genres_update(genres, "haiku", NULL, 0);
    check_row(model, 1, "Portrait", "#4");
    cw_genres_clear(genres);
    g_assert_cmpint(gtk_tree_model_iter_n_children(model, NULL), ==, 0);
    cw_genres_free(genres);
    gtk_widget_destroy(window);
    g_free(choice.name);
}

int
main(int argc, char **argv)
{
    gtk_test_init(&argc, &argv, NULL);
    g_test_add_func("/genres/parser/names", parse_names);
    g_test_add_func("/genres/parser/incomplete", incomplete_titles);
    g_test_add_func("/genres/parser/compound", compound_titles);
    g_test_add_func("/genres/parser/typed-components", typed_components);
    g_test_add_func("/genres/list/changes", list_changes);
    return g_test_run();
}

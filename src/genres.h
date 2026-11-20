#ifndef CW_GENRES_H
#define CW_GENRES_H

#include <gtk/gtk.h>

#define CW_NUMBER_WARNING_COLOR "#dc3545"

typedef struct CwGenres CwGenres;

typedef struct {
    const gchar *key;
    const gchar *name;
    guint64 ordinal;
    gint digits_start; /* Character offsets in the title, end exclusive. */
    gint digits_end;
    gboolean overflow;
} CwGenreComponent;

/* Visit numbered title components in written order, including comma lists
 * and parenthetical genres. Strings are valid during the callback. An
 * out-of-range decimal saturates at G_MAXUINT64 and has no successor. */
void cw_genre_foreach(const gchar *title,
                      void (*found)(const CwGenreComponent *part, gpointer data),
                      gpointer data);

/* The typed ASCII # points into this title. A complete component name and
 * an empty number slot at the caret identify the case-folded genre key. */
gchar *cw_genre_key_at(const gchar *title, const gchar *typed_hash);

CwGenres *cw_genres_new(void (*activate)(const gchar *name, guint64 ordinal, gpointer data),
                        gpointer data);
GtkWidget *cw_genres_widget(CwGenres *genres);
void cw_genres_focus(CwGenres *genres);
void cw_genres_update(CwGenres *genres, const gchar *key, const gchar *name, guint64 ordinal);
void cw_genres_set_warning(CwGenres *genres, const gchar *key, gboolean warning);
void cw_genres_clear(CwGenres *genres);
void cw_genres_free(CwGenres *genres);

#endif

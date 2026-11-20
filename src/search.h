#ifndef CW_SEARCH_H
#define CW_SEARCH_H

#include <gtksourceview/gtksource.h>

typedef struct CwSearch CwSearch;

/* The view keeps the same source buffer when the document changes. */
CwSearch *cw_search_new(GtkSourceView *view, GKeyFile *preferences,
                        void (*prefs_changed)(gpointer), gpointer user_data);
GtkWidget *cw_search_widget(CwSearch *search);
gboolean cw_search_handle_key(CwSearch *search, GdkEventKey *event);
void cw_search_find(CwSearch *search, gboolean backwards);
void cw_search_show(CwSearch *search, gboolean replace);
void cw_search_number(CwSearch *search);
void cw_search_step(CwSearch *search, gboolean backwards);
void cw_search_hide(CwSearch *search);
void cw_search_reset(CwSearch *search);
void cw_search_free(CwSearch *search);

#endif

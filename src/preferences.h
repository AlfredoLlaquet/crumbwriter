#ifndef CW_PREFERENCES_H
#define CW_PREFERENCES_H

#include "app.h"

void cw_preferences_load(CwApp *app);
gboolean cw_preference_enabled(CwApp *app, const gchar *name);
gint cw_preference_integer(CwApp *app, const gchar *name, gint fallback,
                            gint minimum, gint maximum);
void cw_preferences_theme(CwApp *app);
void cw_preferences_font(CwApp *app, GtkCssProvider *provider);
void cw_preferences_show(CwApp *app, GtkWindow *parent);
void cw_preferences_store_cursor(CwApp *app, const gchar *path, GtkTextBuffer *buffer);
void cw_preferences_read_cursor(CwApp *app, const gchar *path, gint *line, gint *column);

#endif

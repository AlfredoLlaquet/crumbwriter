#ifndef CW_APP_H
#define CW_APP_H

#include <gtk/gtk.h>

typedef struct {
    GtkApplication *gtk;
    GKeyFile *preferences;
    gchar *preferences_path;
    gchar *last_document;
    GtkWidget *settings_window;
    gboolean recovered;
} CwApp;

void cw_app_write_preferences(gpointer data);
void cw_app_apply_preferences(CwApp *app);

#endif

#ifndef CW_WINDOW_H
#define CW_WINDOW_H

#include "app.h"

typedef struct CwWindow CwWindow;

CwWindow *cw_window_new(CwApp *app);
void cw_window_open(CwApp *app, const gchar *path);
void cw_window_recover(CwApp *app, const gchar *draft_path);
void cw_window_apply_preferences(CwWindow *w);
void cw_window_register_shortcuts(GtkApplication *app);

#endif

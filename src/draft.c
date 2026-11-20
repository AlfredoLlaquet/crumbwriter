#include "draft.h"
#include "spacing.h"
#include <errno.h>
#include <glib/gstdio.h>

static gchar *
draft_directory(void)
{
    return g_build_filename(g_get_user_data_dir(), "crumbwriter", "drafts", NULL);
}

gboolean
cw_draft_write(gchar **path, const gchar *text, GError **error)
{
    g_autofree gchar *dir = draft_directory();
    if (g_mkdir_with_parents(dir, 0700) < 0) {
        g_set_error(error, G_FILE_ERROR, g_file_error_from_errno(errno),
                    "Cannot create the draft directory: %s", g_strerror(errno));
        return FALSE;
    }
    if (*path == NULL) {
        g_autofree gchar *id = g_uuid_string_random();
        g_autofree gchar *name = g_strconcat(id, ".txt", NULL);
        *path = g_build_filename(dir, name, NULL);
    }
    while (g_str_has_prefix(text, "\357\273\277"))
        text += 3;
    g_autofree gchar *snapshot = g_strdup(text);
    gsize length = cw_spacing_normalize(snapshot);
    return g_file_set_contents_full(*path, snapshot, length,
               G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE, 0600, error);
}

void
cw_draft_remove(gchar **path)
{
    if (*path && g_unlink(*path) < 0 && errno != ENOENT)
        g_warning("Could not remove saved draft %s: %s", *path, g_strerror(errno));
    g_clear_pointer(path, g_free);
}

GPtrArray *
cw_draft_list(void)
{
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    g_autofree gchar *dir = draft_directory();
    g_autoptr(GDir) entries = g_dir_open(dir, 0, NULL);
    const gchar *name;
    while (entries && (name = g_dir_read_name(entries))) {
        if (g_str_has_suffix(name, ".txt"))
            g_ptr_array_add(paths, g_build_filename(dir, name, NULL));
    }
    return paths;
}

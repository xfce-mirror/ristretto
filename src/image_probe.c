/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "image_probe.h"
#include <string.h>

#ifdef HAVE_GLYCIN
#include <glycin.h>
#endif

gboolean
rstto_image_probe_available (void)
{
#ifdef HAVE_GLYCIN
    return TRUE;
#else
    return FALSE;
#endif
}

static void
collect_raw_types (const gchar *directory, GHashTable *types)
{
    gchar *path = g_build_filename (directory, "glycin-loaders", "2+", "conf.d", NULL);
    GDir *dir = g_dir_open (path, 0, NULL);
    const gchar *name;
    while (dir != NULL && (name = g_dir_read_name (dir)) != NULL)
    {
        if (!g_str_has_suffix (name, ".conf"))
            continue;
        gchar *filename = g_build_filename (path, name, NULL);
        GKeyFile *config = g_key_file_new ();
        if (g_key_file_load_from_file (config, filename, G_KEY_FILE_NONE, NULL))
        {
            gchar **groups = g_key_file_get_groups (config, NULL);
            for (guint i = 0; groups[i] != NULL; i++)
            {
                if (!g_str_has_prefix (groups[i], "loader:"))
                    continue;
                gchar *exec = g_key_file_get_string (config, groups[i], "Exec", NULL);
                gchar *basename = exec != NULL ? g_path_get_basename (exec) : NULL;
                if (g_strcmp0 (basename, "glycin-raw") == 0)
                    g_hash_table_add (types, g_strdup (groups[i] + strlen ("loader:")));
                g_free (basename);
                g_free (exec);
            }
            g_strfreev (groups);
        }
        g_key_file_unref (config);
        g_free (filename);
    }
    if (dir != NULL)
        g_dir_close (dir);
    g_free (path);
}

gchar **
rstto_image_probe_raw_mime_types (void)
{
    GHashTable *types = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    const gchar *override = g_getenv ("GLYCIN_DATA_DIR");
    if (override != NULL)
        collect_raw_types (override, types);
    else
    {
        collect_raw_types (g_get_user_data_dir (), types);
        const gchar *const *dirs = g_get_system_data_dirs ();
        for (guint i = 0; dirs[i] != NULL; i++)
            collect_raw_types (dirs[i], types);
    }
    /* Union all unsafe mappings, even if a higher-priority config overrides
     * one. Being conservative avoids depending on directory iteration order. */
    GPtrArray *result = g_ptr_array_new ();
    GHashTableIter iter;
    gpointer key;
    g_hash_table_iter_init (&iter, types);
    while (g_hash_table_iter_next (&iter, &key, NULL))
        g_ptr_array_add (result, g_strdup (key));
    g_ptr_array_add (result, NULL);
    g_hash_table_unref (types);
    return (gchar **) g_ptr_array_free (result, FALSE);
}

static gboolean
raw_type (const gchar *type, const gchar *const *raw_types)
{
    if (type == NULL)
        return FALSE;
    if (g_content_type_is_a (type, "image/x-dcraw"))
        return TRUE;
    gchar *mime = g_content_type_get_mime_type (type);
    gboolean raw = raw_types != NULL && mime != NULL && g_strv_contains (raw_types, mime);
    g_free (mime);
    return raw;
}

gboolean
rstto_image_probe_is_raw (GFile *file, GBytes *prefix, const gchar *const *raw_types)
{
    gsize size;
    const guchar *bytes = g_bytes_get_data (prefix, &size);
    gchar *basename = g_file_get_basename (file);
    /* Classify the extension separately: TIFF and embedded JPEG signatures can
     * otherwise mask a RAW filename. The byte-only check matches Glycin's
     * classification of the GBytes input (which carries no filename). */
    gchar *content = g_content_type_guess (NULL, bytes, size, NULL);
    gchar *filename = g_content_type_guess (basename, NULL, 0, NULL);
    gchar *combined = g_content_type_guess (basename, bytes, size, NULL);
    gboolean raw = raw_type (content, raw_types) || raw_type (filename, raw_types)
                   || raw_type (combined, raw_types);
    g_free (content);
    g_free (filename);
    g_free (combined);
    g_free (basename);
    return raw;
}

GBytes *
rstto_image_probe_read (GInputStream *stream, GCancellable *cancel)
{
    guchar *bytes = g_malloc (RSTTO_PROBE_LIMIT);
    gsize total = 0;
    while (total < RSTTO_PROBE_LIMIT)
    {
        gssize count = g_input_stream_read (stream, bytes + total,
                                           MIN (RSTTO_PROBE_BUFFER_SIZE, RSTTO_PROBE_LIMIT - total),
                                           cancel, NULL);
        if (count < 0 || g_cancellable_is_cancelled (cancel))
        {
            g_free (bytes);
            return NULL;
        }
        if (count == 0)
            break;
        total += count;
    }
    return g_bytes_new_take (bytes, total);
}

typedef struct
{
    gint width, height;
} Dimensions;

#ifdef HAVE_GLYCIN
static void
metadata_ready (GObject *source, GAsyncResult *result, gpointer data)
{
    GTask *task = data;
    GlyImage *image = gly_loader_load_finish (GLY_LOADER (source), result, NULL);
    Dimensions *dimensions = NULL;
    if (image != NULL && !g_cancellable_is_cancelled (g_task_get_cancellable (task)))
    {
        guint32 width = gly_image_get_width (image), height = gly_image_get_height (image);
        if (width > 0 && height > 0 && width <= G_MAXINT && height <= G_MAXINT)
        {
            dimensions = g_new (Dimensions, 1);
            *dimensions = (Dimensions) { width, height };
        }
    }
    g_clear_object (&image);
    /* Discard our loader/input references before publishing completion. Glycin
     * owns its pooled helper and asynchronous remote-object cleanup. */
    g_task_set_task_data (task, NULL, NULL);
    g_task_return_pointer (task, dimensions, g_free);
    g_object_unref (task);
}
#endif

void
rstto_image_probe_async (GBytes *prefix, GCancellable *cancel,
                        GAsyncReadyCallback callback, gpointer data)
{
    GTask *task = g_task_new (NULL, cancel, callback, data);
    g_task_set_return_on_cancel (task, FALSE);
#ifdef HAVE_GLYCIN
    GlyLoader *loader = gly_loader_new_for_bytes (prefix);
    gly_loader_set_apply_transformations (loader, FALSE);
    g_task_set_task_data (task, loader, g_object_unref);
    gly_loader_load_async (loader, cancel, metadata_ready, task);
#else
    g_task_return_pointer (task, NULL, NULL);
    g_object_unref (task);
#endif
}

gboolean
rstto_image_probe_finish (GAsyncResult *result, gint *width, gint *height)
{
    Dimensions *dimensions = g_task_propagate_pointer (G_TASK (result), NULL);
    if (dimensions == NULL)
        return FALSE;
    *width = dimensions->width;
    *height = dimensions->height;
    g_free (dimensions);
    return TRUE;
}

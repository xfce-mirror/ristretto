/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util.h"
#include "image_viewer.h"
#include "main_window.h"
#include "settings.h"

#include <glib/gstdio.h>

static RsttoImageViewer *viewer;
static RsttoImageCache *cache;

static void
find_viewer (GtkWidget *widget, gpointer unused)
{
    if (RSTTO_IS_IMAGE_VIEWER (widget))
        viewer = RSTTO_IMAGE_VIEWER (widget);
    else if (GTK_IS_CONTAINER (widget))
        gtk_container_foreach (GTK_CONTAINER (widget), find_viewer, NULL);
}

static void
wait_loaded (void)
{
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    RsttoCacheStats stats;
    do
    {
        for (guint n = 0; n < 10 && g_main_context_pending (NULL); n++)
            g_main_context_iteration (NULL, FALSE);
        rstto_image_cache_get_stats (cache, &stats);
        g_assert_cmpuint (stats.jobs, <=, 2);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
        g_usleep (1000);
    } while (stats.jobs != 0 || rstto_image_cache_is_busy (cache));
}

static void
add_exif_orientation (const gchar *path)
{
    /* JPEG APP1, little-endian TIFF with one SHORT Orientation=6 entry. */
    const guint8 exif[] = {
        0xff, 0xe1, 0, 34, 'E', 'x', 'i', 'f', 0, 0,
        'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0,
        0x12, 1, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0
    };
    gchar *contents;
    gsize size;
    g_assert_true (g_file_get_contents (path, &contents, &size, NULL));
    GByteArray *jpeg = g_byte_array_new ();
    g_byte_array_append (jpeg, (const guint8 *) contents, 2);
    g_byte_array_append (jpeg, exif, sizeof exif);
    g_byte_array_append (jpeg, (const guint8 *) contents + 2, size - 2);
    g_assert_true (g_file_set_contents (path, (const gchar *) jpeg->data, jpeg->len, NULL));
    g_byte_array_unref (jpeg);
    g_free (contents);
}

static void
test_viewer (void)
{
    gchar *directory = g_dir_make_tmp ("ristretto-viewer-test-XXXXXX", NULL);
    RsttoSettings *settings = rstto_settings_new ();
    gboolean enabled;
    guint images, total, single, count;
    g_object_get (settings, "cache-enabled", &enabled, "cache-max-images", &images,
                  "cache-max-mib", &total, "cache-max-image-mib", &single, "preload-count", &count, NULL);
    g_assert_true (enabled);
    g_assert_cmpuint (images, ==, 5);
    g_assert_cmpuint (total, ==, 256);
    g_assert_cmpuint (single, ==, 128);
    g_assert_cmpuint (count, ==, 2);
    g_object_set (settings, "maximize-on-startup", FALSE, "show-error-missing-thumbnailer", FALSE, NULL);
    RsttoImageList *list = rstto_image_list_new ();
    GtkWidget *window = rstto_main_window_new (list, FALSE);
    g_object_ref_sink (window);
    gtk_widget_show_all (window);
    gtk_container_foreach (GTK_CONTAINER (window), find_viewer, NULL);
    g_assert_nonnull (viewer);
    cache = g_object_ref (rstto_image_viewer_get_cache (viewer));
    RsttoFile *files[8];
    for (guint n = 0; n < G_N_ELEMENTS (files); n++)
    {
        gchar *path = g_strdup_printf ("%s/%u.%s", directory, n, n == 2 ? "jpg" : "png");
        GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, n != 2, 8, 128, 96);
        gdk_pixbuf_fill (pixbuf, 0x102030ff + (n << 24));
        GError *error = NULL;
        gdk_pixbuf_save (pixbuf, path, n == 2 ? "jpeg" : "png", &error, NULL);
        g_assert_no_error (error);
        g_object_unref (pixbuf);
        if (n == 2)
            add_exif_orientation (path);
        GFile *file = g_file_new_for_path (path);
        files[n] = rstto_file_new (file);
        g_object_unref (file);
        g_free (path);
    }
    GFile *dir = g_file_new_for_path (directory);
    GError *error = NULL;
    g_assert_true (rstto_image_list_set_directory (list, dir, files[0], &error));
    g_assert_no_error (error);
    g_object_unref (dir);
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (rstto_image_list_is_busy (list))
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    g_assert_cmpint (rstto_image_list_get_n_images (list), ==, 8);
    for (guint n = 0; n < G_N_ELEMENTS (files); n++)
    {
        g_object_unref (files[n]);
        files[n] = g_object_ref (rstto_image_list_get_file_at_position (list, n));
    }
    RsttoImageListIter *iter = rstto_main_window_get_iter (RSTTO_MAIN_WINDOW (window));
    rstto_image_list_iter_set_position (iter, 0);
    wait_loaded ();
    RsttoCacheStats before, after;
    rstto_image_cache_get_stats (cache, &before);
    g_assert_cmpuint (before.entries, ==, 5);
    g_assert_cmpuint (before.decodes, ==, 5);
    rstto_image_list_iter_next (iter);
    wait_loaded ();
    rstto_image_cache_get_stats (cache, &after);
    g_assert_cmpuint (after.decodes, ==, before.decodes);
    g_assert_cmpuint (after.hits, >, before.hits);
    rstto_image_list_iter_previous (iter);
    wait_loaded ();
    g_assert_cmpint (rstto_image_viewer_get_width (viewer), ==, 128);
    rstto_image_viewer_set_orientation (viewer, RSTTO_IMAGE_ORIENT_90);
    rstto_image_viewer_set_scale (viewer, 2.0);
    rstto_image_list_iter_next (iter);
    wait_loaded ();
    rstto_image_list_iter_previous (iter);
    wait_loaded ();
    g_assert_cmpint (rstto_image_viewer_get_orientation (viewer), ==, RSTTO_IMAGE_ORIENT_90);
    /* Printing consumes this same surface-to-pixbuf export. */
    GdkPixbuf *export = rstto_image_viewer_get_pixbuf (viewer);
    g_assert_nonnull (export);
    g_assert_cmpint (gdk_pixbuf_get_width (export), ==, 128);
    g_assert_cmpint (gdk_pixbuf_get_pixels (export)[0], ==, 0x10);
    g_object_unref (export);
    rstto_image_cache_get_stats (cache, &before);
    rstto_image_list_iter_set_position (iter, 2);
    wait_loaded ();
    g_assert_cmpint (rstto_image_viewer_get_orientation (viewer), ==, RSTTO_IMAGE_ORIENT_90);
    rstto_image_cache_get_stats (cache, &after);
    g_assert_cmpuint (after.hits, >, before.hits);
    rstto_image_list_iter_set_position (iter, 0);
    wait_loaded ();
    rstto_image_list_set_sort_order (list, SORT_ORDER_DESC);
    wait_loaded ();
    g_assert_true (rstto_image_list_iter_get_file (iter) == files[0]);
    rstto_image_list_remove_file (list, files[7]);
    wait_loaded ();
    g_object_set (settings, "preload-count", 0, "cache-max-images", 2, NULL);
    wait_loaded ();
    rstto_image_cache_get_stats (cache, &after);
    g_assert_cmpuint (after.entries, <=, 2);
    rstto_image_cache_get_stats (cache, &before);
    rstto_file_changed (files[0]);
    wait_loaded ();
    rstto_image_cache_get_stats (cache, &after);
    g_assert_cmpuint (after.decodes, >, before.decodes);
    /* Exercise the real directory monitor, not an explicit Refresh signal. */
    rstto_image_cache_get_stats (cache, &before);
    gchar *changed_path = g_file_get_path (rstto_file_get_file (files[0]));
    GdkPixbuf *changed = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 128, 96);
    gdk_pixbuf_fill (changed, 0xab2040ff);
    g_assert_true (gdk_pixbuf_save (changed, changed_path, "png", NULL, NULL));
    g_object_unref (changed);
    g_free (changed_path);
    until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    do
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
        rstto_image_cache_get_stats (cache, &after);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    } while (after.decodes == before.decodes);
    wait_loaded ();
    export = rstto_image_viewer_get_pixbuf (viewer);
    g_assert_cmpint (gdk_pixbuf_get_pixels (export)[0], ==, 0xab);
    g_object_unref (export);
    g_object_set (settings, "limit-quality", TRUE, NULL);
    wait_loaded ();
    /* Close immediately after a fresh foreground request. */
    rstto_image_list_iter_next (iter);
    gtk_widget_destroy (window);
    wait_loaded ();
    rstto_image_cache_get_stats (cache, &after);
    g_assert_cmpuint (after.resident_bytes, ==, 0);
    g_assert_cmpuint (after.reserved_bytes, ==, 0);
    g_object_unref (window);
    g_object_unref (list);
    g_object_unref (cache);
    g_object_unref (settings);
    for (guint n = 0; n < G_N_ELEMENTS (files); n++)
    {
        g_file_delete (rstto_file_get_file (files[n]), NULL, NULL);
        g_object_unref (files[n]);
    }
    g_rmdir (directory);
    g_free (directory);
}

int
main (int argc, char **argv)
{
    gchar *config = g_dir_make_tmp ("ristretto-test-config-XXXXXX", NULL);
    g_setenv ("XDG_CONFIG_HOME", config, TRUE);
    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    g_test_init (&argc, &argv, NULL);
    if (!gtk_init_check (NULL, NULL))
    {
        g_print ("A graphical display is required for the viewer integration test.\n");
        g_rmdir (config);
        g_free (config);
        return 77;
    }
    rstto_settings_set_xfconf_disabled (TRUE);
    g_test_add_func ("/viewer/navigation-and-lifetime", test_viewer);
    gint result = g_test_run ();
    gchar *accels = g_build_filename (config, "ristretto", "accels.scm", NULL);
    g_remove (accels);
    g_free (accels);
    gchar *app_config = g_build_filename (config, "ristretto", NULL);
    g_rmdir (app_config);
    g_free (app_config);
    g_rmdir (config);
    g_free (config);
    return result;
}

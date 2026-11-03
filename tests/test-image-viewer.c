/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util.h"
#include "image_viewer.h"
#include "image_probe.h"
#include "main_window.h"
#include "settings.h"

#include <glib/gstdio.h>

void rstto_test_worker_checkpoint (gint phase, gboolean demand, GCancellable *cancel, gboolean entering);

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
        g_assert_cmpuint (stats.jobs, <=, RSTTO_PRELOAD_WORKERS_MAX + 1);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
        g_usleep (1000);
    } while (stats.jobs != 0 || rstto_image_cache_is_busy (cache)
             || (viewer != NULL && rstto_image_viewer_is_busy (viewer)));
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
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 30 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed ();
        return;
    }
    gchar *directory = g_dir_make_tmp ("ristretto-viewer-test-XXXXXX", NULL);
    RsttoSettings *settings = rstto_settings_new ();
    gboolean enabled;
    guint images, total, single, count, workers;
    g_object_get (settings, "cache-enabled", &enabled, "cache-max-images", &images,
                  "cache-max-mib", &total, "cache-max-image-mib", &single, "preload-count", &count, "preload-workers", &workers, NULL);
    g_assert_true (enabled);
    g_assert_cmpuint (images, ==, 5);
    g_assert_cmpuint (total, ==, 256);
    g_assert_cmpuint (single, ==, 128);
    g_assert_cmpuint (count, ==, 2);
    g_assert_cmpuint (workers, ==, 2);
    g_object_set (settings, "maximize-on-startup", FALSE, "show-error-missing-thumbnailer", FALSE, "cache-max-images", 6u, NULL);
    RsttoImageList *list = rstto_image_list_new ();
    GtkWidget *window = rstto_main_window_new (list, FALSE);
    g_object_ref_sink (window);
    gtk_widget_show_all (window);
    gtk_container_foreach (GTK_CONTAINER (window), find_viewer, NULL);
    g_assert_nonnull (viewer);
    g_object_ref (viewer);
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
    before = after;
    g_object_set (settings, "preload-workers", 1, NULL);
    wait_loaded ();
    g_object_set (settings, "preload-workers", 4, NULL);
    wait_loaded ();
    rstto_image_cache_get_stats (cache, &after);
    g_assert_cmpuint (after.decodes, ==, before.decodes); /* Scheduling must not reload the viewer. */
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
    g_clear_object (&viewer);
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

/* Logical navigation ticks release workers deterministically. Actual input is
 * delivered at 4 Hz, while each selected phase is held for three input ticks. */
static GMutex scrolling_mutex;
static GCond scrolling_cond;
static gboolean scrolling_gate;
static guint scrolling_tick, scrolling_phase, scrolling_cancelled, scrolling_entered;

void
rstto_test_worker_checkpoint (gint phase, gboolean demand, GCancellable *cancel, gboolean entering)
{
    g_mutex_lock (&scrolling_mutex);
    if (scrolling_gate && entering && (guint) phase == scrolling_phase)
    {
        guint release = scrolling_tick + 3;
        scrolling_entered++;
        while (scrolling_gate && scrolling_tick < release)
            g_cond_wait (&scrolling_cond, &scrolling_mutex);
    }
    if (scrolling_gate && !entering && demand && g_cancellable_is_cancelled (cancel))
        scrolling_cancelled++;
    g_mutex_unlock (&scrolling_mutex);
}

typedef struct
{
    GtkWidget *window;
    RsttoImageList *list;
    RsttoImageListIter *iter;
    RsttoFile *files[16], *last;
    guint presentations, last_tick, maximum_gap, latest;
    gboolean running;
    GFile *forbidden[2];
    RsttoFile *replacement;
    gchar *replacement_dir;
} Scrolling;

static void
scrolling_check_limits (void)
{
    RsttoCacheStats stats;
    rstto_image_cache_get_stats (cache, &stats);
    g_assert_cmpuint (stats.resident_bytes + stats.reserved_bytes, <=, (guint64) 2048 * 1024 * 1024);
    g_assert_cmpuint (stats.entries + stats.reserved_images, <=, 6);
    g_assert_cmpuint (stats.speculative_images, <=, 4);
    g_assert_cmpuint (stats.jobs, <=, 5);
    g_assert_cmpuint (stats.active_tasks, <=, 5);
}

static void
scrolling_presented (RsttoImageViewer *widget, Scrolling *s)
{
    for (guint n = 0; n < 2; n++)
        if (s->forbidden[n] != NULL && rstto_image_viewer_get_file (widget) != NULL)
            g_assert_false (g_file_equal (s->forbidden[n], rstto_file_get_file (rstto_image_viewer_get_file (widget))));
    if (!s->running)
        return;
    scrolling_check_limits ();
    RsttoFile *file = rstto_image_viewer_get_file (widget);
    guint index;
    for (index = 0; index < G_N_ELEMENTS (s->files); index++)
        if (file != NULL && g_file_equal (rstto_file_get_file (file), rstto_file_get_file (s->files[index])))
            break;
    g_assert_cmpuint (index, <, G_N_ELEMENTS (s->files));
    g_assert_nonnull (strstr (gtk_window_get_title (GTK_WINDOW (s->window)), rstto_file_get_display_name (file)));
    GdkPixbuf *pixels = rstto_image_viewer_get_pixbuf (widget);
    g_assert_nonnull (pixels);
    g_assert_cmpuint (gdk_pixbuf_get_pixels (pixels)[0], ==, 0x10 + index);
    g_object_unref (pixels);
    if (file != s->last)
    {
        s->presentations++;
        s->maximum_gap = MAX (s->maximum_gap, scrolling_tick - s->last_tick);
        s->last_tick = scrolling_tick;
        s->last = file;
    }
}

static gboolean
scrolling_input (gpointer data)
{
    Scrolling *s = data;
    g_mutex_lock (&scrolling_mutex);
    scrolling_tick++;
    g_cond_broadcast (&scrolling_cond);
    guint tick = scrolling_tick;
    g_mutex_unlock (&scrolling_mutex);
    /* Forward movement, wraparound, then reversal, beyond the six-slot cache. */
    s->latest = (tick <= 20 ? 5 + tick : 45 - tick) % G_N_ELEMENTS (s->files);
    rstto_image_list_iter_set_position (s->iter, s->latest);
    scrolling_check_limits ();
    if (tick == 40)
    {
        g_assert_cmpuint (s->presentations, >=, 6); /* progress before input stops */
        g_assert_cmpuint (s->maximum_gap, <=, 8);
        g_assert_cmpuint (tick - s->last_tick, <=, 8);
        g_mutex_lock (&scrolling_mutex);
        g_assert_cmpuint (scrolling_entered, >, 0);
        g_assert_cmpuint (scrolling_cancelled, ==, 0);
        scrolling_gate = FALSE;
        g_cond_broadcast (&scrolling_cond);
        g_mutex_unlock (&scrolling_mutex);
        return G_SOURCE_REMOVE;
    }
    return G_SOURCE_CONTINUE;
}

static void
scrolling_hold (guint phase)
{
    g_mutex_lock (&scrolling_mutex);
    scrolling_phase = phase;
    scrolling_tick = scrolling_entered = 0;
    scrolling_gate = TRUE;
    g_mutex_unlock (&scrolling_mutex);
}

static void
scrolling_await_worker (void)
{
    gint64 until = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
    for (;;)
    {
        g_mutex_lock (&scrolling_mutex);
        gboolean entered = scrolling_entered > 0;
        g_mutex_unlock (&scrolling_mutex);
        if (entered)
            break;
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
}

static void
scrolling_release (void)
{
    g_mutex_lock (&scrolling_mutex);
    scrolling_gate = FALSE;
    g_cond_broadcast (&scrolling_cond);
    g_mutex_unlock (&scrolling_mutex);
    wait_loaded ();
}

static void
navigation_boundaries (Scrolling *s, const gchar *directory)
{
    s->running = FALSE;
    scrolling_hold (4);
    rstto_image_cache_invalidate (cache, rstto_file_get_file (s->files[6]));
    rstto_image_list_iter_find_file (s->iter, s->files[6]);
    scrolling_await_worker ();
    rstto_image_list_iter_find_file (s->iter, s->files[7]);
    gtk_widget_hide (s->window);
    scrolling_release ();
    g_assert_true (rstto_image_viewer_get_file (viewer) == s->files[7]);
    gtk_widget_show (s->window);

    scrolling_hold (4);
    rstto_image_cache_invalidate (cache, rstto_file_get_file (s->files[10]));
    rstto_image_list_iter_find_file (s->iter, s->files[10]);
    scrolling_await_worker ();
    rstto_image_list_iter_find_file (s->iter, s->files[11]);
    s->forbidden[0] = rstto_file_get_file (s->files[10]);
    rstto_image_list_remove_file (s->list, s->files[10]);
    scrolling_release ();
    g_assert_true (rstto_image_viewer_get_file (viewer) == s->files[11]);

    scrolling_hold (4);
    rstto_image_cache_invalidate (cache, rstto_file_get_file (s->files[14]));
    rstto_image_list_iter_find_file (s->iter, s->files[14]);
    scrolling_await_worker ();
    rstto_image_list_iter_find_file (s->iter, s->files[15]);
    s->forbidden[0] = rstto_file_get_file (s->files[14]);
    s->forbidden[1] = rstto_file_get_file (s->files[15]);
    s->replacement_dir = g_build_filename (directory, "replacement", NULL);
    g_assert_cmpint (g_mkdir (s->replacement_dir, 0700), ==, 0);
    gchar *path = g_build_filename (s->replacement_dir, "new.png", NULL);
    GdkPixbuf *pixels = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 64, 48);
    gdk_pixbuf_fill (pixels, 0x998877ff);
    g_assert_true (gdk_pixbuf_save (pixels, path, "png", NULL, NULL));
    g_object_unref (pixels);
    GFile *file = g_file_new_for_path (path), *dir = g_file_new_for_path (s->replacement_dir);
    s->replacement = rstto_file_new (file);
    g_assert_true (rstto_image_list_set_directory (s->list, dir, s->replacement, NULL));
    g_object_unref (file); g_object_unref (dir); g_free (path);
    scrolling_release ();
    gint64 until = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;
    while (rstto_image_list_is_busy (s->list))
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    wait_loaded ();
    g_assert_true (g_file_equal (rstto_file_get_file (rstto_image_viewer_get_file (viewer)),
                                 rstto_file_get_file (s->replacement)));
    s->forbidden[0] = s->forbidden[1] = NULL;

    rstto_image_list_add_file (s->list, s->files[12], NULL);
    rstto_image_list_add_file (s->list, s->files[13], NULL);
    wait_loaded ();
    scrolling_hold (9);
    rstto_image_cache_invalidate (cache, rstto_file_get_file (s->files[12]));
    rstto_image_list_iter_find_file (s->iter, s->files[12]);
    scrolling_await_worker ();
    rstto_image_list_iter_find_file (s->iter, s->files[13]);
    gtk_widget_destroy (s->window);
    scrolling_release ();
    RsttoCacheStats stats;
    rstto_image_cache_get_stats (cache, &stats);
    g_assert_cmpuint (stats.resident_bytes + stats.reserved_bytes, ==, 0);
    g_assert_cmpuint (stats.jobs, ==, 0);
}


static void
sustained_scrolling (gconstpointer data)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 45 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed ();
        return;
    }
    Scrolling s = { 0 };
    gchar *directory = g_dir_make_tmp ("ristretto-scrolling-test-XXXXXX", NULL);
    RsttoSettings *settings = rstto_settings_new ();
    g_object_set (settings, "maximize-on-startup", FALSE, "show-error-missing-thumbnailer", FALSE,
        "window-width", 800u, "window-height", 600u, "cache-enabled", TRUE,
        "cache-max-images", 6u, "cache-max-mib", 2048u, "cache-max-image-mib", 256u,
        "preload-count", 5u, "preload-workers", 5u, "limit-quality", FALSE,
        "wrap-images", TRUE, "default-zoom", RSTTO_SCALE_FIT_TO_VIEW,
        "file-manager-sort-sync", FALSE, "file-manager-sort-sync-once", FALSE, NULL);
    s.list = rstto_image_list_new ();
    s.window = rstto_main_window_new (s.list, FALSE);
    g_object_ref_sink (s.window);
    gtk_widget_show_all (s.window);
    gtk_container_foreach (GTK_CONTAINER (s.window), find_viewer, NULL);
    g_object_ref (viewer);
    cache = g_object_ref (rstto_image_viewer_get_cache (viewer));
    for (guint n = 0; n < G_N_ELEMENTS (s.files); n++)
    {
        gchar *path = g_strdup_printf ("%s/%02u.png", directory, n);
        GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 1024, 768);
        gdk_pixbuf_fill (pixbuf, 0x102030ff + (n << 24));
        g_assert_true (gdk_pixbuf_save (pixbuf, path, "png", NULL, NULL));
        g_object_unref (pixbuf);
        GFile *file = g_file_new_for_path (path);
        s.files[n] = rstto_file_new (file);
        g_object_unref (file); g_free (path);
    }
    /* Write all fixtures before installing monitors for any of them. */
    for (guint n = 0; n < G_N_ELEMENTS (s.files); n++)
        g_assert_true (rstto_image_list_add_file (s.list, s.files[n], NULL));
    s.iter = rstto_main_window_get_iter (RSTTO_MAIN_WINDOW (s.window));
    rstto_image_list_iter_set_position (s.iter, 0);
    wait_loaded ();
    g_assert_cmpint (rstto_image_list_get_n_images (s.list), ==, 16);
    s.last = rstto_image_viewer_get_file (viewer);
    g_signal_connect (viewer, "frame-presented", G_CALLBACK (scrolling_presented), &s);
    g_mutex_lock (&scrolling_mutex);
    scrolling_phase = GPOINTER_TO_UINT (data);
    scrolling_tick = scrolling_cancelled = scrolling_entered = 0;
    scrolling_gate = TRUE;
    g_mutex_unlock (&scrolling_mutex);
    s.running = TRUE;
    if (GPOINTER_TO_UINT (data) == 0)
    {
        navigation_boundaries (&s, directory);
        goto cleanup;
    }
    g_timeout_add (250, scrolling_input, &s);
    gint64 until = g_get_monotonic_time () + 20 * G_USEC_PER_SEC;
    while (scrolling_tick < 40)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    wait_loaded ();
    g_assert_true (g_file_equal (rstto_file_get_file (rstto_image_viewer_get_file (viewer)),
                                 rstto_file_get_file (s.files[s.latest])));
    scrolling_check_limits ();
    g_test_message ("phase=%u presented=%u maximum_gap_ticks=%u cancelled_demand=%u",
        scrolling_phase, s.presentations, s.maximum_gap, scrolling_cancelled);
    s.running = FALSE;
    /* A queued target returning to the already displayed image must settle. */
    rstto_image_list_iter_set_position (s.iter, (s.latest + 1) % 16);
    rstto_image_list_iter_set_position (s.iter, s.latest);
    wait_loaded ();
    g_assert_true (g_file_equal (rstto_file_get_file (rstto_image_viewer_get_file (viewer)),
                                 rstto_file_get_file (s.files[s.latest])));
cleanup:
    gtk_widget_destroy (s.window);
    wait_loaded ();
    g_clear_object (&viewer);
    g_object_unref (s.window); g_object_unref (s.list); g_object_unref (cache); g_object_unref (settings);
    for (guint n = 0; n < G_N_ELEMENTS (s.files); n++)
    {
        g_file_delete (rstto_file_get_file (s.files[n]), NULL, NULL);
        g_object_unref (s.files[n]);
    }
    if (s.replacement != NULL)
    {
        g_file_delete (rstto_file_get_file (s.replacement), NULL, NULL);
        g_object_unref (s.replacement);
        g_rmdir (s.replacement_dir); g_free (s.replacement_dir);
    }
    g_rmdir (directory); g_free (directory);
}


static gboolean benchmark_scrolling;
typedef struct
{
    RsttoImageListIter *iter;
    guint inputs, frames, latest;
    gint64 start, last_input, last_frame, final_frame, max_gap;
    guint64 peak_accounted;
    RsttoFile *last;
} ScrollBenchmark;

static void
benchmark_presented (RsttoImageViewer *widget, ScrollBenchmark *b)
{
    gint64 now = g_get_monotonic_time ();
    RsttoFile *file = rstto_image_viewer_get_file (widget);
    if (file == b->last)
        return;
    b->last = file;
    b->frames++;
    b->max_gap = MAX (b->max_gap, now - b->last_frame);
    b->last_frame = now;
    if (file == rstto_image_list_iter_get_file (b->iter))
        b->final_frame = now;
    RsttoCacheStats stats;
    rstto_image_cache_get_stats (cache, &stats);
    b->peak_accounted = MAX (b->peak_accounted, stats.resident_bytes + stats.reserved_bytes);
    g_print ("scroll-frame input=%u frame=%u elapsed_ms=%.3f accounted_mib=%.3f hits=%" G_GUINT64_FORMAT
             " decodes=%" G_GUINT64_FORMAT " renders=%" G_GUINT64_FORMAT " render_hits=%" G_GUINT64_FORMAT "\n",
        b->inputs, b->frames, (now - b->start) / 1000.0,
        (stats.resident_bytes + stats.reserved_bytes) / (1024.0 * 1024),
        stats.hits, stats.decodes, stats.renders, stats.render_hits);
}

static gboolean
benchmark_input (gpointer data)
{
    ScrollBenchmark *b = data;
    b->inputs++;
    b->latest = (b->inputs <= 24 ? b->inputs : 48 - b->inputs) % 16;
    b->last_input = g_get_monotonic_time ();
    rstto_image_list_iter_set_position (b->iter, b->latest);
    scrolling_check_limits ();
    return b->inputs < 48 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

static void
benchmark_scroll_sequence (RsttoImageListIter *iter)
{
    ScrollBenchmark b = { .iter = iter, .last = rstto_image_viewer_get_file (viewer) };
    b.start = b.last_frame = g_get_monotonic_time ();
    gulong handler = g_signal_connect (viewer, "frame-presented", G_CALLBACK (benchmark_presented), &b);
    g_timeout_add (250, benchmark_input, &b);
    gint64 until = b.start + 30 * G_USEC_PER_SEC;
    while (b.inputs < 48 || rstto_image_viewer_is_busy (viewer))
    {
        g_main_context_iteration (NULL, FALSE);
        RsttoCacheStats stats;
        rstto_image_cache_get_stats (cache, &stats);
        b.peak_accounted = MAX (b.peak_accounted, stats.resident_bytes + stats.reserved_bytes);
        scrolling_check_limits ();
        g_assert_cmpint (g_get_monotonic_time (), <, until);
        g_usleep (1000);
    }
    g_assert_true (rstto_image_viewer_get_file (viewer) == rstto_image_list_iter_get_file (iter));
    g_assert_cmpuint (b.frames, >, 6);
    g_print ("scroll-summary inputs=%u frames=%u max_gap_ms=%.3f final_target_ms=%.3f peak_accounted_mib=%.3f viewport=%dx%d\n",
        b.inputs, b.frames, b.max_gap / 1000.0, MAX (0, b.final_frame - b.last_input) / 1000.0,
        b.peak_accounted / (1024.0 * 1024), gtk_widget_get_allocated_width (GTK_WIDGET (viewer)),
        gtk_widget_get_allocated_height (GTK_WIDGET (viewer)));
    g_signal_handler_disconnect (viewer, handler);
    wait_loaded ();
}

/* Explicit manual benchmark; the normal test suite keeps its small fixtures. */
static gboolean benchmark_smoothing;
static gboolean benchmark_cold;

static void
frame_done (GdkFrameClock *clock, gboolean *done)
{
    *done = TRUE;
}

static void
wait_frame (void)
{
    gboolean done = FALSE;
    GdkFrameClock *clock = gtk_widget_get_frame_clock (GTK_WIDGET (viewer));
    g_assert_nonnull (clock);
    gulong handler = g_signal_connect (clock, "after-paint", G_CALLBACK (frame_done), &done);
    gtk_widget_queue_draw (GTK_WIDGET (viewer));
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (!done)
    {
        g_main_context_iteration (NULL, FALSE);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
        g_usleep (1000);
    }
    g_signal_handler_disconnect (clock, handler);
}

static void
benchmark_navigation (void)
{
    gchar *directory = g_dir_make_tmp ("ristretto-navigation-profile-XXXXXX", NULL);
    RsttoSettings *settings = rstto_settings_new ();
    g_object_set (settings, "maximize-on-startup", FALSE, "show-error-missing-thumbnailer", FALSE,
                  "window-width", g_getenv ("RSTTO_BENCH_LARGE") ? 5120u : (benchmark_scrolling ? 2800u : 1280u),
                  "window-height", g_getenv ("RSTTO_BENCH_LARGE") ? 2792u : (benchmark_scrolling ? 1900u : 900u),
                  "cache-enabled", TRUE, "cache-max-images", 6u, "cache-max-mib", 2048u,
                  "cache-max-image-mib", 256u, "preload-count", benchmark_cold ? 0u : 5u, "preload-workers", 5u,
                  "limit-quality", FALSE, "wrap-images", TRUE,
                  "enable-smoothing", benchmark_smoothing, "file-manager-sort-sync", FALSE,
                  "file-manager-sort-sync-once", FALSE, NULL);
    RsttoImageList *list = rstto_image_list_new ();
    GtkWidget *window = rstto_main_window_new (list, FALSE);
    g_object_ref_sink (window);
    gtk_widget_show_all (window);
    gtk_container_foreach (GTK_CONTAINER (window), find_viewer, NULL);
    g_assert_nonnull (viewer);
    g_object_ref (viewer);
    cache = g_object_ref (rstto_image_viewer_get_cache (viewer));
    RsttoFile *files[16];
    guint image_count = benchmark_scrolling ? 16 : 8;
    for (guint n = 0; n < image_count; n++)
    {
        gchar *path = g_strdup_printf ("%s/%u.jpg", directory, n);
        GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 7728, 5152);
        for (gint y = 0; y < 5152; y++)
        {
            guchar *row = gdk_pixbuf_get_pixels (pixbuf) + y * gdk_pixbuf_get_rowstride (pixbuf);
            for (gint x = 0; x < 7728; x++)
            {
                row[3 * x] = (x + n * 71) % 256;
                row[3 * x + 1] = (y + n * 97) % 256;
                row[3 * x + 2] = (x / 8 + y / 8) % 256;
            }
        }
        g_assert_true (gdk_pixbuf_save (pixbuf, path, "jpeg", NULL, "quality", "90", NULL));
        g_object_unref (pixbuf);
        GFile *file = g_file_new_for_path (path);
        files[n] = rstto_file_new (file);
        g_object_unref (file);
        g_free (path);
    }
    for (guint n = 0; n < image_count; n++)
        g_assert_true (rstto_image_list_add_file (list, files[n], NULL));
    guint capacity, targets, workers;
    gboolean wrap;
    g_object_get (settings, "cache-max-images", &capacity, "preload-count", &targets,
                  "preload-workers", &workers, "wrap-images", &wrap, NULL);
    g_assert_cmpint (rstto_image_list_get_n_images (list), ==, image_count);
    g_assert_cmpuint (capacity, ==, 6);
    g_assert_cmpuint (targets, ==, benchmark_cold ? 0 : 5);
    g_assert_cmpuint (workers, ==, 5);
    g_assert_true (wrap);
    g_print ("benchmark images=%u slots=%u targets=%u workers=%u wrap=%d\n", image_count, capacity, targets, workers, wrap);
    RsttoImageListIter *iter = rstto_main_window_get_iter (RSTTO_MAIN_WINDOW (window));
    rstto_image_list_iter_set_position (iter, 0);
    wait_loaded ();
    wait_frame ();
    RsttoCacheStats before, after;
    rstto_image_cache_get_stats (cache, &before);
    g_assert_cmpuint (before.entries, >=, benchmark_cold ? 1 : 2);
    if (benchmark_scrolling)
        benchmark_scroll_sequence (iter);
    for (guint n = 0; !benchmark_scrolling && n < 8; n++)
    {
        gint64 start = g_get_monotonic_time ();
        rstto_image_list_iter_set_position (iter, (n + 1) % (benchmark_cold ? 8 : 2));
        gint64 until = start + 10 * G_USEC_PER_SEC;
        while (rstto_image_viewer_is_busy (viewer))
        {
            g_main_context_iteration (NULL, FALSE);
            g_assert_cmpint (g_get_monotonic_time (), <, until);
            g_usleep (1000);
        }
        wait_frame ();
        rstto_image_cache_get_stats (cache, &after);
        /* Other directory images may be decoded speculatively. Each selected
         * image must instead be a validated cache hit. */
        if (benchmark_cold)
            g_assert_cmpuint (after.decodes, ==, before.decodes + 1);
        else
        {
            g_assert_cmpuint (after.hits, ==, before.hits + 1);
            g_assert_cmpuint (after.render_hits, >, before.render_hits);
        }
        g_assert_cmpuint (after.resident_bytes + after.reserved_bytes, <=, (guint64) 2048 * 1024 * 1024);
        g_assert_cmpuint (after.entries, <=, 6);
        g_print ("navigation sample=%u smoothing=%d elapsed_ms=%.3f hits=%" G_GUINT64_FORMAT
                 " decodes=%" G_GUINT64_FORMAT " viewport=%dx%d\n", n, benchmark_smoothing,
                 (g_get_monotonic_time () - start) / 1000.0, after.hits, after.decodes,
                 gtk_widget_get_allocated_width (GTK_WIDGET (viewer)), gtk_widget_get_allocated_height (GTK_WIDGET (viewer)));
        wait_loaded (); /* Background dwell is outside the navigation sample. */
        rstto_image_cache_get_stats (cache, &before);
    }
    gtk_widget_destroy (window);
    wait_loaded ();
    g_object_unref (window);
    g_clear_object (&viewer);
    g_object_unref (list);
    g_object_unref (cache);
    g_object_unref (settings);
    for (guint n = 0; n < image_count; n++)
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
    benchmark_scrolling = argc == 2 && g_str_equal (argv[1], "--benchmark-scrolling");
    gboolean benchmark = argc == 2 && (benchmark_scrolling || g_str_equal (argv[1], "--benchmark-navigation=good")
                                       || g_str_equal (argv[1], "--benchmark-navigation=nearest"));
    if (benchmark)
    {
        benchmark_cold = g_getenv ("RSTTO_BENCH_COLD") != NULL;
        benchmark_smoothing = benchmark_scrolling || g_str_equal (argv[1], "--benchmark-navigation=good");
        argc = 1;
        argv[1] = NULL;
    }
    g_test_init (&argc, &argv, NULL);
    if (!rstto_image_probe_available () || !gtk_init_check (NULL, NULL))
    {
        g_print ("Glycin probing and a graphical display are required for the viewer integration test.\n");
        g_rmdir (config);
        g_free (config);
        return 77;
    }
    rstto_settings_set_xfconf_disabled (TRUE);
    if (benchmark)
        g_test_add_func ("/viewer/profile-navigation", benchmark_navigation);
    else
    {
        g_test_add_func ("/viewer/navigation-and-lifetime", test_viewer);
        g_test_add_data_func ("/viewer/sustained-decode", GUINT_TO_POINTER (4), sustained_scrolling);
        g_test_add_data_func ("/viewer/sustained-conversion", GUINT_TO_POINTER (11), sustained_scrolling);
        g_test_add_data_func ("/viewer/sustained-render", GUINT_TO_POINTER (9), sustained_scrolling);
        g_test_add_data_func ("/viewer/navigation-boundaries", NULL, sustained_scrolling);
    }
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

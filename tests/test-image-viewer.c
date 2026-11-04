/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util.h"
#include "image_probe.h"
#include "image_viewer.h"
#include "main_window.h"
#include "settings.h"

#include <glib/gstdio.h>
#include <math.h>

void
rstto_test_worker_checkpoint (gint phase, gboolean demand, GCancellable *cancel, gboolean entering);

static RsttoImageViewer *viewer;
static RsttoImageCache *cache;
static void
wait_frame (void);


/**
 * find_viewer:
 *
 * Locate the image viewer recursively inside the test window's widget
 * hierarchy.
 */
static void
find_viewer (GtkWidget *widget, gpointer unused)
{
    if (RSTTO_IS_IMAGE_VIEWER (widget))
        viewer = RSTTO_IMAGE_VIEWER (widget);
    else if (GTK_IS_CONTAINER (widget))
        gtk_container_foreach (GTK_CONTAINER (widget), find_viewer, NULL);
}


/**
 * wait_loaded:
 *
 * Dispatch GTK until foreground image preparation completes or the test
 * deadline expires.
 */
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


/**
 * add_exif_orientation:
 *
 * Insert a small EXIF orientation segment into a JPEG fixture without
 * re-encoding its pixels.
 */
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


/**
 * test_viewer:
 *
 * Exercise viewer rendering, orientation, settings, printing and foreground
 * fallback through an isolated window.
 */
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
#ifdef HAVE_PLACEBO
    if (rstto_settings_get_startup_gpu_enabled (settings))
    {
        RsttoGpuStats gpu_before, gpu_after;
        rstto_image_cache_get_gpu_stats (cache, &gpu_before);
        g_assert_cmpuint (gpu_before.completed, >, 0);
        rstto_image_viewer_set_scale (viewer, 2.0);
        wait_frame ();
        wait_loaded ();
        wait_frame ();
        rstto_image_cache_get_gpu_stats (cache, &gpu_after);
        g_assert_cmpuint (gpu_after.completed, >, gpu_before.completed);
        rstto_image_cache_get_stats (cache, &after);
        g_assert_cmpuint (after.decodes, ==, before.decodes);
        rstto_image_viewer_set_scale (viewer, -1.0);
        wait_frame ();
        wait_loaded ();
        wait_frame ();
        rstto_image_cache_get_stats (cache, &before);
    }
#endif
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


/**
 * rstto_test_worker_checkpoint:
 *
 * Hold selected decode or preparation phases behind a deterministic barrier
 * without releasing task ownership.
 */
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


/**
 * scrolling_check_limits:
 *
 * Assert cache byte, image, speculative-slot and shared-task limits during
 * viewer input.
 */
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


/**
 * scrolling_presented:
 *
 * Record committed selections and assert progress and resource limits during
 * sustained navigation.
 */
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


/**
 * scrolling_input:
 *
 * Inject the next timed navigation event for the sustained-scrolling
 * regression.
 */
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


/**
 * scrolling_hold:
 *
 * Arm a worker-phase barrier before starting a navigation or view transition.
 */
static void
scrolling_hold (guint phase)
{
    g_mutex_lock (&scrolling_mutex);
    scrolling_phase = phase;
    scrolling_tick = scrolling_entered = 0;
    scrolling_gate = TRUE;
    g_mutex_unlock (&scrolling_mutex);
}


/**
 * scrolling_await_worker:
 *
 * Wait for the armed checkpoint while keeping the GTK main context responsive.
 */
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


/**
 * scrolling_release:
 *
 * Release the worker checkpoint and allow its retained resources to drain.
 */
static void
scrolling_release (void)
{
    g_mutex_lock (&scrolling_mutex);
    scrolling_gate = FALSE;
    g_cond_broadcast (&scrolling_cond);
    g_mutex_unlock (&scrolling_mutex);
    wait_loaded ();
}


/**
 * zoom_latest_view:
 *
 * Delay preparation while issuing successive zoom changes and require the
 * latest desired scale to remain authoritative.
 */
static void
zoom_latest_view (void)
{
#ifndef HAVE_PLACEBO
    g_test_skip ("Built without GPU rendering");
    return;
#endif
    if (g_strcmp0 (g_getenv ("RISTRETTO_RENDERER"), "placebo") != 0)
    {
        g_test_skip ("GPU zoom regression");
        return;
    }
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 30 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed ();
        return;
    }
    RsttoSettings *settings = rstto_settings_new ();
    g_object_set (settings, "preload-count", 0u, "show-error-missing-thumbnailer", FALSE, NULL);
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    viewer = RSTTO_IMAGE_VIEWER (rstto_image_viewer_new ());
    gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET (viewer));
    gtk_container_add (GTK_CONTAINER (window), scroll);
    gtk_widget_show_all (window);
    cache = g_object_ref (rstto_image_viewer_get_cache (viewer));
    gchar *directory = g_dir_make_tmp ("ristretto-zoom-test-XXXXXX", NULL);
    gchar *path = g_build_filename (directory, "fixture.png", NULL);
    /* Keep the logical fixture size fixed so the scrollbar assertions also
     * exercise overflowing content on a high-DPI display. */
    gint monitor_scale = gtk_widget_get_scale_factor (GTK_WIDGET (viewer));
    GdkPixbuf *pixels = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 128 * monitor_scale, 96 * monitor_scale);
    gdk_pixbuf_fill (pixels, 0x20a050ff);
    g_assert_true (gdk_pixbuf_save (pixels, path, "png", NULL, NULL));
    g_object_unref (pixels);
    GFile *gf = g_file_new_for_path (path);
    RsttoFile *file = rstto_file_new (gf);
    rstto_image_viewer_set_file (viewer, file, 2.0, RSTTO_SCALE_NONE, RSTTO_IMAGE_ORIENT_NONE);
    wait_loaded ();
    RsttoCacheStats before, after;
    rstto_image_cache_get_stats (cache, &before);
    /* Delay verification after preparation. New input must survive completion
     * of an older zoom, including repeated keys and a direction reversal. */
    guint paints = rstto_image_viewer_test_unprepared_paints (viewer);
    const double first[] = { 2.2, 2.0, 2.2, 2.42, 4.8, 6.2 };
    const double latest[] = { 2.42, 1.8, 1.9, 2.662, 5.5, 4.5 };
    for (guint i = 0; i < G_N_ELEMENTS (first); i++)
    {
        scrolling_hold (10);
        rstto_image_viewer_set_scale (viewer, first[i]);
        scrolling_await_worker ();
        rstto_image_viewer_set_scale (viewer, latest[i]);
        g_assert_cmpfloat (rstto_image_viewer_get_scale (viewer), ==, latest[i]);
        scrolling_release ();
        g_assert_cmpfloat (rstto_image_viewer_get_scale (viewer), ==, latest[i]);
        g_assert_cmpfloat (rstto_file_get_scale (file), ==, latest[i]);
    }
    rstto_image_viewer_set_scale (viewer, 8.0);
    wait_loaded ();
    GtkAdjustment *h = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (viewer));
    GtkAdjustment *v = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (viewer));
    scrolling_hold (10);
    gtk_adjustment_set_value (h, 40);
    gtk_adjustment_set_value (v, 30);
    gtk_widget_queue_draw (GTK_WIDGET (viewer));
    scrolling_await_worker ();
    gtk_adjustment_set_value (h, 100);
    gtk_adjustment_set_value (v, 80);
    scrolling_release ();
    g_assert_cmpfloat (gtk_adjustment_get_value (h), ==, 100);
    g_assert_cmpfloat (gtk_adjustment_get_value (v), ==, 80);
    g_assert_cmpfloat (rstto_file_get_h_adjustment (file), ==, 100);
    g_assert_cmpfloat (rstto_file_get_v_adjustment (file), ==, 80);
    /* A genuine renderer failure must also prepare the fallback off-thread. */
    rstto_image_viewer_test_fail_gpu (viewer);
    scrolling_hold (10);
    rstto_image_viewer_set_scale (viewer, 4.0);
    scrolling_await_worker ();
    rstto_image_viewer_set_scale (viewer, 3.8);
    scrolling_release ();
    g_assert_cmpfloat (rstto_image_viewer_get_scale (viewer), ==, 3.8);
    g_assert_cmpuint (rstto_image_viewer_test_unprepared_paints (viewer), ==, paints);
    rstto_image_cache_get_stats (cache, &after);
    g_assert_cmpuint (after.decodes, ==, before.decodes);
    gtk_widget_destroy (window);
    viewer = NULL;
    wait_loaded ();
    g_object_unref (cache);
    cache = NULL;
    g_object_unref (file);
    g_object_unref (gf);
    g_object_unref (settings);
    g_remove (path);
    g_rmdir (directory);
    g_free (path);
    g_free (directory);
}


/**
 * drag_button:
 *
 * Inject a left-button press or release with modifiers into the test viewer.
 *
 * Drive the real handlers without injecting input into another desktop window.
 */
static void
drag_button (gboolean press, guint modifiers)
{
    GtkWidget *widget = GTK_WIDGET (viewer);
    GdkEventButton event = { 0 };
    event.type = press ? GDK_BUTTON_PRESS : GDK_BUTTON_RELEASE;
    event.window = gtk_widget_get_window (widget);
    event.button = 1;
    event.x = 320;
    event.y = 240;
    event.state = modifiers;
    if (press)
        GTK_WIDGET_GET_CLASS (widget)->button_press_event (widget, &event);
    else
        GTK_WIDGET_GET_CLASS (widget)->button_release_event (widget, &event);
}


/**
 * drag_motion:
 *
 * Inject held-button motion relative to the recorded drag origin.
 */
static void
drag_motion (double dx, double dy)
{
    GtkWidget *widget = GTK_WIDGET (viewer);
    GdkEventMotion event = { 0 };
    event.type = GDK_MOTION_NOTIFY;
    event.window = gtk_widget_get_window (widget);
    event.state = GDK_BUTTON1_MASK;
    event.x = 320 - dx;
    event.y = 240 - dy;
    GTK_WIDGET_GET_CLASS (widget)->motion_notify_event (widget, &event);
}

typedef struct
{
    guint frames;
    RsttoRenderKey first, last;
    double desired_h, desired_v;
    RsttoFile *expected_file;
} DragFrames;


/**
 * drag_presented:
 *
 * Record completed drag frames and verify monotonically advancing render
 * revisions and bounded accounting.
 */
static void
drag_presented (RsttoImageViewer *widget, DragFrames *frames)
{
    RsttoRenderKey key;
    g_assert_true (rstto_image_viewer_test_render_key (widget, &key));
    if (frames->frames == 0)
        frames->first = key;
    else
        g_assert_cmpuint (key.view_revision, >=, frames->last.view_revision);
    frames->last = key;
    frames->frames++;
    RsttoFile *file = rstto_image_viewer_get_file (widget);
    if (frames->expected_file != NULL)
        g_assert_true (file == frames->expected_file);
    g_assert_cmpfloat (rstto_file_get_h_adjustment (file), ==, frames->desired_h);
    g_assert_cmpfloat (rstto_file_get_v_adjustment (file), ==, frames->desired_v);
    scrolling_check_limits ();
}


/**
 * drag_continuous:
 *
 * Drive motion faster than rendering, requiring frames before release and
 * exact final settling without pan or zoom rollback.
 */
static void
drag_continuous (gconstpointer data)
{
#ifndef HAVE_PLACEBO
    g_test_skip ("Built without GPU rendering");
    return;
#endif
    if (g_strcmp0 (g_getenv ("RISTRETTO_RENDERER"), "placebo") != 0)
    {
        g_test_skip ("Asynchronous GPU/fallback dragging");
        return;
    }
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 30 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed ();
        return;
    }
    guint phase = GPOINTER_TO_UINT (data);
    RsttoSettings *settings = rstto_settings_new ();
    g_object_set (settings, "preload-count", 0u, "cache-max-images", 6u,
                  "cache-max-mib", 2048u, "cache-max-image-mib", 256u, "preload-workers", 5u,
                  "limit-quality", FALSE, "show-error-missing-thumbnailer", FALSE, NULL);
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size (GTK_WINDOW (window), 640, 480);
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    viewer = RSTTO_IMAGE_VIEWER (rstto_image_viewer_new ());
    gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET (viewer));
    gtk_container_add (GTK_CONTAINER (window), scroll);
    gtk_widget_show_all (window);
    cache = g_object_ref (rstto_image_viewer_get_cache (viewer));
    gchar *directory = g_dir_make_tmp ("ristretto-drag-test-XXXXXX", NULL);
    gchar *path = g_build_filename (directory, "alpha.png", NULL);
    GdkPixbuf *pixels = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 1024, 768);
    gdk_pixbuf_fill (pixels, 0x20a05080);
    g_assert_true (gdk_pixbuf_save (pixels, path, "png", NULL, NULL));
    g_object_unref (pixels);
    GFile *gf = g_file_new_for_path (path);
    RsttoFile *file = rstto_file_new (gf);
    rstto_image_viewer_set_file (viewer, file, 2.0, RSTTO_SCALE_NONE, RSTTO_IMAGE_ORIENT_NONE);
    wait_loaded ();
    if (phase == 0)
        rstto_image_viewer_test_fail_gpu (viewer);
    GtkAdjustment *h = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (viewer));
    GtkAdjustment *v = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (viewer));
    guint paints = rstto_image_viewer_test_unprepared_paints (viewer);
    RsttoCacheStats before, after;
    rstto_image_cache_get_stats (cache, &before);
    RsttoGpuStats gpu_before, gpu_after;
    rstto_image_cache_get_gpu_stats (cache, &gpu_before);
    for (guint n = 0; n < 8; n++)
    {
        /* All EXIF orientations, transparency, reversal and diagonal movement.
         * Even rounds stop with LMB held; odd rounds release before completion. */
        rstto_image_viewer_set_orientation (viewer, n + 1);
        gtk_adjustment_set_value (h, 100);
        gtk_adjustment_set_value (v, 100);
        wait_frame ();
        wait_loaded ();
        wait_frame ();
        drag_button (TRUE, 0);
        scrolling_hold (phase != 0 ? phase : 10);
        gint sign = n % 2 ? -1 : 1;
        drag_motion (20 * sign, 10 * sign);
        scrolling_await_worker ();
        drag_motion (40 * sign, 20 * sign);
        wait_frame (); /* Latest desired revision is observed while work is held. */
        DragFrames frames = { .desired_h = 100 + 40 * sign, .desired_v = 100 + 20 * sign };
        gulong handler = g_signal_connect (viewer, "frame-presented", G_CALLBACK (drag_presented), &frames);
        if (n % 2)
            drag_button (FALSE, 0);
        scrolling_release ();
        /* No extra input or explicit repaint may be needed to settle. */
        g_assert_cmpuint (frames.frames, >=, 2);
        g_assert_cmpint (frames.first.h_adjust, ==, 100 + 20 * sign);
        g_assert_cmpint (frames.first.v_adjust, ==, 100 + 10 * sign);
        g_assert_cmpint (frames.last.h_adjust, ==, frames.desired_h);
        g_assert_cmpint (frames.last.v_adjust, ==, frames.desired_v);
        g_signal_handler_disconnect (viewer, handler);
        if (!(n % 2))
            drag_button (FALSE, 0);
    }
    /* A zoom, rotation or allocation change during the admitted pan is not
     * a preview: it must be discarded before any presentation. */
    for (guint change = 0; change < 3; change++)
    {
        rstto_image_viewer_set_orientation (viewer, 1);
        rstto_image_viewer_set_scale (viewer, 2.0);
        gtk_adjustment_set_value (h, 100);
        gtk_adjustment_set_value (v, 100);
        wait_frame ();
        wait_loaded ();
        wait_frame ();
        drag_button (TRUE, 0);
        scrolling_hold (10);
        drag_motion (20, 10);
        scrolling_await_worker ();
        RsttoRenderKey old;
        g_assert_true (rstto_image_viewer_test_render_key (viewer, &old));
        if (change == 0)
            rstto_image_viewer_set_scale (viewer, 0.9);
        if (change == 1)
            rstto_image_viewer_set_orientation (viewer, 6);
        if (change == 2)
            gtk_window_resize (GTK_WINDOW (window), 700, 500);
        wait_frame ();
        DragFrames frames = { .desired_h = gtk_adjustment_get_value (h), .desired_v = gtk_adjustment_get_value (v) };
        gulong handler = g_signal_connect (viewer, "frame-presented", G_CALLBACK (drag_presented), &frames);
        scrolling_release ();
        wait_frame ();
        wait_loaded ();
        wait_frame ();
        g_assert_cmpuint (frames.frames, >=, 1);
        if (change == 0)
            g_assert_cmpint (frames.first.render_width, !=, old.render_width);
        if (change == 1)
            g_assert_cmpint (frames.first.orientation, ==, 6);
        if (change == 2)
            g_assert_cmpint (frames.first.width, !=, old.width);
        g_signal_handler_disconnect (viewer, handler);
        drag_button (FALSE, 0);
    }
    /* Boundary clamping settles with no release or further input. */
    drag_button (TRUE, 0);
    drag_motion (-10000, -10000);
    wait_frame ();
    wait_loaded ();
    wait_frame ();
    RsttoRenderKey key;
    g_assert_true (rstto_image_viewer_test_render_key (viewer, &key));
    g_assert_cmpint (key.h_adjust, ==, 0);
    g_assert_cmpint (key.v_adjust, ==, 0);
    drag_button (FALSE, 0);
    g_assert_cmpuint (rstto_image_viewer_test_unprepared_paints (viewer), ==, paints);
    rstto_image_cache_get_stats (cache, &after);
    g_assert_cmpuint (after.decodes, ==, before.decodes);
    rstto_image_cache_get_gpu_stats (cache, &gpu_after);
    if (phase != 0)
    {
        g_assert_cmpuint (gpu_after.uploads, ==, gpu_before.uploads);
        g_assert_cmpuint (gpu_after.source_hits, >, gpu_before.source_hits);
        g_assert_cmpuint (gpu_after.programs, ==, gpu_before.programs);
    }
    g_assert_cmpuint (gpu_after.peak_bytes, <=, RSTTO_GPU_MAX_BYTES);
    g_assert_cmpuint (gpu_after.peak_sources, <=, RSTTO_GPU_MAX_SOURCES);
    rstto_image_viewer_set_scale (viewer, 2.0);
    wait_frame ();
    wait_loaded ();
    wait_frame ();
    scrolling_hold (10);
    drag_button (TRUE, 0);
    drag_motion (20, 10);
    scrolling_await_worker ();
    gchar *other_path = g_build_filename (directory, "other.png", NULL);
    GFile *other_gf = g_file_new_for_path (other_path);
    g_assert_true (g_file_copy (gf, other_gf, 0, NULL, NULL, NULL, NULL));
    RsttoFile *other = rstto_file_new (other_gf);
    rstto_image_viewer_set_file (viewer, other, 1, RSTTO_SCALE_NONE, RSTTO_IMAGE_ORIENT_NONE);
    DragFrames frames = { .expected_file = other };
    gulong handler = g_signal_connect (viewer, "frame-presented", G_CALLBACK (drag_presented), &frames);
    scrolling_release ();
    wait_frame ();
    wait_loaded ();
    wait_frame ();
    g_assert_true (rstto_image_viewer_get_file (viewer) == other);
    g_assert_cmpuint (frames.frames, >=, 1);
    g_signal_handler_disconnect (viewer, handler);
    drag_button (FALSE, 0);
    /* Ensure the replacement is zoomed beyond the viewport at monitor scale 2
     * as well, then destroy while verification owns its next pan result. */
    rstto_image_viewer_set_scale (viewer, 2.0);
    wait_frame ();
    wait_loaded ();
    wait_frame ();
    scrolling_hold (10);
    drag_button (TRUE, 0);
    drag_motion (20, 10);
    scrolling_await_worker ();
    gtk_widget_destroy (window);
    viewer = NULL;
    scrolling_release ();
    rstto_image_cache_get_stats (cache, &after);
    g_assert_cmpuint (after.resident_bytes + after.reserved_bytes, ==, 0);
    g_object_unref (other);
    g_object_unref (other_gf);
    g_remove (other_path);
    g_free (other_path);
    g_object_unref (cache);
    cache = NULL;
    g_object_unref (file);
    g_object_unref (gf);
    g_object_unref (settings);
    g_remove (path);
    g_rmdir (directory);
    g_free (path);
    g_free (directory);
}


/**
 * navigation_boundaries:
 *
 * Exercise pending navigation across wraparound, removal and directory
 * replacement boundaries.
 */
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
    g_object_unref (file);
    g_object_unref (dir);
    g_free (path);
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


/**
 * sustained_scrolling:
 *
 * Inject sustained navigation and reversals while workers are delayed,
 * asserting presentation progress without cancellation starvation.
 */
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
        g_object_unref (file);
        g_free (path);
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
    g_object_unref (s.window);
    g_object_unref (s.list);
    g_object_unref (cache);
    g_object_unref (settings);
    for (guint n = 0; n < G_N_ELEMENTS (s.files); n++)
    {
        g_file_delete (rstto_file_get_file (s.files[n]), NULL, NULL);
        g_object_unref (s.files[n]);
    }
    if (s.replacement != NULL)
    {
        g_file_delete (rstto_file_get_file (s.replacement), NULL, NULL);
        g_object_unref (s.replacement);
        g_rmdir (s.replacement_dir);
        g_free (s.replacement_dir);
    }
    g_rmdir (directory);
    g_free (directory);
}


static gboolean benchmark_scrolling, benchmark_zoom, benchmark_zoom_stream, benchmark_drag;
static gint64 benchmark_last_presented;


/**
 * benchmark_frame:
 *
 * Record frame timing and resource peaks during benchmark presentation.
 */
static void
benchmark_frame (RsttoImageViewer *widget, gpointer unused)
{
    benchmark_last_presented = g_get_monotonic_time ();
}
typedef struct
{
    RsttoImageListIter *iter;
    guint inputs, frames, latest;
    gint64 start, last_input, last_frame, final_frame, max_gap;
    guint64 peak_accounted;
    RsttoFile *last;
    RsttoFile **files;
    gint64 selected_at[16];
} ScrollBenchmark;


/**
 * benchmark_presented:
 *
 * Record navigation latency for each committed benchmark selection.
 */
static void
benchmark_presented (RsttoImageViewer *widget, ScrollBenchmark *b)
{
    gint64 now = g_get_monotonic_time ();
    RsttoFile *file = rstto_image_viewer_get_file (widget);
    if (file == b->last)
        return;
    b->last = file;
    b->frames++;
    for (guint i = 0; i < 16; i++)
        if (file == b->files[i] && b->selected_at[i] != 0)
            g_print ("selection-presented input=%u image=%u latency_ms=%.3f latest=%d\n",
                     b->inputs, i, (now - b->selected_at[i]) / 1000.0,
                     file == rstto_image_list_iter_get_file (b->iter));
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


/**
 * benchmark_input:
 *
 * Advance the benchmark's image iterator at the configured scrolling interval.
 */
static gboolean
benchmark_input (gpointer data)
{
    ScrollBenchmark *b = data;
    b->inputs++;
    b->latest = (b->inputs <= 24 ? b->inputs : 48 - b->inputs) % 16;
    b->last_input = g_get_monotonic_time ();
    b->selected_at[b->latest] = b->last_input;
    rstto_image_list_iter_set_position (b->iter, b->latest);
    scrolling_check_limits ();
    return b->inputs < 48 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}


/**
 * benchmark_scroll_sequence:
 *
 * Measure timed scrolling over the fixture list and wait for the final
 * coalesced selection to settle.
 */
static void
benchmark_scroll_sequence (RsttoImageListIter *iter, RsttoImageList *list)
{
    RsttoFile *files[16];
    for (guint n = 0; n < 16; n++)
        files[n] = rstto_image_list_get_file_at_position (list, n);
    ScrollBenchmark b = { .iter = iter, .last = rstto_image_viewer_get_file (viewer), .files = files };
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


/**
 * frame_done:
 *
 * Mark completion of one GTK after-paint cycle for benchmark synchronization.
 */
static void
frame_done (GdkFrameClock *clock, gboolean *done)
{
    *done = TRUE;
}


/**
 * wait_frame:
 *
 * Wait for the next painted frame without blocking GTK event dispatch.
 */
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

typedef struct
{
    guint inputs, frames;
    double desired;
    gint64 start, input_at, last_frame, max_gap;
} ZoomBenchmark;


/**
 * benchmark_zoom_presented:
 *
 * Collect completed zoom-frame latency and update the benchmark's progress
 * counters.
 */
static void
benchmark_zoom_presented (RsttoImageViewer *widget, ZoomBenchmark *b)
{
    gint64 now = g_get_monotonic_time ();
    b->frames++;
    b->max_gap = MAX (b->max_gap, now - b->last_frame);
    b->last_frame = now;
    double actual = rstto_image_viewer_get_scale (widget);
    g_print ("zoom-presented input=%u scale=%.6f desired=%.6f latency_ms=%.3f latest=%d\n",
             b->inputs, actual, b->desired, (now - b->input_at) / 1000.0, fabs (actual - b->desired) < 1e-8);
}


/**
 * benchmark_zoom_input:
 *
 * Inject successive zoom commands at the benchmark cadence.
 */
static gboolean
benchmark_zoom_input (gpointer data)
{
    ZoomBenchmark *b = data;
    b->inputs++;
    b->desired = .12 * pow (1.1, b->inputs <= 24 ? b->inputs : 48 - b->inputs);
    b->input_at = g_get_monotonic_time ();
    rstto_image_viewer_set_scale (viewer, b->desired);
    scrolling_check_limits ();
    return b->inputs < 48 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}


/**
 * benchmark_zoom_sequence:
 *
 * Measure sustained zoom and final settling using the same prepared
 * full-resolution image.
 */
static void
benchmark_zoom_sequence (void)
{
    ZoomBenchmark b = { 0 };
    b.start = b.input_at = b.last_frame = g_get_monotonic_time ();
    gulong handler = g_signal_connect (viewer, "frame-presented", G_CALLBACK (benchmark_zoom_presented), &b);
    g_timeout_add (166, benchmark_zoom_input, &b);
    while (b.inputs < 48)
    {
        g_main_context_iteration (NULL, FALSE);
        g_assert_cmpint (g_get_monotonic_time (), <, b.start + 45 * G_USEC_PER_SEC);
        g_usleep (1000);
    }
    wait_frame ();
    wait_loaded ();
    wait_frame ();
    g_print ("zoom-stream-summary inputs=%u frames=%u max_gap_ms=%.3f final_target_ms=%.3f actual=%.6f desired=%.6f\n",
             b.inputs, b.frames, b.max_gap / 1000.0, (b.last_frame - b.input_at) / 1000.0,
             rstto_image_viewer_get_scale (viewer), b.desired);
    g_signal_handler_disconnect (viewer, handler);
}

typedef struct
{
    guint inputs, frames, held_frames;
    gint64 start, last_frame, final_input;
    gboolean held;
} DragBenchmark;


/**
 * benchmark_drag_presented:
 *
 * Collect completed drag-frame gaps and final-view progress during timed
 * motion.
 */
static void
benchmark_drag_presented (RsttoImageViewer *widget, DragBenchmark *b)
{
    RsttoRenderKey key;
    g_assert_true (rstto_image_viewer_test_render_key (widget, &key));
    gint64 now = g_get_monotonic_time ();
    b->frames++;
    b->held_frames += b->held;
    g_print ("drag-presented input=%u held=%d gap_ms=%.3f h=%d v=%d revision=%" G_GUINT64_FORMAT "\n",
             b->inputs, b->held, (now - b->last_frame) / 1000.0, key.h_adjust, key.v_adjust, key.view_revision);
    b->last_frame = now;
}


/**
 * benchmark_drag_input:
 *
 * Inject the next motion event in the sustained-drag benchmark.
 */
static gboolean
benchmark_drag_input (gpointer data)
{
    DragBenchmark *b = data;
    gint64 now = g_get_monotonic_time ();
    if (now < b->start + (b->inputs + 1) * G_USEC_PER_SEC / 60)
        return G_SOURCE_CONTINUE;
    b->inputs++;
    double dx = b->inputs == 480 ? 0 : 200 * sin (b->inputs * G_PI / 120);
    double dy = b->inputs == 480 ? 0 : 120 * sin (b->inputs * G_PI / 80);
    drag_motion (dx, dy);
    scrolling_check_limits ();
    b->final_input = now;
    return b->inputs < 480 ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}


/**
 * benchmark_drag_sequence:
 *
 * Measure held-button dragging and final settling at a fixed input cadence.
 */
static void
benchmark_drag_sequence (void)
{
    /* Reserve no internal scrollbar area, retaining normal adjustments/pan.
     * This keeps the measured image viewport at exactly 5003 x 2664. */
    GtkWidget *scroll = gtk_widget_get_ancestor (GTK_WIDGET (viewer), GTK_TYPE_SCROLLED_WINDOW);
    g_assert_nonnull (scroll);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_EXTERNAL, GTK_POLICY_EXTERNAL);
    rstto_image_viewer_set_scale (viewer, 2.0);
    wait_frame ();
    wait_loaded ();
    wait_frame ();
    GtkAdjustment *h = gtk_scrollable_get_hadjustment (GTK_SCROLLABLE (viewer));
    GtkAdjustment *v = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (viewer));
    gtk_adjustment_set_value (h, 400);
    gtk_adjustment_set_value (v, 300);
    wait_frame ();
    wait_loaded ();
    wait_frame ();
    RsttoCacheStats before, after;
    rstto_image_cache_get_stats (cache, &before);
    DragBenchmark b = { .held = TRUE };
    b.start = b.last_frame = g_get_monotonic_time ();
    gulong handler = g_signal_connect (viewer, "frame-presented", G_CALLBACK (benchmark_drag_presented), &b);
    drag_button (TRUE, 0);
    g_timeout_add (1, benchmark_drag_input, &b);
    while (b.inputs < 480)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (500);
        g_assert_cmpint (g_get_monotonic_time (), <, b.start + 20 * G_USEC_PER_SEC);
    }
    b.held = FALSE; /* Stop measuring the motion interval, but keep LMB down. */
    RsttoRenderKey key;
    gboolean settled = FALSE;
    /* Observe natural settling without injecting a paint or another input. */
    gint64 deadline = b.final_input + 5 * G_USEC_PER_SEC;
    do
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (500);
        g_assert_true (rstto_image_viewer_test_render_key (viewer, &key));
        settled = key.h_adjust == (gint) gtk_adjustment_get_value (h)
                  && key.v_adjust == (gint) gtk_adjustment_get_value (v) && !rstto_image_viewer_is_busy (viewer);
    } while (!settled && g_get_monotonic_time () < deadline);
    g_assert_cmpint (gtk_widget_get_allocated_width (GTK_WIDGET (viewer)), ==, 5003);
    g_assert_cmpint (gtk_widget_get_allocated_height (GTK_WIDGET (viewer)), ==, 2664);
    rstto_image_cache_get_stats (cache, &after);
    g_print ("drag-summary inputs=%u frames=%u moving_frames=%u fps=%.3f final_ms=%.3f settled=%d decodes=%" G_GUINT64_FORMAT
             " viewport=%dx%d\n",
             b.inputs, b.frames, b.held_frames,
             b.held_frames * 1000000.0 / (b.final_input - b.start),
             (b.last_frame - b.final_input) / 1000.0, settled, after.decodes - before.decodes,
             gtk_widget_get_allocated_width (GTK_WIDGET (viewer)), gtk_widget_get_allocated_height (GTK_WIDGET (viewer)));
    drag_button (FALSE, 0);
    g_signal_handler_disconnect (viewer, handler);
}


/**
 * benchmark_first_frame:
 *
 * Record the first completed image frame for startup-latency measurement.
 */
static void
benchmark_first_frame (RsttoImageViewer *widget, gint64 *first)
{
    if (*first == 0 && rstto_image_viewer_get_file (widget) != NULL)
        *first = g_get_monotonic_time ();
}


/**
 * benchmark_navigation:
 *
 * Run the selected full-resolution presentation workload and report timing,
 * reuse and accounting counters.
 */
static void
benchmark_navigation (void)
{
    gint64 startup = g_get_monotonic_time (), first_frame = 0;
    const gchar *reused = g_getenv ("RSTTO_BENCH_FIXTURES");
    gchar *directory = reused != NULL ? g_strdup (reused) : g_dir_make_tmp ("ristretto-navigation-profile-XXXXXX", NULL);
    if (reused != NULL)
        g_mkdir_with_parents (directory, 0700);
    RsttoSettings *settings = rstto_settings_new ();
    g_object_set (settings, "maximize-on-startup", FALSE, "show-error-missing-thumbnailer", FALSE,
                  "window-width", g_getenv ("RSTTO_BENCH_LARGE") ? 5120u : (benchmark_scrolling ? 2800u : 1280u),
                  "window-height", g_getenv ("RSTTO_BENCH_LARGE") ? 2792u : (benchmark_scrolling ? 1900u : 900u),
                  "cache-enabled", TRUE, "cache-max-images", 6u, "cache-max-mib", 2048u,
                  "cache-max-image-mib", 256u, "preload-count", (benchmark_cold || benchmark_zoom) ? 0u : 5u, "preload-workers", 5u,
                  "limit-quality", FALSE, "wrap-images", TRUE,
                  "enable-smoothing", benchmark_smoothing, "file-manager-sort-sync", FALSE,
                  "file-manager-sort-sync-once", FALSE, NULL);
    RsttoImageList *list = rstto_image_list_new ();
    GtkWidget *window = rstto_main_window_new (list, FALSE);
    g_object_ref_sink (window);
    /* Desktop input must not become part of the programmed input sequence. */
    gtk_window_set_accept_focus (GTK_WINDOW (window), FALSE);
    gtk_window_set_focus_on_map (GTK_WINDOW (window), FALSE);
    gtk_widget_set_sensitive (window, FALSE);
    gtk_widget_show_all (window);
    gtk_container_foreach (GTK_CONTAINER (window), find_viewer, NULL);
    g_assert_nonnull (viewer);
    g_object_ref (viewer);
    cache = g_object_ref (rstto_image_viewer_get_cache (viewer));
    gulong first_handler = g_signal_connect (viewer, "frame-presented", G_CALLBACK (benchmark_first_frame), &first_frame);
    RsttoFile *files[16];
    guint image_count = benchmark_scrolling ? 16 : 8;
    for (guint n = 0; n < image_count; n++)
    {
        gchar *path = g_strdup_printf ("%s/%u.jpg", directory, n);
        if (!g_file_test (path, G_FILE_TEST_EXISTS))
        {
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
        }
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
    g_assert_cmpuint (targets, ==, (benchmark_cold || benchmark_zoom) ? 0 : 5);
    g_assert_cmpuint (workers, ==, 5);
    g_assert_true (wrap);
    g_print ("benchmark images=%u slots=%u targets=%u workers=%u wrap=%d\n", image_count, capacity, targets, workers, wrap);
    RsttoImageListIter *iter = rstto_main_window_get_iter (RSTTO_MAIN_WINDOW (window));
    rstto_image_list_iter_set_position (iter, 0);
    wait_loaded ();
    wait_frame ();
    g_assert_cmpint (rstto_image_list_iter_get_position (iter), ==, 0);
    g_assert_true (rstto_image_viewer_get_file (viewer) == rstto_image_list_iter_get_file (iter));
    RsttoCacheStats before, after;
    rstto_image_cache_get_stats (cache, &before);
    g_assert_cmpuint (before.entries, >=, (benchmark_cold || benchmark_zoom) ? 1 : 2);
    g_print ("startup first_image_ms=%.3f ready_ms=%.3f\n", (first_frame - startup) / 1000.0,
             (g_get_monotonic_time () - startup) / 1000.0);
    g_signal_handler_disconnect (viewer, first_handler);
    g_signal_connect (viewer, "frame-presented", G_CALLBACK (benchmark_frame), NULL);
    if (benchmark_drag)
        benchmark_drag_sequence ();
    if (benchmark_zoom_stream)
        benchmark_zoom_sequence ();
    if (benchmark_scrolling)
        benchmark_scroll_sequence (iter, list);
    if (benchmark_zoom && !benchmark_zoom_stream && !benchmark_drag)
    {
        const double zooms[] = { .1, .25, .5, 1, 1.5, 2, .517, .345, .5, 1, 2, .25 };
        for (guint n = 0; n < G_N_ELEMENTS (zooms); n++)
        {
            gint64 start = g_get_monotonic_time ();
            rstto_image_viewer_set_scale (viewer, zooms[n]);
            /* Paint/layout schedules derivative refresh, including scrollbar
             * geometry changes. Measure through the prepared result. */
            wait_frame ();
            wait_loaded ();
            wait_frame ();
            g_print ("zoom sample=%u scale=%.4f elapsed_ms=%.3f presented_ms=%.3f\n", n, zooms[n],
                     (g_get_monotonic_time () - start) / 1000.0,
                     (benchmark_last_presented - start) / 1000.0);
        }
    }
    for (guint n = 0; !benchmark_scrolling && !benchmark_zoom && n < 8; n++)
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
                 " decodes=%" G_GUINT64_FORMAT " viewport=%dx%d\n",
                 n, benchmark_smoothing,
                 (benchmark_last_presented > start ? benchmark_last_presented - start : g_get_monotonic_time () - start) / 1000.0,
                 after.hits, after.decodes,
                 gtk_widget_get_allocated_width (GTK_WIDGET (viewer)), gtk_widget_get_allocated_height (GTK_WIDGET (viewer)));
        wait_loaded (); /* Background dwell is outside the navigation sample. */
        rstto_image_cache_get_stats (cache, &before);
    }
#ifdef HAVE_PLACEBO
    RsttoGpuStats gpu_stats;
    rstto_image_cache_get_gpu_stats (cache, &gpu_stats);
    g_print ("gpu-summary peak_bytes=%" G_GUINT64_FORMAT " retained=%" G_GUINT64_FORMAT
             " uploads=%" G_GUINT64_FORMAT " submissions=%" G_GUINT64_FORMAT " discarded=%" G_GUINT64_FORMAT " peak_active=%u\n",
             gpu_stats.peak_bytes, gpu_stats.retained, gpu_stats.uploads, gpu_stats.submissions,
             gpu_stats.discarded, gpu_stats.peak_active);
    g_print ("gpu-source-summary hits=%" G_GUINT64_FORMAT " misses=%" G_GUINT64_FORMAT
             " bytes=%" G_GUINT64_FORMAT " sources=%u peak_sources=%u reserved=%" G_GUINT64_FORMAT "\n",
             gpu_stats.source_hits, gpu_stats.source_misses, gpu_stats.source_bytes, gpu_stats.sources,
             gpu_stats.peak_sources, gpu_stats.reserved);
#endif
    gtk_widget_destroy (window);
    wait_loaded ();
    g_object_unref (window);
    g_clear_object (&viewer);
    g_object_unref (list);
    g_object_unref (cache);
    g_object_unref (settings);
    for (guint n = 0; n < image_count; n++)
    {
        if (reused == NULL)
            g_file_delete (rstto_file_get_file (files[n]), NULL, NULL);
        g_object_unref (files[n]);
    }
    if (reused == NULL)
        g_rmdir (directory);
    g_free (directory);
}


/**
 * benchmark_scenario:
 *
 * Select a benchmark input scenario and invoke the common navigation harness.
 */
static void
benchmark_scenario (gconstpointer mode)
{
    const gchar *name = mode;
    benchmark_cold = g_str_equal (name, "cold");
    benchmark_scrolling = g_str_equal (name, "scroll");
    benchmark_zoom_stream = g_str_equal (name, "zoomstream");
    benchmark_drag = g_str_equal (name, "drag");
    benchmark_zoom = benchmark_zoom_stream || benchmark_drag || g_str_equal (name, "zoom");
    benchmark_smoothing = TRUE;
    g_print ("BENCH_PHASE name=%s\n", name);
    fflush (stdout);
    benchmark_navigation ();
}


typedef struct
{
    gboolean waiting, rejected;
    guint source_destroyed, derivative_destroyed;
    gboolean derivative;
    guint64 source_bytes;
    cairo_surface_t *source, *rendered; /* borrowed until the next delivery */
} RejectedDelivery;


/**
 * delivery_surface_destroyed:
 *
 * Count actual surface destruction without adding a reference that could hide
 * a missing display pin.
 */
static void
delivery_surface_destroyed (gpointer data)
{
    (*(guint *) data)++;
}


/**
 * delivery_observed:
 *
 * Observe borrowed surfaces and block the next image after the viewer declines
 * a refreshed result. An observer never accepts delivery on the viewer's behalf.
 */
static gboolean
delivery_observed (RsttoImageCache *unused_cache, RsttoImageData *image, RejectedDelivery *state)
{
    if (state->waiting)
    {
        state->waiting = FALSE;
        state->rejected = TRUE;
        scrolling_hold (4); /* The next foreground decode must not hide the gap. */
    }
    else if (!state->rejected)
    {
        g_assert_nonnull (image->surface);
        state->source = image->surface;
        state->rendered = image->rendered;
        state->source_bytes = (guint64) cairo_image_surface_get_stride (image->surface)
                              * cairo_image_surface_get_height (image->surface);
    }
    return FALSE;
}


/**
 * rejected_delivery:
 *
 * Refresh A with B pending, then draw and print A while B is blocked. Exercise
 * unchanged metadata, path replacement, CPU derivatives, destruction and forced
 * limit reduction without retaining the old pixels in the test itself.
 */
static void
rejected_delivery (gconstpointer data)
{
    if (!g_test_subprocess ())
    {
        g_test_trap_subprocess (NULL, 20 * G_USEC_PER_SEC, 0);
        g_test_trap_assert_passed ();
        return;
    }
    guint mode = GPOINTER_TO_UINT (data);
    RsttoSettings *settings = rstto_settings_new ();
    g_object_set (settings, "preload-count", 0u, "cache-max-images", 6u,
                  "cache-max-mib", 2048u, "cache-max-image-mib", 256u,
                  "preload-workers", 5u, "limit-quality", FALSE, NULL);
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size (GTK_WINDOW (window), 400, 300);
    GtkWidget *scroll = gtk_scrolled_window_new (NULL, NULL);
    viewer = RSTTO_IMAGE_VIEWER (rstto_image_viewer_new ());
    gtk_container_add (GTK_CONTAINER (scroll), GTK_WIDGET (viewer));
    gtk_container_add (GTK_CONTAINER (window), scroll);
    gtk_widget_show_all (window);
    cache = g_object_ref (rstto_image_viewer_get_cache (viewer));
    if (mode == 2)
        rstto_image_viewer_test_fail_gpu (viewer);
    wait_frame ();
    gchar *directory = g_dir_make_tmp ("ristretto-delivery-test-XXXXXX", NULL);
    RsttoFile *files[2];
    for (guint n = 0; n < G_N_ELEMENTS (files); n++)
    {
        gchar *path = g_strdup_printf ("%s/%u.png", directory, n);
        GdkPixbuf *pixels = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 1024, 768);
        gdk_pixbuf_fill (pixels, 0x20a050ff + n * 0x10000000);
        g_assert_true (gdk_pixbuf_save (pixels, path, "png", NULL, NULL));
        GFile *file = g_file_new_for_path (path);
        files[n] = rstto_file_new (file);
        g_object_unref (file);
        g_object_unref (pixels);
        g_free (path);
    }
    RejectedDelivery state = { .derivative = mode == 2 };
    g_signal_connect (cache, "ready", G_CALLBACK (delivery_observed), &state);
    rstto_image_viewer_set_file (viewer, files[0], mode == 2 ? 0.5 : 1.0,
                                 RSTTO_SCALE_NONE, RSTTO_IMAGE_ORIENT_NONE);
    wait_loaded ();
    wait_frame ();
    wait_loaded ();
    g_assert_cmpuint (state.source_bytes, >, 0);
    /* Initial scrollbar allocation may already have replaced a derivative.
     * Observe only the settled display that the upcoming delivery will skip. */
    static cairo_user_data_key_t source_key, derivative_key;
    g_assert_cmpint (cairo_surface_set_user_data (state.source, &source_key,
                                                  &state.source_destroyed, delivery_surface_destroyed),
                     ==, CAIRO_STATUS_SUCCESS);
    if (state.derivative)
    {
        g_assert_nonnull (state.rendered);
        g_assert_cmpint (cairo_surface_set_user_data (state.rendered, &derivative_key,
                                                      &state.derivative_destroyed, delivery_surface_destroyed),
                         ==, CAIRO_STATUS_SUCCESS);
    }
    guint64 decodes;
    RsttoCacheStats stats;
    rstto_image_cache_get_stats (cache, &stats);
    decodes = stats.decodes;
    scrolling_hold (mode == 2 ? 10 : 4);
    if (mode == 2)
        rstto_image_viewer_set_scale (viewer, 0.6);
    else
    {
        if (mode == 1)
        {
            gchar *replacement = g_build_filename (directory, "replacement.png", NULL);
            GdkPixbuf *pixels = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 800, 600);
            gdk_pixbuf_fill (pixels, 0x123456ff);
            g_assert_true (gdk_pixbuf_save (pixels, replacement, "png", NULL, NULL));
            g_assert_cmpint (g_rename (replacement, rstto_file_get_path (files[0])), ==, 0);
            g_object_unref (pixels);
            g_free (replacement);
        }
        /* Refresh and file-monitor updates reach this same invalidation signal. */
        rstto_file_invalidate_metadata (files[0]);
        g_signal_emit_by_name (files[0], "changed");
    }
    scrolling_await_worker ();
    rstto_image_viewer_set_file (viewer, files[1], 1.0, RSTTO_SCALE_NONE, RSTTO_IMAGE_ORIENT_NONE);
    state.waiting = TRUE;
    g_mutex_lock (&scrolling_mutex);
    scrolling_gate = FALSE;
    g_cond_broadcast (&scrolling_cond);
    g_mutex_unlock (&scrolling_mutex);
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (!state.rejected)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    scrolling_await_worker ();
    g_assert_cmpuint (state.source_destroyed, ==, 0);
    g_assert_cmpuint (state.derivative_destroyed, ==, 0);
    g_assert_true (rstto_image_viewer_get_file (viewer) == files[0]);
    rstto_image_cache_get_stats (cache, &stats);
    g_assert_cmpuint (stats.resident_bytes, >=, state.source_bytes);
    if (mode == 2)
        g_assert_cmpuint (stats.decodes, ==, decodes + 1); /* Only B's admitted decode. */
    scrolling_check_limits ();
    wait_frame ();
    GdkPixbuf *print = rstto_image_viewer_get_pixbuf (viewer);
    g_assert_nonnull (print);
    g_assert_cmpint (gdk_pixbuf_get_width (print), ==, 1024);
    const guchar *pixel = gdk_pixbuf_get_pixels (print);
    g_assert_cmphex (pixel[0], ==, 0x20);
    g_assert_cmphex (pixel[1], ==, 0xa0);
    g_assert_cmphex (pixel[2], ==, 0x50);
    g_object_unref (print);
    if (mode == 4)
    {
        RsttoCacheLimits limits = { TRUE, 1, 0, 0 };
        rstto_image_cache_set_limits (cache, &limits);
        rstto_image_cache_get_stats (cache, &stats);
        g_assert_cmpuint (state.source_destroyed, ==, 0);
        g_assert_cmpuint (stats.resident_bytes, >=, state.source_bytes);
    }
    if (mode == 3)
    {
        gtk_widget_destroy (window);
        viewer = NULL;
    }
    scrolling_release ();
    if (mode != 3)
    {
        g_assert_true (rstto_image_viewer_get_file (viewer) == files[1]);
        gtk_widget_destroy (window);
        viewer = NULL;
    }
    wait_loaded ();
    g_assert_cmpuint (state.source_destroyed, ==, 1);
    if (mode == 2)
        g_assert_cmpuint (state.derivative_destroyed, ==, 1);
    rstto_image_cache_get_stats (cache, &stats);
    g_assert_cmpuint (stats.resident_bytes + stats.reserved_bytes, ==, 0);
    g_assert_cmpuint (stats.entries + stats.reserved_images, ==, 0);
    g_object_unref (cache);
    cache = NULL;
    for (guint n = 0; n < G_N_ELEMENTS (files); n++)
    {
        g_file_delete (rstto_file_get_file (files[n]), NULL, NULL);
        g_object_unref (files[n]);
    }
    g_rmdir (directory);
    g_free (directory);
    g_object_unref (settings);
}


/**
 * main:
 *
 * Register viewer regressions and optional reproducible presentation benchmark
 * scenarios.
 */
int
main (int argc, char **argv)
{
    gchar *config = g_dir_make_tmp ("ristretto-test-config-XXXXXX", NULL);
    g_setenv ("XDG_CONFIG_HOME", config, TRUE);
    g_setenv ("GSETTINGS_BACKEND", "memory", TRUE);
    const gchar *scenario = argc == 2 && g_str_has_prefix (argv[1], "--benchmark-scenario=")
                                ? argv[1] + strlen ("--benchmark-scenario=")
                                : NULL;
    benchmark_zoom = argc == 2 && g_str_equal (argv[1], "--benchmark-zoom");
    benchmark_scrolling = argc == 2 && g_str_equal (argv[1], "--benchmark-scrolling");
    gboolean benchmark = argc == 2 && (scenario != NULL || benchmark_zoom || benchmark_scrolling || g_str_equal (argv[1], "--benchmark-navigation=good") || g_str_equal (argv[1], "--benchmark-navigation=nearest"));
    if (benchmark)
    {
        benchmark_cold = g_getenv ("RSTTO_BENCH_COLD") != NULL;
        benchmark_smoothing = benchmark_zoom || benchmark_scrolling || g_str_equal (argv[1], "--benchmark-navigation=good");
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
    if (scenario != NULL)
    {
        g_assert_true (g_str_equal (scenario, "cold") || g_str_equal (scenario, "warm")
                       || g_str_equal (scenario, "scroll") || g_str_equal (scenario, "zoom")
                       || g_str_equal (scenario, "zoomstream") || g_str_equal (scenario, "drag"));
        g_test_add_data_func ("/comparison/scenario", scenario, benchmark_scenario);
    }
    else if (benchmark)
        g_test_add_func ("/viewer/profile-navigation", benchmark_navigation);
    else
    {
        const gchar *delivery_cases[] = { "refresh", "replacement", "derivative", "destroy", "limit-reduction" };
        for (guint n = 0; n < G_N_ELEMENTS (delivery_cases); n++)
        {
            gchar *name = g_strdup_printf ("/viewer/rejected-delivery/%s", delivery_cases[n]);
            g_test_add_data_func (name, GUINT_TO_POINTER (n), rejected_delivery);
            g_free (name);
        }
        g_test_add_func ("/viewer/navigation-and-lifetime", test_viewer);
        g_test_add_func ("/viewer/zoom-latest-view", zoom_latest_view);
        g_test_add_data_func ("/viewer/drag-render", GUINT_TO_POINTER (9), drag_continuous);
        g_test_add_data_func ("/viewer/drag-verification", GUINT_TO_POINTER (10), drag_continuous);
        g_test_add_data_func ("/viewer/drag-cpu-fallback", NULL, drag_continuous);
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

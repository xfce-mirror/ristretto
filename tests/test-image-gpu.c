/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/image_gpu.h"
#include <gtk/gtk.h>
#include <math.h>
#include <string.h>
#include <libplacebo/filters.h>

typedef struct { RsttoGpuImage *image; GError *error; gboolean done; } Result;
static gboolean display;
static GtkWidget *window;
static RsttoGpuRenderer *gpu;

static void
done (GObject *object, GAsyncResult *result, gpointer data)
{
    Result *r = data;
    r->image = rstto_gpu_render_finish (RSTTO_GPU_RENDERER (object), result, &r->error);
    r->done = TRUE;
}

static void
wait_result (Result *result)
{
    gint64 deadline = g_get_monotonic_time () + 20 * G_USEC_PER_SEC;
    while (!result->done)
    {
        g_main_context_iteration (NULL, FALSE);
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_usleep (1000);
    }
}

static void
warm_done (GObject *object, GAsyncResult *result, gpointer data)
{
    Result *r = data;
    g_assert_true (rstto_gpu_warmup_finish (RSTTO_GPU_RENDERER (object), result, &r->error));
    r->done = TRUE;
}

static gboolean
start (guint workers)
{
    if (!display) { g_test_skip ("No GTK display"); return FALSE; }
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_widget_realize (window);
    GError *error = NULL;
    gpu = rstto_gpu_renderer_new (gtk_widget_get_window (window), workers, &error);
    if (gpu == NULL)
    {
        g_test_skip (error->message);
        g_clear_error (&error);
        gtk_widget_destroy (window);
        return FALSE;
    }
    while (rstto_gpu_renderer_needs_warmup (gpu))
    {
        Result r = { 0 };
        rstto_gpu_warmup_async (gpu, TRUE, NULL, warm_done, &r);
        wait_result (&r);
        g_assert_no_error (r.error);
    }
    return TRUE;
}

static void
stop (void)
{
    RsttoGpuStats stats;
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    do {
        g_main_context_iteration (NULL, FALSE);
        rstto_gpu_renderer_get_stats (gpu, &stats);
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_usleep (1000);
    } while (stats.textures || stats.active || stats.transient);
    /* Run deferred owner releases before destroying the GdkWindow. */
    while (g_main_context_iteration (NULL, FALSE)) { }
    g_object_unref (gpu);
    gtk_widget_destroy (window);
}

static cairo_surface_t *
fixture (gboolean alpha)
{
    cairo_surface_t *surface = cairo_image_surface_create (alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24, 47, 31);
    guint32 *pixels = (guint32 *) cairo_image_surface_get_data (surface);
    for (guint y = 0; y < 31; y++)
        for (guint x = 0; x < 47; x++)
        {
            guint a = alpha ? (x * 19 + y * 13) % 256 : 255;
            guint r = x * 251 / 47 * a / 255, g = y * 239 / 31 * a / 255, b = (x + y) * 193 / 78 * a / 255;
            pixels[y * 47 + x] = a << 24 | r << 16 | g << 8 | b;
        }
    cairo_surface_mark_dirty (surface);
    return surface;
}

static RsttoRenderKey
key_for (guint orientation, double zoom, gboolean smooth, double monitor)
{
    RsttoRenderView view = { .viewport_width = 59, .viewport_height = 53, .orientation = orientation,
        .image_scale = 1, .pixel_scale = monitor, .scale = zoom, .previous_scale = 1,
        .default_zoom = -1, .h_adjust = 3, .v_adjust = 5, .smoothing = smooth };
    RsttoRenderGeometry geometry;
    RsttoRenderKey key;
    g_assert_true (rstto_image_render_geometry (&view, 47, 31, &geometry));
    g_assert_true (rstto_image_render_key (&view, &geometry, 47, 47, &key));
    rstto_gpu_render_key (gpu, 47, 31, &key);
    return key;
}

static cairo_surface_t *
render (cairo_surface_t *source, RsttoRenderKey *key)
{
    RsttoRenderPixels pixels;
    rstto_image_render_pixels (source, &pixels);
    Result result = { 0 };
    GCancellable *cancel = g_cancellable_new ();
    rstto_gpu_render_async (gpu, &pixels, key, cancel, NULL, NULL, NULL, done, &result);
    wait_result (&result);
    g_assert_no_error (result.error);
    g_assert_nonnull (result.image);
    cairo_surface_t *surface = rstto_gpu_test_download (result.image);
    rstto_gpu_image_unref (result.image);
    g_object_unref (cancel);
    return surface;
}

static void
limits (void)
{
    RsttoRenderKey key = { .width = 64, .height = 64, .x_scale = 1, .y_scale = 1, .pixel_scale = 1, .source_scale = 1 };
    g_assert_true (rstto_gpu_dimensions_fit (64, 63, 63, &key));
    g_assert_true (rstto_gpu_dimensions_fit (64, 64, 64, &key));
    g_assert_false (rstto_gpu_dimensions_fit (64, 65, 64, &key));
    g_assert_false (rstto_gpu_dimensions_fit (64, 64, 65, &key));
    key.width = 65; g_assert_false (rstto_gpu_dimensions_fit (64, 10, 10, &key));
    key.width = 64; key.height = 65; g_assert_false (rstto_gpu_dimensions_fit (64, 10, 10, &key));
    key.height = 64; g_assert_false (rstto_gpu_dimensions_fit (0, 10, 10, &key));
    g_assert_false (rstto_gpu_dimensions_fit (64, -1, 10, &key));
    key.width = G_MAXINT; key.height = G_MAXINT;
    g_assert_false (rstto_gpu_dimensions_fit (G_MAXUINT, G_MAXINT, G_MAXINT, &key));
    key.width = key.height = 64; key.x_scale = INFINITY;
    g_assert_false (rstto_gpu_dimensions_fit (64, 10, 10, &key));
}

static void
filters (void)
{
    RsttoRenderKey key = { .x_scale = 1, .y_scale = 1, .pixel_scale = 1, .source_scale = 1 };
    g_assert_cmpint (rstto_gpu_filter (&key), ==, RSTTO_GPU_NEAREST);
    key.smoothing = TRUE;
    g_assert_cmpint (rstto_gpu_filter (&key), ==, RSTTO_GPU_DIRECT);
    key.x_scale = key.y_scale = .5;
    g_assert_cmpint (rstto_gpu_filter (&key), ==, RSTTO_GPU_DOWN);
    key.pixel_scale = 2;
    g_assert_cmpint (rstto_gpu_filter (&key), ==, RSTTO_GPU_DIRECT);
    key.source_scale = .5;
    g_assert_cmpint (rstto_gpu_filter (&key), ==, RSTTO_GPU_UP);
    key.x_scale = key.y_scale = 1.001; key.pixel_scale = key.source_scale = 1;
    g_assert_cmpint (rstto_gpu_filter (&key), ==, RSTTO_GPU_UP);
    key.x_scale = key.y_scale = .999;
    g_assert_cmpint (rstto_gpu_filter (&key), ==, RSTTO_GPU_DOWN);
}

static void
nearest (void)
{
    if (!start (2)) return;
    for (guint alpha = 0; alpha < 2; alpha++)
    {
        cairo_surface_t *source = fixture (alpha);
        for (guint orientation = 1; orientation <= 8; orientation++)
        {
            RsttoRenderKey key = key_for (orientation, 1, FALSE, 1);
            cairo_surface_t *actual = render (source, &key);
            cairo_surface_t *expected = rstto_image_render (source, &key, NULL);
            g_assert_cmpmem (cairo_image_surface_get_data (actual), key.width * key.height * 4,
                             cairo_image_surface_get_data (expected), key.width * key.height * 4);
            cairo_surface_destroy (actual); cairo_surface_destroy (expected);
        }
        cairo_surface_destroy (source);
    }
    stop ();
}

static void
smooth (void)
{
    if (!start (2)) return;
    for (guint alpha = 0; alpha < 2; alpha++)
    {
        cairo_surface_t *source = fixture (alpha);
        for (guint orientation = 1; orientation <= 8; orientation++)
            for (guint scale = 0; scale < 3; scale++)
            {
                RsttoRenderKey key = key_for (orientation, scale == 0 ? .5 : scale == 1 ? 1.5 : 2.25, TRUE, scale == 2 ? 2 : 1);
                cairo_surface_t *actual = render (source, &key);
                guint32 *pixels = (guint32 *) cairo_image_surface_get_data (actual);
                guint nonzero = 0;
                for (gint n = 0; n < key.width * key.height; n++)
                {
                    guint a = pixels[n] >> 24;
                    nonzero += a != 0;
                    g_assert_cmpuint ((pixels[n] >> 16) & 255, <=, a);
                    g_assert_cmpuint ((pixels[n] >> 8) & 255, <=, a);
                    g_assert_cmpuint (pixels[n] & 255, <=, a);
                }
                g_assert_cmpuint (nonzero, >, 100);
                cairo_surface_destroy (actual);
            }
        cairo_surface_destroy (source);
    }
    stop ();
}

static void
reference_filter (void)
{
    if (!start (1)) return;
    /* Independent double-precision convolution using the host filter function,
     * rather than libplacebo's generated GLSL/LUT. Smooth ramps avoid the
     * antiringing clamp so discrepancies expose mapping/filter errors. */
    cairo_surface_t *source = fixture (FALSE);
    guint32 *input = (guint32 *) cairo_image_surface_get_data (source);
    const double scales[] = { .5, 1.5 };
    for (guint n = 0; n < G_N_ELEMENTS (scales); n++)
    {
        double scale = scales[n];
        RsttoRenderKey key = { .width = n == 0 ? 23 : 70, .height = n == 0 ? 15 : 46,
            .source_scale = 1, .pixel_scale = 1, .x_scale = scale, .y_scale = scale,
            .render_width = 47 * scale, .render_height = 31 * scale, .orientation = 1, .smoothing = TRUE };
        rstto_gpu_render_key (gpu, 47, 31, &key);
        cairo_surface_t *actual = render (source, &key);
        guint32 *output = (guint32 *) cairo_image_surface_get_data (actual);
        struct pl_filter_config config = n == 0 ? pl_filter_ewa_lanczos : pl_filter_ewa_lanczossharp;
        pl_filter filter = pl_filter_generate (NULL, &(struct pl_filter_params) {
            .config = config, .lut_entries = 256, .cutoff = 1e-3 });
        double widening = MAX (1, 1 / scale), radius = filter->radius * widening;
        for (gint y = 5; y < key.height - 5; y += 3)
            for (gint x = 5; x < key.width - 5; x += 3)
            {
                double sx = (x + .5) / scale - .5, sy = (y + .5) / scale - .5;
                double sum[3] = { 0 }, weights = 0;
                for (gint iy = floor (sy - radius); iy <= ceil (sy + radius); iy++)
                    for (gint ix = floor (sx - radius); ix <= ceil (sx + radius); ix++)
                    {
                        double distance = hypot (ix - sx, iy - sy) / widening;
                        if (distance >= filter->radius) continue;
                        double weight = pl_filter_sample (&config, distance);
                        guint32 pixel = input[CLAMP (iy, 0, 30) * 47 + CLAMP (ix, 0, 46)];
                        weights += weight;
                        for (guint c = 0; c < 3; c++) sum[c] += weight * ((pixel >> (c * 8)) & 255);
                    }
                for (guint c = 0; c < 3; c++)
                    g_assert_cmpfloat (fabs (((output[y * key.width + x] >> (c * 8)) & 255) - sum[c] / weights), <=, 2);
            }
        pl_filter_free (&filter);
        cairo_surface_destroy (actual);
    }
    cairo_surface_destroy (source);
    stop ();
}

static void
runtime_variants (void)
{
    if (!start (1)) return;
    RsttoGpuStats before, after;
    rstto_gpu_renderer_get_stats (gpu, &before);
    g_assert_cmpuint (before.warmups, ==, 1);
    g_assert_cmpuint (before.programs, ==, 3);
    g_assert_cmpuint (before.transient, ==, 0);
    g_assert_cmpuint (before.uploads, ==, 0);
    const double zooms[] = { .125, .1635, .1978, .2176, .2633, .3855, .5, .8, 1, 1.1, 1.5, 2.0 };
    for (guint alpha = 0; alpha < 2; alpha++)
    {
        cairo_surface_t *source = fixture (alpha);
        for (guint i = 0; i < G_N_ELEMENTS (zooms); i++)
        {
            RsttoRenderKey key = key_for (1 + i % 8, zooms[i], i != 8, 1 + i % 2);
            cairo_surface_t *output = render (source, &key);
            cairo_surface_destroy (output);
            rstto_gpu_renderer_get_stats (gpu, &after);
            g_test_message ("alpha=%u index=%u scale=%.4f programs=%" G_GUINT64_FORMAT, alpha, i, zooms[i], after.programs);
        }
        cairo_surface_destroy (source);
    }
    rstto_gpu_renderer_get_stats (gpu, &after);
    g_assert_cmpuint (after.programs, ==, before.programs);
    stop ();
}

static void
installed_reference (void)
{
    if (!start (1)) return;
    const double zooms[] = { .15, .2176, .5, .83, 1.1, 1.5, .99, 2.0 };
    for (guint alpha = 0; alpha < 3; alpha++)
    {
        cairo_surface_t *source = fixture (alpha != 0);
        if (alpha == 2)
        {
            guint32 *pixels = (guint32 *) cairo_image_surface_get_data (source);
            for (guint y = 0; y < 31; y++)
                for (guint x = 0; x < 47; x++)
                    pixels[y * 47 + x] = (x + y) % 3 == 0 ? 0 : (x + y) % 3 == 1 ? 0x80008040 : 0xfff00201;
            cairo_surface_mark_dirty (source);
        }
        for (guint i = 0; i < G_N_ELEMENTS (zooms); i++)
        {
            RsttoRenderKey key = key_for (1 + i % 8, zooms[i], TRUE, 1);
            cairo_surface_t *actual = render (source, &key);
            rstto_gpu_test_reference (gpu, TRUE);
            cairo_surface_t *reference = render (source, &key);
            rstto_gpu_test_reference (gpu, FALSE);
            const guint8 *a = cairo_image_surface_get_data (actual), *b = cairo_image_surface_get_data (reference);
            guint maximum = 0;
            for (gint n = 0; n < cairo_image_surface_get_stride (actual) * key.height; n++)
                maximum = MAX (maximum, (guint) ABS ((gint) a[n] - (gint) b[n]));
            g_test_message ("alpha=%u zoom=%.4f maximum channel difference=%u", alpha, zooms[i], maximum);
            g_assert_cmpuint (maximum, <=, 2);
            cairo_surface_destroy (actual); cairo_surface_destroy (reference);
        }
        cairo_surface_destroy (source);
    }
    stop ();
}

static void
quality_scale (void)
{
    if (!start (1)) return;
    cairo_surface_t *source = fixture (TRUE);
    for (guint orientation = 1; orientation <= 8; orientation++)
    {
        RsttoRenderView view = { .viewport_width = 43, .viewport_height = 37, .orientation = orientation,
            .image_scale = 2, .pixel_scale = 2, .scale = 1, .previous_scale = 1, .h_adjust = 3, .v_adjust = 2 };
        RsttoRenderGeometry geometry;
        RsttoRenderKey key;
        g_assert_true (rstto_image_render_geometry (&view, 94, 62, &geometry));
        g_assert_true (rstto_image_render_key (&view, &geometry, 94, 47, &key));
        rstto_gpu_render_key (gpu, 47, 31, &key);
        cairo_surface_t *actual = render (source, &key);
        cairo_surface_t *expected = rstto_image_render (source, &key, NULL);
        g_assert_cmpmem (cairo_image_surface_get_data (actual), key.width * key.height * 4,
                         cairo_image_surface_get_data (expected), key.width * key.height * 4);
        cairo_surface_destroy (actual); cairo_surface_destroy (expected);
    }
    cairo_surface_destroy (source);
    stop ();
}

static void
destroy_running (void)
{
    if (!start (1)) return;
    cairo_surface_t *source = fixture (TRUE);
    RsttoRenderPixels pixels; rstto_image_render_pixels (source, &pixels);
    RsttoRenderKey key = key_for (1, .5, TRUE, 1);
    Result result = { 0 };
    GCancellable *cancel = g_cancellable_new ();
    rstto_gpu_test_hold (gpu, TRUE);
    rstto_gpu_render_async (gpu, &pixels, &key, cancel, NULL, NULL, NULL, done, &result);
    RsttoGpuStats stats;
    gint64 deadline = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    do {
        g_main_context_iteration (NULL, FALSE);
        rstto_gpu_renderer_get_stats (gpu, &stats);
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_usleep (1000);
    } while (stats.submissions == 0);
    gpointer weak = gpu;
    g_object_add_weak_pointer (G_OBJECT (gpu), &weak);
    gtk_widget_destroy (window);
    g_cancellable_cancel (cancel);
    rstto_gpu_test_hold (gpu, FALSE);
    g_object_unref (gpu);
    wait_result (&result);
    g_assert_null (result.image);
    g_clear_error (&result.error);
    while (weak != NULL)
    {
        g_main_context_iteration (NULL, FALSE);
        g_assert_cmpint (g_get_monotonic_time (), <, deadline);
        g_usleep (1000);
    }
    cairo_surface_destroy (source);
    g_object_unref (cancel);
}

static void
parallel_cleanup (void)
{
    if (!start (5)) return;
    cairo_surface_t *source = fixture (TRUE);
    RsttoRenderPixels pixels; rstto_image_render_pixels (source, &pixels);
    Result results[5] = { 0 };
    GCancellable *cancel[5];
    rstto_gpu_test_hold (gpu, TRUE);
    for (guint n = 0; n < 5; n++)
    {
        RsttoRenderKey key = key_for (n + 1, .5, TRUE, 1);
        cancel[n] = g_cancellable_new ();
        rstto_gpu_render_async (gpu, &pixels, &key, cancel[n], NULL, NULL, NULL, done, &results[n]);
    }
    g_assert_false (rstto_gpu_renderer_available (gpu));
    g_cancellable_cancel (cancel[1]);
    g_cancellable_cancel (cancel[3]);
    RsttoGpuStats stats;
    rstto_gpu_renderer_get_stats (gpu, &stats);
    g_assert_cmpuint (stats.active, ==, 5);
    g_assert_cmpuint (stats.peak_active, ==, 5);
    for (guint n = 0; n < 5; n++) g_assert_false (results[n].done);
    rstto_gpu_test_hold (gpu, FALSE);
    for (guint n = 0; n < 5; n++)
    {
        wait_result (&results[n]);
        if (n == 1 || n == 3) g_assert_null (results[n].image);
        else { g_assert_no_error (results[n].error); g_assert_nonnull (results[n].image); }
        rstto_gpu_image_unref (results[n].image);
        g_clear_error (&results[n].error); g_object_unref (cancel[n]);
    }
    cairo_surface_destroy (source);
    stop ();
}

static void
retirement_backpressure (void)
{
    if (!start (1)) return;
    cairo_surface_t *source = fixture (TRUE);
    RsttoRenderPixels pixels; rstto_image_render_pixels (source, &pixels);
    RsttoRenderKey key = key_for (1, .5, TRUE, 1);
    GCancellable *cancel = g_cancellable_new ();
    Result first = { 0 }, next = { 0 };
    rstto_gpu_render_async (gpu, &pixels, &key, cancel, NULL, NULL, NULL, done, &first);
    wait_result (&first);
    g_assert_no_error (first.error);
    rstto_gpu_test_hold (gpu, TRUE);
    rstto_gpu_image_unref (first.image);
    rstto_gpu_render_async (gpu, &pixels, &key, cancel, NULL, NULL, NULL, done, &next);
    gint64 until = g_get_monotonic_time () + 50 * 1000;
    do {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
    } while (g_get_monotonic_time () < until);
    RsttoGpuStats stats;
    rstto_gpu_renderer_get_stats (gpu, &stats);
    g_assert_cmpuint (stats.active, ==, 1);
    g_assert_cmpuint (stats.retiring, ==, 1);
    g_assert_cmpuint (stats.uploads, ==, 1);
    g_assert_cmpuint (stats.transient, ==, 0);
    g_assert_false (next.done);
    rstto_gpu_test_hold (gpu, FALSE);
    wait_result (&next);
    g_assert_no_error (next.error);
    g_assert_nonnull (next.image);
    rstto_gpu_renderer_get_stats (gpu, &stats);
    g_assert_cmpuint (stats.uploads, ==, 2);
    g_assert_cmpuint (stats.retiring, ==, 0);
    rstto_gpu_image_unref (next.image);
    g_object_unref (cancel);
    cairo_surface_destroy (source);
    stop ();
}

int
main (int argc, char **argv)
{
    if (argc == 2 && g_str_has_prefix (argv[1], "--binary-cache="))
    {
        gint64 start_time = g_get_monotonic_time ();
        display = gtk_init_check (NULL, NULL);
        gboolean reject = g_str_equal (argv[1], "--binary-cache=reject");
        gboolean warm = reject || g_str_equal (argv[1], "--binary-cache=warm");
        if (reject) rstto_gpu_test_reject_binary ();
        if (!start (1)) return 77;
        guint compiled, loaded, accepted;
        rstto_gpu_test_programs (&compiled, &loaded, &accepted);
        RsttoGpuStats stats;
        rstto_gpu_renderer_get_stats (gpu, &stats);
        g_print ("BINARY_CACHE compiles=%u loads=%u accepted=%u elapsed_ms=%.3f hits=%" G_GUINT64_FORMAT " misses=%" G_GUINT64_FORMAT " writes=%" G_GUINT64_FORMAT "\n",
            compiled, loaded, accepted, (g_get_monotonic_time () - start_time) / 1000.0, stats.binary_hits, stats.binary_misses, stats.binary_writes);
        if (g_str_equal (argv[1], "--binary-cache=measure")) { stop (); return 0; }
        if (warm) { g_assert_cmpuint (loaded, ==, 3); g_assert_cmpuint (accepted, ==, reject ? 2 : 3); }
        else g_assert_cmpuint (loaded, ==, 1); /* Up/down share the fixed EWA program. */
        g_assert_cmpuint (compiled, ==, warm ? (reject ? 2 : 0) : 4);
        stop ();
        return 0;
    }
    g_test_init (&argc, &argv, NULL);
    display = gtk_init_check (&argc, &argv);
    g_test_add_func ("/gpu/dimensions", limits);
    g_test_add_func ("/gpu/filters", filters);
    g_test_add_func ("/gpu/nearest-orientations-alpha", nearest);
    g_test_add_func ("/gpu/smooth-geometry-alpha", smooth);
    g_test_add_func ("/gpu/reference-filter", reference_filter);
    g_test_add_func ("/gpu/runtime-variants", runtime_variants);
    g_test_add_func ("/gpu/installed-reference", installed_reference);
    g_test_add_func ("/gpu/quality-monitor-scale", quality_scale);
    g_test_add_func ("/gpu/destroy-running", destroy_running);
    g_test_add_func ("/gpu/parallel-delayed-cancellation", parallel_cleanup);
    g_test_add_func ("/gpu/retirement-backpressure", retirement_backpressure);
    return g_test_run ();
}

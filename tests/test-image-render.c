/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/image_render.h"

#include <float.h>
#include <math.h>


/**
 * reference:
 *
 * Render with the viewer's original transform and device-scaled source as an
 * independent reference for asynchronous CPU preparation.
 */
static cairo_surface_t *
reference (cairo_surface_t *source, const RsttoRenderView *v, const RsttoRenderGeometry *g, gdouble quality)
{
    cairo_surface_t *output = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                          ceil (v->viewport_width * v->pixel_scale), ceil (v->viewport_height * v->pixel_scale));
    cairo_t *cr = cairo_create (output);
    cairo_scale (cr, v->pixel_scale, v->pixel_scale);
    gint w = g->width, h = g->height, x = g->x, y = g->y, ha = g->h_adjust, va = g->v_adjust;
    cairo_matrix_t flip;
    switch (v->orientation)
    {
        case 2:
            cairo_translate (cr, -ha, -va);
            cairo_translate (cr, w, 0);
            cairo_translate (cr, x, y);
            cairo_matrix_init_identity (&flip);
            flip.xx = -1;
            cairo_transform (cr, &flip);
            break;
        case 4:
            cairo_translate (cr, -ha, -va);
            cairo_translate (cr, 0, h);
            cairo_translate (cr, x, y);
            cairo_matrix_init_identity (&flip);
            flip.yy = -1;
            cairo_transform (cr, &flip);
            break;
        case 5:
            cairo_rotate (cr, G_PI * 1.5);
            cairo_translate (cr, va, -ha);
            cairo_translate (cr, -y, x);
            cairo_matrix_init_identity (&flip);
            flip.xx = -1;
            cairo_transform (cr, &flip);
            break;
        case 7:
            cairo_rotate (cr, G_PI * 0.5);
            cairo_translate (cr, -va, ha);
            cairo_translate (cr, h, -w);
            cairo_translate (cr, y, -x);
            cairo_matrix_init_identity (&flip);
            flip.xx = -1;
            cairo_transform (cr, &flip);
            break;
        case 6:
            cairo_rotate (cr, G_PI * 0.5);
            cairo_translate (cr, -va, ha);
            cairo_translate (cr, 0, -w);
            cairo_translate (cr, y, -x);
            break;
        case 8:
            cairo_rotate (cr, G_PI * 1.5);
            cairo_translate (cr, va, -ha);
            cairo_translate (cr, -h, 0);
            cairo_translate (cr, -y, x);
            break;
        case 3:
            cairo_rotate (cr, G_PI);
            cairo_translate (cr, ha, va);
            cairo_translate (cr, -w, -h);
            cairo_translate (cr, -x, -y);
            break;
        default:
            cairo_translate (cr, -ha, -va);
            cairo_translate (cr, x, y);
            break;
    }
    gboolean swap = v->orientation >= 5;
    gdouble xs = (1 - DBL_EPSILON) * w / (swap ? g->image_height : g->image_width);
    gdouble ys = (1 - DBL_EPSILON) * h / (swap ? g->image_width : g->image_height);
    cairo_surface_t *wrapper = cairo_image_surface_create_for_data (cairo_image_surface_get_data (source),
                                                                    CAIRO_FORMAT_ARGB32, 256, 192, cairo_image_surface_get_stride (source));
    cairo_surface_set_device_scale (wrapper, v->image_scale, v->image_scale);
    cairo_scale (cr, xs / quality, ys / quality);
    cairo_set_source_surface (cr, wrapper, 0, 0);
    cairo_pattern_set_filter (cairo_get_source (cr), v->smoothing ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
    cairo_paint (cr);
    cairo_destroy (cr);
    cairo_surface_destroy (wrapper);
    return output;
}


/**
 * render_pixels:
 *
 * Compare prepared and striped pixels against the reference across geometry,
 * orientation and transparency cases.
 */
static void
render_pixels (void)
{
    cairo_surface_t *source = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 256, 192);
    for (gint y = 0; y < 192; y++)
        for (gint x = 0; x < 256; x++)
        {
            guint a = (x + y) % 256;
            guint32 *row = (guint32 *) (cairo_image_surface_get_data (source) + y * cairo_image_surface_get_stride (source));
            row[x] = a << 24 | ((x * a / 255) << 16) | ((y * a / 255) << 8) | (a / 2);
        }
    cairo_surface_mark_dirty (source);
    cairo_surface_flush (source);
    GCancellable *cancel = g_cancellable_new ();
    for (gint o = 1; o <= 8; o++)
        for (gint smoothing = 0; smoothing < 2; smoothing++)
            for (gint device = 1; device <= 2; device++)
                for (gint crop = 0; crop < 2; crop++)
                    for (gint quality_divisor = 1; quality_divisor <= 2; quality_divisor++)
                    {
                        RsttoRenderView v = { .viewport_width = 263, .viewport_height = 347, .orientation = o, .image_scale = device == 2 ? 1.5 : 1, .pixel_scale = device, .scale = crop ? 0.6 : 0, .h_adjust = crop ? 13 : 0, .v_adjust = crop ? 17 : 0, .smoothing = smoothing };
                        RsttoRenderGeometry g;
                        RsttoRenderKey key;
                        g_assert_true (rstto_image_render_geometry (&v, 256 * quality_divisor, 192 * quality_divisor, &g));
                        g_assert_true (rstto_image_render_key (&v, &g, 256 * quality_divisor, 256, &key));
                        cairo_surface_t *actual = rstto_image_render (source, &key, cancel);
                        cairo_surface_t *expected = reference (source, &v, &g, 1.0 / quality_divisor);
                        g_assert_nonnull (actual);
                        cairo_surface_flush (actual);
                        cairo_surface_flush (expected);
                        gsize bytes = (gsize) cairo_image_surface_get_stride (actual) * key.height;
                        guint8 *a = cairo_image_surface_get_data (actual), *b = cairo_image_surface_get_data (expected);
                        for (gsize i = 0; i < bytes; i++)
                        {
                            if (ABS ((gint) a[i] - b[i]) > (smoothing ? 2 : 0))
                                g_test_message ("orientation=%d smoothing=%d device=%d crop=%d quality=%d offset=%zu a=%u b=%u", o, smoothing, device, crop, quality_divisor, i, a[i], b[i]);
                            g_assert_cmpint (ABS ((gint) a[i] - b[i]), <=, smoothing ? 2 : 0);
                        }
                        cairo_surface_destroy (actual);
                        cairo_surface_destroy (expected);
                    }
    gdouble sx, sy;
    cairo_surface_get_device_scale (source, &sx, &sy);
    g_assert_cmpfloat (sx, ==, 1);
    g_assert_cmpfloat (sy, ==, 1);
    g_object_unref (cancel);
    cairo_surface_destroy (source);
}


/**
 * render_bounds:
 *
 * Reject invalid or overflowing output dimensions before allocating a render
 * surface.
 */
static void
render_bounds (void)
{
    guint64 bytes;
    RsttoRenderKey key = { .width = 64, .height = 48, .pixel_scale = 1 };
    g_assert_true (rstto_image_render_weight (&key, &bytes));
    g_assert_cmpuint (bytes, ==, 64 * 48 * 4);
    key.width = G_MAXINT;
    g_assert_false (rstto_image_render_weight (&key, &bytes));
    key.width = 0;
    g_assert_false (rstto_image_render_weight (&key, &bytes));
    RsttoRenderView view = { .viewport_width = 64, .viewport_height = 48, .image_scale = 1, .pixel_scale = 1, .scale = 0 };
    RsttoRenderGeometry geometry;
    g_assert_false (rstto_image_render_geometry (&view, 0, 32, &geometry));
    view.image_scale = NAN;
    g_assert_false (rstto_image_render_geometry (&view, 32, 32, &geometry));
    GCancellable *cancel = g_cancellable_new ();
    g_cancellable_cancel (cancel);
    g_assert_null (rstto_image_render (NULL, &key, cancel));
    g_object_unref (cancel);
}


/**
 * convert_continue:
 *
 * Stop conversion after a controlled number of row-block cancellation checks.
 */
static gboolean
convert_continue (gpointer data)
{
    guint *remaining = data;
    return (*remaining)-- != 0;
}


/**
 * convert_pixels:
 *
 * Compare optimized RGB/RGBA conversion against exact scalar premultiplication
 * across widths, strides and alpha boundaries.
 */
static void
convert_pixels (void)
{
    for (guint mode = 0; mode < 4; mode++)
        for (gint width = 1; width <= 259; width++)
        {
            gboolean alpha = mode != 0;
            gint channels = alpha ? 4 : 3, stride = width * channels + 19, height = 131;
            guchar *pixels = g_malloc (stride * height);
            for (gint n = 0; n < stride * height; n++)
                pixels[n] = (n * 179 + n / 257) % 256;
            if (mode >= 2)
                for (gint y = 0; y < height; y++)
                    for (gint x = 0; x < width; x++)
                        pixels[y * stride + x * 4 + 3] = mode == 2 ? 255 : 0;
            GdkPixbuf *pixbuf = gdk_pixbuf_new_from_data (pixels, GDK_COLORSPACE_RGB, alpha, 8,
                                                          width, height, stride, NULL, NULL);
            cairo_surface_t *surface = rstto_image_convert (pixbuf, NULL, NULL);
            g_assert_cmpint (cairo_surface_status (surface), ==, CAIRO_STATUS_SUCCESS);
            for (gint y = 0; y < height; y++)
            {
                const guchar *src = pixels + y * stride;
                const guint32 *dst = (const guint32 *) (cairo_image_surface_get_data (surface)
                                                        + y * cairo_image_surface_get_stride (surface));
                for (gint x = 0; x < width; x++, src += channels)
                {
                    guint a = alpha ? src[3] : 255;
                    guint32 expected = (a << 24) | (((src[0] * a + 127) / 255) << 16)
                                       | (((src[1] * a + 127) / 255) << 8) | ((src[2] * a + 127) / 255);
                    g_assert_cmphex (dst[x], ==, expected);
                }
            }
            guint remaining = 1;
            g_assert_null (rstto_image_convert (pixbuf, convert_continue, &remaining));
            cairo_surface_destroy (surface);
            g_object_unref (pixbuf);
            g_free (pixels);
        }
}


/**
 * convert_immutable:
 *
 * Verify that converting an immutable GBytes-backed pixbuf does not request a
 * writable full-image copy.
 */
static void
convert_immutable (void)
{
#if GDK_PIXBUF_CHECK_VERSION(2, 32, 0)
    guchar *pixels = g_malloc0 (24 * 128 + 21);
    GBytes *bytes = g_bytes_new_take (pixels, 24 * 128 + 21);
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_bytes (bytes, GDK_COLORSPACE_RGB, FALSE, 8, 7, 129, 24);
    g_assert_true (gdk_pixbuf_read_pixels (pixbuf) == pixels);
    cairo_surface_t *surface = rstto_image_convert (pixbuf, NULL, NULL);
    g_assert_nonnull (surface);
    g_assert_cmpint (cairo_surface_status (surface), ==, CAIRO_STATUS_SUCCESS);
    g_assert_true (gdk_pixbuf_read_pixels (pixbuf) == pixels); /* no writable full-image copy */
    cairo_surface_destroy (surface);
    g_object_unref (pixbuf);
    g_bytes_unref (bytes);
#else
    g_test_skip ("Read-only pixbuf access requires GdkPixbuf 2.32");
#endif
}


/**
 * render_compatibility:
 *
 * Require exact cache identity while permitting only pan differences in the
 * separate presentation classification.
 */
static void
render_compatibility (void)
{
    RsttoRenderKey a = { .width = 640, .height = 480, .pixel_scale = 2, .source_scale = 1, .orientation = 6, .render_width = 1024, .render_height = 768, .x = 0, .y = 0, .h_adjust = 30, .v_adjust = 20, .x_scale = 2, .y_scale = 2, .smoothing = TRUE, .backend = 1, .filter = 2, .context_generation = 3, .view_revision = 4 }, b = a;
    b.view_revision++;
    g_assert_cmpint (rstto_image_render_compatibility (&a, &b), ==, RSTTO_RENDER_EXACT);
    b.h_adjust++;
    b.v_adjust--;
    g_assert_false (rstto_image_render_key_equal (&a, &b));
    g_assert_cmpint (rstto_image_render_compatibility (&a, &b), ==, RSTTO_RENDER_PAN_ONLY);
#define CHECK_FIELD(field) \
    do \
    { \
        b = a; \
        b.h_adjust++; \
        b.field++; \
        g_assert_cmpint (rstto_image_render_compatibility (&a, &b), ==, RSTTO_RENDER_INCOMPATIBLE); \
    } while (0)
    CHECK_FIELD (width);
    CHECK_FIELD (height);
    CHECK_FIELD (pixel_scale);
    CHECK_FIELD (source_scale);
    CHECK_FIELD (orientation);
    CHECK_FIELD (render_width);
    CHECK_FIELD (render_height);
    CHECK_FIELD (x);
    CHECK_FIELD (y);
    CHECK_FIELD (x_scale);
    CHECK_FIELD (y_scale);
    CHECK_FIELD (smoothing);
    CHECK_FIELD (backend);
    CHECK_FIELD (filter);
    CHECK_FIELD (context_generation);
#undef CHECK_FIELD
}


/**
 * main:
 *
 * Register CPU geometry, conversion, stripe and render-compatibility tests.
 */
int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/render/pixels-and-transforms", render_pixels);
    g_test_add_func ("/render/bounds-and-cancellation", render_bounds);
    g_test_add_func ("/render/pixel-conversion", convert_pixels);
    g_test_add_func ("/render/immutable-pixbuf", convert_immutable);
    g_test_add_func ("/render/completion-compatibility", render_compatibility);
    return g_test_run ();
}

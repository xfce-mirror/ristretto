/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "image_render.h"
#include <float.h>
#include <math.h>
#if defined(__SSE2__) && !defined(RSTTO_RENDER_SCALAR)
#include <emmintrin.h>
#endif

/* EXIF orientation values and the viewer's existing zoom modes. */
static gboolean
swapped (gint orientation)
{
    return orientation >= 5 && orientation <= 8;
}

gboolean
rstto_image_render_geometry (const RsttoRenderView *v, gint width, gint height, RsttoRenderGeometry *g)
{
    if (width <= 0 || height <= 0 || v->viewport_width <= 0 || v->viewport_height <= 0
        || !isfinite (v->image_scale) || v->image_scale <= 0 || !isfinite (v->scale)
        || width / v->image_scale > G_MAXINT || height / v->image_scale > G_MAXINT
        || !isfinite (v->h_adjust) || !isfinite (v->v_adjust))
        return FALSE;
    *g = (RsttoRenderGeometry) { 0 };
    g->image_width = width / v->image_scale;
    g->image_height = height / v->image_scale;
    if (g->image_width <= 0 || g->image_height <= 0)
        return FALSE;
    gint iw = swapped (v->orientation) ? g->image_height : g->image_width;
    gint ih = swapped (v->orientation) ? g->image_width : g->image_height;
    gdouble hs = (gdouble) v->viewport_width / iw, vs = (gdouble) v->viewport_height / ih;
    gdouble scale = v->scale;
    g->auto_scale = -2;
    if (scale == -2)
        scale = v->default_zoom != -2 ? v->default_zoom : (hs > 1 && vs > 1 ? 1 : -1);
    if (scale == -1 || scale == 0)
    {
        g->auto_scale = scale;
        scale = scale == -1 ? MIN (1.0, MIN (hs, vs)) : MIN (hs, vs);
    }
    else if (scale < 1)
    {
        if (scale * MAX (g->image_width, g->image_height) < 0.1 * MIN (v->viewport_width, v->viewport_height))
            scale = v->previous_scale;
    }
    else
        scale = MIN (scale, MAX (1.0, 1.5 * MAX (v->viewport_width, v->viewport_height)
                                              / MIN (g->image_width, g->image_height)));
    if (!isfinite (scale) || scale <= 0 || scale * MAX (iw, ih) > G_MAXINT)
        return FALSE;
    g->scale = scale;
    g->width = iw * scale;
    g->height = ih * scale;
    g->x = MAX (0, (v->viewport_width - iw * scale) / 2.0);
    g->y = MAX (0, (v->viewport_height - ih * scale) / 2.0);
    g->h_adjust = CLAMP (v->h_adjust, 0, MAX (0, g->width - v->viewport_width));
    g->v_adjust = CLAMP (v->v_adjust, 0, MAX (0, g->height - v->viewport_height));
    return g->width > 0 && g->height > 0;
}

gboolean
rstto_image_render_key (const RsttoRenderView *v, const RsttoRenderGeometry *g,
                        gint width, gint decoded_width, RsttoRenderKey *key)
{
    if (width <= 0 || decoded_width <= 0 || g->image_width <= 0 || g->image_height <= 0
        || !isfinite (v->image_scale) || v->image_scale <= 0
        || !isfinite (v->pixel_scale) || v->pixel_scale <= 0
        || v->viewport_width <= 0 || v->viewport_height <= 0
        || v->viewport_width * v->pixel_scale > G_MAXINT || v->viewport_height * v->pixel_scale > G_MAXINT)
        return FALSE;
    *key = (RsttoRenderKey) { .width = ceil (v->viewport_width * v->pixel_scale),
                             .height = ceil (v->viewport_height * v->pixel_scale),
                             .pixel_scale = v->pixel_scale, .source_scale = v->image_scale, .smoothing = v->smoothing };
    key->orientation = v->orientation;
    key->render_width = g->width; key->render_height = g->height;
    key->x = g->x; key->y = g->y;
    key->h_adjust = g->h_adjust; key->v_adjust = g->v_adjust;
    gdouble quality = (gdouble) decoded_width / width;
    key->x_scale = ((1 - DBL_EPSILON) * g->width
                    / (swapped (v->orientation) ? g->image_height : g->image_width)) / quality;
    key->y_scale = ((1 - DBL_EPSILON) * g->height
                    / (swapped (v->orientation) ? g->image_width : g->image_height)) / quality;
    return TRUE;
}

void
rstto_image_render_transform (cairo_t *cr, const RsttoRenderKey *key)
{
    /* Preserve the original sequence of Cairo operations, including its inverse
     * matrix rounding. Collapsing them into cairo_set_matrix() can choose a
     * neighboring source pixel at nearest-neighbor boundaries. */
    gint w = key->render_width, h = key->render_height, x = key->x, y = key->y;
    gint ha = key->h_adjust, va = key->v_adjust;
    cairo_matrix_t flip;
    switch (key->orientation)
    {
        case 2:
            cairo_translate (cr, -ha, -va); cairo_translate (cr, w, 0); cairo_translate (cr, x, y);
            cairo_matrix_init_identity (&flip); flip.xx = -1; cairo_transform (cr, &flip); break;
        case 4:
            cairo_translate (cr, -ha, -va); cairo_translate (cr, 0, h); cairo_translate (cr, x, y);
            cairo_matrix_init_identity (&flip); flip.yy = -1; cairo_transform (cr, &flip); break;
        case 5:
            cairo_rotate (cr, G_PI * 1.5); cairo_translate (cr, va, -ha); cairo_translate (cr, -y, x);
            cairo_matrix_init_identity (&flip); flip.xx = -1; cairo_transform (cr, &flip); break;
        case 7:
            cairo_rotate (cr, G_PI * 0.5); cairo_translate (cr, -va, ha); cairo_translate (cr, h, -w);
            cairo_translate (cr, y, -x); cairo_matrix_init_identity (&flip); flip.xx = -1; cairo_transform (cr, &flip); break;
        case 6:
            cairo_rotate (cr, G_PI * 0.5); cairo_translate (cr, -va, ha); cairo_translate (cr, 0, -w); cairo_translate (cr, y, -x); break;
        case 8:
            cairo_rotate (cr, G_PI * 1.5); cairo_translate (cr, va, -ha); cairo_translate (cr, -h, 0); cairo_translate (cr, -y, x); break;
        case 3:
            cairo_rotate (cr, G_PI); cairo_translate (cr, ha, va); cairo_translate (cr, -w, -h); cairo_translate (cr, -x, -y); break;
        default: cairo_translate (cr, -ha, -va); cairo_translate (cr, x, y); break;
    }
    cairo_scale (cr, key->x_scale, key->y_scale);
}

gboolean
rstto_image_render_key_equal (const RsttoRenderKey *a, const RsttoRenderKey *b)
{
    return a->width == b->width && a->height == b->height && a->pixel_scale == b->pixel_scale
           && a->source_scale == b->source_scale && a->smoothing == b->smoothing
           && a->orientation == b->orientation && a->render_width == b->render_width && a->render_height == b->render_height
           && a->x == b->x && a->y == b->y && a->h_adjust == b->h_adjust && a->v_adjust == b->v_adjust
           && a->x_scale == b->x_scale && a->y_scale == b->y_scale;
}

gboolean
rstto_image_render_weight (const RsttoRenderKey *key, guint64 *weight)
{
    if (key->width <= 0 || key->height <= 0 || key->width >= 32767 || key->height >= 32767)
        return FALSE;
    gint stride = cairo_format_stride_for_width (CAIRO_FORMAT_ARGB32, key->width);
    if (stride < 0 || (guint64) stride > G_MAXUINT64 / key->height)
        return FALSE;
    *weight = (guint64) stride * key->height;
    return TRUE;
}

void
rstto_image_render_pixels (cairo_surface_t *surface, RsttoRenderPixels *pixels)
{
    *pixels = (RsttoRenderPixels) { cairo_image_surface_get_data (surface),
        cairo_image_surface_get_width (surface), cairo_image_surface_get_height (surface),
        cairo_image_surface_get_stride (surface), cairo_image_surface_get_format (surface) };
}

gboolean
rstto_image_render_stripe (const RsttoRenderPixels *source, const RsttoRenderPixels *output,
                           const RsttoRenderKey *key, gint row, gint rows)
{
    g_return_val_if_fail (row >= 0 && rows > 0 && row <= output->height - rows, FALSE);
    cairo_surface_t *input = cairo_image_surface_create_for_data (source->data, source->format,
        source->width, source->height, source->stride);
    cairo_surface_set_device_scale (input, key->source_scale, key->source_scale);
    cairo_surface_t *target = cairo_image_surface_create_for_data (output->data, output->format,
        output->width, output->height, output->stride);
    cairo_t *cr = cairo_create (target);
    /* Integral device-pixel clipping, in the original full-image coordinates.
     * Each wrapper has private Cairo/Pixman state and writes disjoint rows. */
    if (row != 0 || rows != output->height)
    {
        cairo_rectangle (cr, 0, row, output->width, rows);
        cairo_clip (cr);
    }
    cairo_scale (cr, key->pixel_scale, key->pixel_scale);
    rstto_image_render_transform (cr, key);
    cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_surface (cr, input, 0, 0);
    cairo_pattern_set_filter (cairo_get_source (cr), key->smoothing ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
    cairo_paint (cr);
    gboolean ok = cairo_status (cr) == CAIRO_STATUS_SUCCESS;
    cairo_destroy (cr);
    cairo_surface_destroy (target);
    cairo_surface_destroy (input);
    return ok;
}

cairo_surface_t *
rstto_image_render (cairo_surface_t *source, const RsttoRenderKey *key, GCancellable *cancel)
{
    if (g_cancellable_is_cancelled (cancel))
        return NULL;
    cairo_surface_t *output = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, key->width, key->height);
    RsttoRenderPixels input, target;
    rstto_image_render_pixels (source, &input);
    rstto_image_render_pixels (output, &target);
    gboolean ok = cairo_surface_status (output) == CAIRO_STATUS_SUCCESS;
    /* Keep nearest sampling independent of clip-origin rounding. */
    gint step = key->smoothing ? RSTTO_RENDER_STRIPE_ROWS : key->height;
    for (gint row = 0; ok && row < key->height; row += step)
        ok = !g_cancellable_is_cancelled (cancel)
             && rstto_image_render_stripe (&input, &target, key, row, MIN (step, key->height - row));
    if (!ok || g_cancellable_is_cancelled (cancel))
    {
        cairo_surface_destroy (output);
        return NULL;
    }
    cairo_surface_mark_dirty (output);
    cairo_surface_set_device_scale (output, key->pixel_scale, key->pixel_scale);
    return output;
}

static void
convert_rgb_row (const guchar *restrict src, guint32 *restrict dst, gint width)
{
    gint x = 0;
#if defined(__SSE2__) && !defined(RSTTO_RENDER_SCALAR)
    /* SSE2 is baseline on x86-64. Other targets keep the scalar implementation.
     * Expand four packed RGB pixels into four native-endian Cairo RGB24 words.
     * A load reads 16 bytes, so leave at least six input pixels, even on the
     * last row with no padding. The scalar tail never reads past the pixbuf. */
    const __m128i m0 = _mm_set_epi32 (0, 0, 0, 0xffffff);
    const __m128i m1 = _mm_set_epi32 (0, 0, 0xffffff, 0);
    const __m128i m2 = _mm_set_epi32 (0, 0xffffff, 0, 0);
    const __m128i m3 = _mm_set_epi32 (0xffffff, 0, 0, 0);
    const __m128i blue = _mm_set1_epi32 (0xff), green = _mm_set1_epi32 (0xff00);
    const __m128i opaque = _mm_set1_epi32 ((gint) 0xff000000u);
    for (; x <= width - 6; x += 4)
    {
        __m128i in = _mm_loadu_si128 ((const __m128i *) (src + (gsize) x * 3));
        __m128i rgb = _mm_or_si128 (_mm_and_si128 (in, m0), _mm_and_si128 (_mm_slli_si128 (in, 1), m1));
        rgb = _mm_or_si128 (rgb, _mm_and_si128 (_mm_slli_si128 (in, 2), m2));
        rgb = _mm_or_si128 (rgb, _mm_and_si128 (_mm_slli_si128 (in, 3), m3));
        __m128i out = _mm_or_si128 (opaque, _mm_and_si128 (rgb, green));
        out = _mm_or_si128 (out, _mm_slli_epi32 (_mm_and_si128 (rgb, blue), 16));
        out = _mm_or_si128 (out, _mm_and_si128 (_mm_srli_epi32 (rgb, 16), blue));
        _mm_storeu_si128 ((__m128i *) (dst + x), out);
    }
#endif
    for (; x < width; x++)
        dst[x] = 0xff000000u | (src[3*x] << 16) | (src[3*x+1] << 8) | src[3*x+2];
}

cairo_surface_t *
rstto_image_convert (GdkPixbuf *pixbuf, gboolean (*keep_going) (gpointer), gpointer data)
{
    gint width = gdk_pixbuf_get_width (pixbuf), height = gdk_pixbuf_get_height (pixbuf);
    gboolean alpha = gdk_pixbuf_get_has_alpha (pixbuf);
    /* Glycin-backed pixbufs may use immutable GBytes. Asking for writable
     * pixels would silently copy the entire decoded image before conversion. */
#if GDK_PIXBUF_CHECK_VERSION(2, 32, 0)
    const guchar *pixels = gdk_pixbuf_read_pixels (pixbuf);
#else
    const guchar *pixels = gdk_pixbuf_get_pixels (pixbuf);
#endif
    gint input_stride = gdk_pixbuf_get_rowstride (pixbuf);
    cairo_surface_t *surface = cairo_image_surface_create (alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24, width, height);
    if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
        return surface;
    guchar *output = cairo_image_surface_get_data (surface);
    gint output_stride = cairo_image_surface_get_stride (surface);
    for (gint y = 0; y < height; y++)
    {
        if ((y % 64) == 0 && keep_going != NULL && !keep_going (data))
        {
            cairo_surface_destroy (surface);
            return NULL;
        }
        const guchar *restrict src = pixels + (gsize) y * input_stride;
        guint32 *restrict dst = (guint32 *) (output + (gsize) y * output_stride);
        if (!alpha)
            convert_rgb_row (src, dst, width);
        else
            for (gint x = 0; x < width; x++)
            {
                guint a = src[4*x+3];
                /* Some loaders expose opaque JPEGs as RGBA. Avoid three
                 * premultiplications for their opaque pixels as well. */
                if (a == 255)
                {
                    dst[x] = 0xff000000u | (src[4*x] << 16) | (src[4*x+1] << 8) | src[4*x+2];
                    continue;
                }
                guint r = src[4*x] * a + 128, g = src[4*x+1] * a + 128, b = src[4*x+2] * a + 128;
                dst[x] = (a << 24) | (((r + (r >> 8)) >> 8) << 16)
                         | (((g + (g >> 8)) >> 8) << 8) | ((b + (b >> 8)) >> 8);
            }
    }
    cairo_surface_mark_dirty (surface);
    return surface;
}

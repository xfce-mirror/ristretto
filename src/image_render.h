/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __RISTRETTO_IMAGE_RENDER_H__
#define __RISTRETTO_IMAGE_RENDER_H__
#include <cairo.h>
#include <gio/gio.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/* Internal, immutable snapshots. No GTK objects cross a worker boundary. */
typedef struct
{
    gint width, height;
    gdouble pixel_scale, source_scale;
    gint orientation, render_width, render_height, x, y, h_adjust, v_adjust;
    gdouble x_scale, y_scale;
    gboolean smoothing;
} RsttoRenderKey;

typedef struct
{
    gint viewport_width, viewport_height, orientation;
    gdouble image_scale, pixel_scale, scale, previous_scale;
    gint default_zoom;
    gdouble h_adjust, v_adjust;
    gboolean smoothing;
} RsttoRenderView;

typedef struct
{
    gint image_width, image_height, width, height, x, y, h_adjust, v_adjust;
    gdouble scale;
    gint auto_scale;
} RsttoRenderGeometry;

gboolean rstto_image_render_geometry (const RsttoRenderView *view, gint width, gint height,
                                      RsttoRenderGeometry *geometry);
gboolean rstto_image_render_key (const RsttoRenderView *view, const RsttoRenderGeometry *geometry,
                                 gint width, gint decoded_width, RsttoRenderKey *key);
void rstto_image_render_transform (cairo_t *cr, const RsttoRenderKey *key);
gboolean rstto_image_render_key_equal (const RsttoRenderKey *a, const RsttoRenderKey *b);
gboolean rstto_image_render_weight (const RsttoRenderKey *key, guint64 *weight);
cairo_surface_t *rstto_image_render (cairo_surface_t *source, const RsttoRenderKey *key, GCancellable *cancel);
/* Immutable storage descriptions: workers never share mutable Cairo objects. */
typedef struct
{
    guchar *data;
    gint width, height, stride;
    cairo_format_t format;
} RsttoRenderPixels;
#define RSTTO_RENDER_STRIPE_ROWS 128
void rstto_image_render_pixels (cairo_surface_t *surface, RsttoRenderPixels *pixels);
gboolean rstto_image_render_stripe (const RsttoRenderPixels *source, const RsttoRenderPixels *output,
                                    const RsttoRenderKey *key, gint row, gint rows);
cairo_surface_t *rstto_image_convert (GdkPixbuf *pixbuf, gboolean (*keep_going) (gpointer), gpointer data);
#endif

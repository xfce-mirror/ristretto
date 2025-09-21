/*
 *  Copyright (c) 2025 The Xfce Development Team
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 *  02110-1301, USA.
 */

#include "big_pattern.h"

/* Same as ceil(a / b) */
#define DIV_CEIL(a, b) ((a) / (b) + !!((a) % (b)))

typedef struct _RsttoBigPatternPrivate RsttoBigPatternPrivate;

struct _RsttoBigPatternPrivate
{
    guint width;
    guint height;

    /*
     * All tiles are this size, or smaller if they are
     * on the edge of the image
     */
    guint tile_size;

    /*
     * To ensure proper filtering, you need to reserve
     * the image paddings that will be overlapped
     *
     * Additionally, cairo blurs the edges of the image
     * with anti-aliasing enabled, so additional
     * padding is needed to avoid gaps
     */
    guint pad;

    /*
     * Tiles are cut into individual cairo_pattern_t
     * objects. Having a single cairo_pattern_t is
     * impossible, because it would act as one large
     * image, thereby nullifying the entire purpose of
     * RsttoBigPattern
     */
    guint n_patterns;
    guint n_rows;
    guint n_cols;
    cairo_pattern_t **patterns;
};

static void
rstto_big_pattern_finalize (GObject *object);

static void
get_tile_area (RsttoBigPattern *self,
               guint index,
               GdkRectangle *rectangle);

static void
cut_tiles (RsttoBigPattern *self,
           GdkPixbuf *pixbuf);

G_DEFINE_TYPE_WITH_PRIVATE (RsttoBigPattern, rstto_big_pattern, G_TYPE_OBJECT)

static void
rstto_big_pattern_class_init (RsttoBigPatternClass *klass)
{
    GObjectClass *object_class;

    object_class = G_OBJECT_CLASS (klass);
    object_class->finalize = rstto_big_pattern_finalize;
}

static void
rstto_big_pattern_init (RsttoBigPattern *self)
{
    RsttoBigPatternPrivate *priv = rstto_big_pattern_get_instance_private (self);

    /* Value taken from texture limit on old RPi */
    priv->tile_size = 2048;
}

static void
rstto_big_pattern_finalize (GObject *object)
{
    RsttoBigPattern *self = RSTTO_BIG_PATTERN (object);
    RsttoBigPatternPrivate *priv = rstto_big_pattern_get_instance_private (self);
    guint i;

    for (i = 0; i < priv->n_patterns; ++i)
    {
        cairo_pattern_destroy (priv->patterns[i]);
    }

    g_free (priv->patterns);
}

static void
get_tile_area (RsttoBigPattern *self,
               guint index,
               GdkRectangle *area)
{
    RsttoBigPatternPrivate *priv = rstto_big_pattern_get_instance_private (self);
    guint row = index / priv->n_cols;
    guint col = index % priv->n_cols;
    guint tile_size_nopad = priv->tile_size - priv->pad;

    /* Cutting tiles with the left and top edges overlapping the previous tile */
    area->x = MAX (0, (glong) (col * tile_size_nopad) - priv->pad);
    area->y = MAX (0, (glong) (row * tile_size_nopad) - priv->pad);

    area->width = MIN (tile_size_nopad + priv->pad * 2, priv->width - area->x);
    area->height = MIN (tile_size_nopad + priv->pad * 2, priv->height - area->y);
}

static void
cut_tiles (RsttoBigPattern *self,
           GdkPixbuf *pixbuf)
{
    RsttoBigPatternPrivate *priv = rstto_big_pattern_get_instance_private (self);
    GdkRectangle area;
    GdkPixbuf *tile_pixbuf;
    cairo_surface_t *tile_surface;
    cairo_pattern_t *tile_pattern;
    gboolean has_alpha;
    guint i;

    has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);

    for (i = 0; i < priv->n_patterns; ++i)
    {
        get_tile_area (self, i, &area);

        /* Need an alpha channel to blend intersecting edges */
        if (has_alpha)
        {
            tile_pixbuf = gdk_pixbuf_new_subpixbuf (pixbuf, area.x, area.y, area.width, area.height);
        }
        else
        {
            tile_pixbuf = gdk_pixbuf_new (gdk_pixbuf_get_colorspace (pixbuf),
                                          TRUE,
                                          gdk_pixbuf_get_bits_per_sample (pixbuf),
                                          area.width, area.height);

            gdk_pixbuf_copy_area (pixbuf,
                                  area.x, area.y,
                                  area.width, area.height,
                                  tile_pixbuf,
                                  0, 0);
        }

        tile_surface = gdk_cairo_surface_create_from_pixbuf (tile_pixbuf, 0, NULL);
        tile_pattern = cairo_pattern_create_for_surface (tile_surface);

        priv->patterns[i] = tile_pattern;

        g_object_unref (tile_pixbuf);
        cairo_surface_destroy (tile_surface);
    }
}

RsttoBigPattern *
rstto_big_pattern_new_from_pixbuf (GdkPixbuf *pixbuf)
{
    RsttoBigPattern *self = g_object_new (RSTTO_TYPE_BIG_PATTERN, NULL);
    RsttoBigPatternPrivate *priv = rstto_big_pattern_get_instance_private (self);

    priv->width = gdk_pixbuf_get_width (pixbuf);
    priv->height = gdk_pixbuf_get_height (pixbuf);

    /* 1024 is a good value that was derived experimentally */
    priv->pad = MAX (priv->width, priv->height) / 1024;
    /* 8 is the minimum value for correct edge blending */
    priv->pad = MAX (8, priv->pad);

    priv->n_rows = DIV_CEIL (priv->height, priv->tile_size);
    priv->n_cols = DIV_CEIL (priv->width, priv->tile_size);

    priv->n_patterns = priv->n_rows * priv->n_cols;
    priv->patterns = g_new0 (cairo_pattern_t *, priv->n_patterns);

    cut_tiles (self, pixbuf);

    return self;
}

void
rstto_big_pattern_set_device_scale (RsttoBigPattern *self,
                                    gdouble xscale,
                                    gdouble yscale)
{
    RsttoBigPatternPrivate *priv = rstto_big_pattern_get_instance_private (self);
    cairo_surface_t *surface;
    guint i;

    for (i = 0; i < priv->n_patterns; ++i)
    {
        cairo_pattern_get_surface (priv->patterns[i], &surface);
        cairo_surface_set_device_scale (surface, xscale, yscale);
    }
}

GdkPixbuf *
rstto_big_pattern_get_pixbuf (RsttoBigPattern *self)
{
    RsttoBigPatternPrivate *priv = rstto_big_pattern_get_instance_private (self);
    cairo_surface_t *surface;
    cairo_t *cr;
    GdkPixbuf *pixbuf;

    surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                          priv->width,
                                          priv->height);

    cr = cairo_create (surface);
    /* If there is no transformation, CAIRO_FILTER_NEAREST is best */
    rstto_big_pattern_cairo_paint (self, cr, 1.0, 1.0, CAIRO_FILTER_NEAREST);
    cairo_destroy (cr);

    pixbuf = gdk_pixbuf_get_from_surface (surface, 0, 0, priv->width, priv->height);
    cairo_surface_destroy (surface);

    return pixbuf;
}

void
rstto_big_pattern_cairo_paint (RsttoBigPattern *self,
                               cairo_t *cr,
                               gdouble xscale,
                               gdouble yscale,
                               cairo_filter_t filter)
{
    RsttoBigPatternPrivate *priv = rstto_big_pattern_get_instance_private (self);
    GdkRectangle area;
    guint i;

    for (i = 0; i < priv->n_patterns; ++i)
    {
        get_tile_area (self, i, &area);

        cairo_save (cr);
        cairo_scale (cr, xscale, yscale);
        cairo_translate (cr, area.x, area.y);
        cairo_pattern_set_filter (priv->patterns[i], filter);
        cairo_set_source (cr, priv->patterns[i]);
        cairo_paint (cr);
        cairo_restore (cr);
    }
}

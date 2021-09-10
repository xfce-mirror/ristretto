/*
 *  Copyright (c) Stephan Arts 2006-2012 <stephan@xfce.org>
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

#include "util.h"



static void
rstto_util_source_remove_all (gpointer  data,
                              GObject  *object)
{
    while (g_source_remove_by_user_data (object));
}



/* an often sufficient way to automate memory management of sources, without having
 * to store a source id and use an ad hoc handler */
gpointer
rstto_util_source_autoremove (gpointer object)
{
    g_return_val_if_fail (G_IS_OBJECT (object), object);

    if (! rstto_object_get_data (object, "source-autoremove"))
    {
        g_object_weak_ref (object, rstto_util_source_remove_all, NULL);
        rstto_object_set_data (object, "source-autoremove", GINT_TO_POINTER (TRUE));
    }

    return object;
}



/*
 * Workaround to avoid using Cairo Xlib backend which has some memory issues:
 * https://gitlab.freedesktop.org/cairo/cairo/-/issues/500
 * https://gitlab.freedesktop.org/cairo/cairo/-/issues/510
 */
void
rstto_util_set_source_pixbuf (cairo_t *ctx,
                              const GdkPixbuf *pixbuf,
                              gint width,
                              gint height,
                              gdouble pixbuf_x,
                              gdouble pixbuf_y)
{
    cairo_t *cr;
    cairo_surface_t *surface;
    cairo_format_t format;

    /* for non-Xlib backends, this is just a wrapper */
    if (cairo_surface_get_type (cairo_get_target (ctx)) != CAIRO_SURFACE_TYPE_XLIB)
    {
        gdk_cairo_set_source_pixbuf (ctx, pixbuf, pixbuf_x, pixbuf_y);
        return;
    }

    /* copied from gdk_cairo_set_source_pixbuf() */
    if (gdk_pixbuf_get_n_channels (pixbuf) == 3)
        format = CAIRO_FORMAT_RGB24;
    else
        format = CAIRO_FORMAT_ARGB32;

    /* create a generic image surface on which to apply gdk_cairo_set_source_pixbuf() */
    surface = cairo_image_surface_create (format, width, height);
    cr = cairo_create (surface);

    /* apply it and put the resulting source in the original context */
    gdk_cairo_set_source_pixbuf (cr, pixbuf, pixbuf_x, pixbuf_y);
    cairo_set_source (ctx, cairo_get_source (cr));

    /* cleanup */
    cairo_surface_destroy (surface);
    cairo_destroy (cr);
}

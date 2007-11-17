/*
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <gtk/gtk.h>
#include <gtk/gtkmarshal.h>
#include <string.h>

#include <thunar-vfs/thunar-vfs.h>
#include <libexif/exif-data.h>

#include "navigator.h"
#include "picture_viewer.h"
#include "thumbnail_viewer.h"

struct _RsttoThumbnailViewerPriv
{
    GtkOrientation  orientation;
    RsttoNavigator *navigator;
    gint dimension;
    gint offset;
    gboolean auto_center;
    gint selection;
    gint begin;
    gint end;
};

static void
rstto_thumbnail_viewer_init(RsttoThumbnailViewer *);
static void
rstto_thumbnail_viewer_class_init(RsttoThumbnailViewerClass *);
static void
rstto_thumbnail_viewer_destroy(GtkObject *object);

static void
rstto_thumbnail_viewer_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_thumbnail_viewer_size_allocate(GtkWidget *, GtkAllocation *);
static void
rstto_thumbnail_viewer_realize(GtkWidget *);
static gboolean 
rstto_thumbnail_viewer_expose(GtkWidget *, GdkEventExpose *);

static void
rstto_thumbnail_viewer_paint(RsttoThumbnailViewer *viewer);
static gboolean
rstto_thumbnail_viewer_paint_entry (RsttoThumbnailViewer *viewer, gint nr, gboolean selected);

static GtkWidgetClass *parent_class = NULL;

static void
cb_rstto_thumbnail_viewer_nav_new_entry (RsttoNavigator *nav,
                                    gint nr,
                                    RsttoNavigatorEntry *entry,
                                    RsttoThumbnailViewer *viewer);
static void
cb_rstto_thumbnail_viewer_nav_iter_changed (RsttoNavigator *nav,
                                       gint nr,
                                       RsttoNavigatorEntry *entry,
                                       RsttoThumbnailViewer *viewer);
static void
cb_rstto_thumbnail_viewer_nav_reordered (RsttoNavigator *nav,
                                    RsttoThumbnailViewer *viewer);

static void
cb_rstto_thumbnail_viewer_button_press_event (RsttoThumbnailViewer *viewer, GdkEventButton *event);
static void
cb_rstto_thumbnail_viewer_scroll_event (RsttoThumbnailViewer *viewer, GdkEventScroll *event);

GType
rstto_thumbnail_viewer_get_type ()
{
    static GType rstto_thumbnail_viewer_type = 0;

    if (!rstto_thumbnail_viewer_type)
    {
        static const GTypeInfo rstto_thumbnail_viewer_info = 
        {
            sizeof (RsttoThumbnailViewerClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_thumbnail_viewer_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoThumbnailViewer),
            0,
            (GInstanceInitFunc) rstto_thumbnail_viewer_init,
            NULL
        };

        rstto_thumbnail_viewer_type = g_type_register_static (GTK_TYPE_WIDGET, "RsttoThumbnailViewer", &rstto_thumbnail_viewer_info, 0);
    }
    return rstto_thumbnail_viewer_type;
}


static void
rstto_thumbnail_viewer_init(RsttoThumbnailViewer *viewer)
{
    viewer->priv = g_new0(RsttoThumbnailViewerPriv, 1);

    viewer->priv->auto_center = TRUE;

    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(viewer), TRUE);
    gtk_widget_set_events (GTK_WIDGET(viewer),
                           GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(viewer), "button_press_event", G_CALLBACK(cb_rstto_thumbnail_viewer_button_press_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "scroll_event", G_CALLBACK(cb_rstto_thumbnail_viewer_scroll_event), NULL);
    viewer->priv->orientation = GTK_ORIENTATION_HORIZONTAL;

}

static void
rstto_thumbnail_viewer_class_init(RsttoThumbnailViewerClass *viewer_class)
{
    GtkWidgetClass *widget_class;
    GtkObjectClass *object_class;

    widget_class = (GtkWidgetClass*)viewer_class;
    object_class = (GtkObjectClass*)viewer_class;

    parent_class = g_type_class_peek_parent(viewer_class);

    widget_class->realize = rstto_thumbnail_viewer_realize;
    widget_class->expose_event = rstto_thumbnail_viewer_expose;

    widget_class->size_request = rstto_thumbnail_viewer_size_request;
    widget_class->size_allocate = rstto_thumbnail_viewer_size_allocate;

    object_class->destroy = rstto_thumbnail_viewer_destroy;
}

static void
rstto_thumbnail_viewer_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->height = 70;
    requisition->width = 70;
}

static void
rstto_thumbnail_viewer_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    RsttoThumbnailViewer *viewer = RSTTO_THUMBNAIL_VIEWER(widget);
    gint border_width =  0;
    widget->allocation = *allocation;

    if (GTK_WIDGET_REALIZED (widget))
    {
         gdk_window_move_resize (widget->window,
            allocation->x + border_width,
            allocation->y + border_width,
            allocation->width - border_width * 2,
            allocation->height - border_width * 2);

    }

    switch(viewer->priv->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            viewer->priv->dimension = widget->allocation.height;
            break;
        case GTK_ORIENTATION_VERTICAL:
            viewer->priv->dimension = widget->allocation.width;
            break;
    }
}

static void
rstto_thumbnail_viewer_realize(GtkWidget *widget)
{
    g_return_if_fail (widget != NULL);
    g_return_if_fail (RSTTO_IS_THUMBNAIL_VIEWER(widget));

    GdkWindowAttr attributes;
    gint attributes_mask;

    GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

    attributes.x = widget->allocation.x;
    attributes.y = widget->allocation.y;
    attributes.width = widget->allocation.width;
    attributes.height = widget->allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events (widget) | 
    GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK;
    attributes.visual = gtk_widget_get_visual (widget);
    attributes.colormap = gtk_widget_get_colormap (widget);

    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
    widget->window = gdk_window_new (gtk_widget_get_parent_window(widget), &attributes, attributes_mask);

    widget->style = gtk_style_attach (widget->style, widget->window);
    gdk_window_set_user_data (widget->window, widget);

    gtk_style_set_background (widget->style, widget->window, GTK_STATE_ACTIVE);
}

static gboolean
rstto_thumbnail_viewer_expose(GtkWidget *widget, GdkEventExpose *event)
{
    RsttoThumbnailViewer *viewer = RSTTO_THUMBNAIL_VIEWER(widget);

    if (GTK_WIDGET_REALIZED (viewer))
    {
        rstto_thumbnail_viewer_paint(viewer);
    }

    return FALSE;
}

/*
 * rstto_thumbnail_viewer_paint:
 *
 * @viewer   : The ThumbnailViewer to paint
 *
 */
static void
rstto_thumbnail_viewer_paint(RsttoThumbnailViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    gint nr = rstto_navigator_get_position(viewer->priv->navigator);
    gint n_entries = rstto_navigator_get_n_files(viewer->priv->navigator);
    gint offset = 0, i;
    if (viewer->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        offset  = (((widget->allocation.width - viewer->priv->dimension) / 2) - nr * viewer->priv->dimension);
        gint min = nr - (((widget->allocation.width - viewer->priv->dimension) / 2) / viewer->priv->dimension) - 1;
        gint max = nr + (((widget->allocation.width - viewer->priv->dimension) / 2) / viewer->priv->dimension) + 1;
        viewer->priv->begin = min < 0? 0 : min;
        viewer->priv->end = max >= n_entries? n_entries - 1: max;
    }
    else
    {
        offset  = (((widget->allocation.height - viewer->priv->dimension) / 2) - nr * viewer->priv->dimension);
        gint min = nr - (((widget->allocation.height - viewer->priv->dimension) / 2) / viewer->priv->dimension) - 1;
        gint max = nr + (((widget->allocation.height - viewer->priv->dimension) / 2) / viewer->priv->dimension) + 1;
        viewer->priv->begin = min < 0? 0 : min;
        viewer->priv->end = max >= n_entries? n_entries - 1: max;
    }

    viewer->priv->offset = offset;

    /* Draw any visible thumbnail on screen */
    for (i = viewer->priv->begin; i <= viewer->priv->end; ++i)
    {
        rstto_thumbnail_viewer_paint_entry(viewer, i, nr == i);
    }

    if (viewer->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        /* Clear the area located prior to the first thumbnail */
        if (offset > 0)
        {
            gdk_window_clear_area(widget->window, 
                                0,
                                0,
                                offset,
                                widget->allocation.height);
        }

        /* Clear the area located after the last thumbnail */
        if (offset + ((viewer->priv->end + 1) * viewer->priv->dimension) < widget->allocation.width)
        {
            gdk_window_clear_area(widget->window, 
                                offset + ((viewer->priv->end + 1) * viewer->priv->dimension),
                                0,
                                widget->allocation.width - (offset + ((viewer->priv->end  + 1)* viewer->priv->dimension)),
                                widget->allocation.height);

        }
    }
    else
    {
        /* Clear the area located prior to the first thumbnail */
        if (offset > 0)
        {
            gdk_window_clear_area(widget->window, 
                                0,
                                0,
                                widget->allocation.width,
                                offset);
        }

        /* Clear the area located after the last thumbnail */
        if (offset + ((viewer->priv->end + 1) * viewer->priv->dimension) < widget->allocation.height)
        {
            gdk_window_clear_area(widget->window, 
                                0,
                                offset + ((viewer->priv->end + 1) * viewer->priv->dimension),
                                widget->allocation.width,
                                widget->allocation.height - (offset + ((viewer->priv->end + 1) * viewer->priv->dimension)));
        }
    }
}

static void
rstto_thumbnail_viewer_destroy(GtkObject *object)
{

}

GtkWidget *
rstto_thumbnail_viewer_new(RsttoNavigator *navigator)
{
    RsttoThumbnailViewer *viewer;

    viewer = g_object_new(RSTTO_TYPE_THUMBNAIL_VIEWER, NULL);

    viewer->priv->navigator = navigator;

    g_signal_connect(G_OBJECT(navigator), "new-entry", G_CALLBACK(cb_rstto_thumbnail_viewer_nav_new_entry), viewer);
    g_signal_connect(G_OBJECT(navigator), "iter-changed", G_CALLBACK(cb_rstto_thumbnail_viewer_nav_iter_changed), viewer);
    g_signal_connect(G_OBJECT(navigator), "reordered", G_CALLBACK(cb_rstto_thumbnail_viewer_nav_reordered), viewer);

    return (GtkWidget *)viewer;
}

/*
 * cb_rstto_thumbnail_viewer_scroll_event:
 *
 * @viewer   : ThumbnailViewer
 * @event    : scroll event
 *
 */
static void
cb_rstto_thumbnail_viewer_scroll_event (RsttoThumbnailViewer *viewer, GdkEventScroll *event)
{
    switch(event->direction)
    {
        case GDK_SCROLL_UP:
        case GDK_SCROLL_LEFT:
            rstto_navigator_jump_back(viewer->priv->navigator);
            break;
        case GDK_SCROLL_DOWN:
        case GDK_SCROLL_RIGHT:
                rstto_navigator_jump_forward(viewer->priv->navigator);
            break;
    }
}


/*
 * cb_rstto_thumbnail_viewer_button_press_event:
 *
 * @viewer   : ThumbnailViewer
 * @event    : scroll event
 *
 */
static void
cb_rstto_thumbnail_viewer_button_press_event (RsttoThumbnailViewer *viewer,
                                         GdkEventButton *event)
{
    gint n = 0;
    if (event->button == 1)
    {
        switch(viewer->priv->orientation)
        {
            case GTK_ORIENTATION_HORIZONTAL:
                n = (event->x - viewer->priv->offset) / viewer->priv->dimension;
                break;
            case GTK_ORIENTATION_VERTICAL:
                n = (event->y - viewer->priv->offset) / viewer->priv->dimension;
                break;
        }
        rstto_navigator_set_file(viewer->priv->navigator, n);
    }
}

/*
 * rstto_thumbnail_viewer_set_orientation:
 *
 * @viewer      : ThumbnailViewer
 * @orientation :
 *
 */
void
rstto_thumbnail_viewer_set_orientation (RsttoThumbnailViewer *viewer, GtkOrientation orientation)
{
    viewer->priv->orientation = orientation;
}

/*
 * rstto_thumbnail_viewer_get_orientation:
 *
 * @viewer      : ThumbnailViewer
 *
 * Return value : GtkOrientation
 *
 */
GtkOrientation
rstto_thumbnail_viewer_get_orientation (RsttoThumbnailViewer *viewer)
{
    return viewer->priv->orientation;
}

/*
 * cb_rstto_thumbnail_viewer_nav_new_entry :
 *
 * @nav    : RsttoNavigator 
 * @nr     : nr
 * @entry  :
 * @viewer :
 *
 */
static void
cb_rstto_thumbnail_viewer_nav_new_entry(RsttoNavigator *nav, gint nr, RsttoNavigatorEntry *entry, RsttoThumbnailViewer *viewer)
{
    if ((nr < viewer->priv->begin))
    {
        viewer->priv->offset -= viewer->priv->dimension;
    }
    /* only paint if the entry is within the visible range */
    if ((nr > viewer->priv->begin) && (nr < viewer->priv->end))
    {
        if (GTK_WIDGET_REALIZED(viewer))
        {
            rstto_thumbnail_viewer_paint(viewer);
        }
    }
}

/*
 * cb_rstto_thumbnail_viewer_nav_iter_changed :
 *
 * @nav    : RsttoNavigator 
 * @nr     : nr
 * @entry  :
 * @viewer :
 *
 */
static void
cb_rstto_thumbnail_viewer_nav_iter_changed(RsttoNavigator *nav, gint nr, RsttoNavigatorEntry *entry, RsttoThumbnailViewer *viewer)
{
    if (GTK_WIDGET_REALIZED(viewer))
    {
        rstto_thumbnail_viewer_paint(viewer);
    }
}

/*
 * cb_rstto_thumbnail_viewer_nav_reordered :
 *
 * @nav    : RsttoNavigator 
 * @viewer :
 *
 */
static void
cb_rstto_thumbnail_viewer_nav_reordered (RsttoNavigator *nav, RsttoThumbnailViewer *viewer)
{
    if (GTK_WIDGET_REALIZED(viewer))
    {
        rstto_thumbnail_viewer_paint(viewer);
    }
}

/*
 * rstto_thumbnail_viewer_paint_entry:
 *
 * @viewer   : ThumbnailViewer
 * @nr       : entry nr
 * @selected : selected state
 *
 * Return value: %TRUE on success, else %FALSE
 */
static gboolean
rstto_thumbnail_viewer_paint_entry (RsttoThumbnailViewer *viewer, gint nr, gboolean selected)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    RsttoNavigatorEntry *entry = rstto_navigator_get_nth_file(viewer->priv->navigator, nr);
    GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(widget->window));
    GdkGC *gc_bg_normal = gdk_gc_new(GDK_DRAWABLE(widget->window));
    GdkGC *gc_bg_selected = gdk_gc_new(GDK_DRAWABLE(widget->window));

    gdk_gc_set_foreground(gc_bg_selected,
                        &(widget->style->bg[GTK_STATE_SELECTED]));
    gdk_gc_set_foreground(gc_bg_normal,
                        &(widget->style->bg[GTK_STATE_NORMAL]));
    GdkPixmap *pixmap = NULL;
    if (entry)
    {
        GdkPixbuf *pixbuf = rstto_navigator_get_entry_thumb(viewer->priv->navigator, entry, viewer->priv->dimension - 4);
        pixmap = gdk_pixmap_new(widget->window, viewer->priv->dimension, viewer->priv->dimension, -1);

        if(selected)
        {
            gdk_gc_set_foreground(gc, &widget->style->fg[GTK_STATE_SELECTED]);
            gdk_draw_rectangle(GDK_DRAWABLE(pixmap),
                            gc_bg_selected,
                            TRUE,
                            0, 0, 
                            viewer->priv->dimension,
                            viewer->priv->dimension);
        }
        else
        {
            gdk_gc_set_foreground(gc, &widget->style->fg[GTK_STATE_NORMAL]);
            gdk_draw_rectangle(GDK_DRAWABLE(pixmap),
                            gc_bg_normal,
                            TRUE,
                            0, 0, 
                            viewer->priv->dimension,
                            viewer->priv->dimension);
        }
        gdk_draw_rectangle(GDK_DRAWABLE(pixmap),
                            gc,
                            FALSE,
                            1, 1, 
                            viewer->priv->dimension - 3,
                            viewer->priv->dimension - 3);

        if(pixbuf)
        {
            gdk_draw_pixbuf(GDK_DRAWABLE(pixmap),
                            gc,
                            pixbuf,
                            0, 0,
                            (0.5 * (viewer->priv->dimension - gdk_pixbuf_get_width(pixbuf))),
                            (0.5 * (viewer->priv->dimension  - gdk_pixbuf_get_height(pixbuf))),
                            -1, -1,
                            GDK_RGB_DITHER_NORMAL,
                            0, 0);
        }

        switch (viewer->priv->orientation)
        {
            case GTK_ORIENTATION_HORIZONTAL:
                gdk_draw_drawable(GDK_DRAWABLE(widget->window),
                            gc,
                            pixmap,
                            0, 0,
                            viewer->priv->offset + ( nr * viewer->priv->dimension),
                            0,
                            viewer->priv->dimension,
                            viewer->priv->dimension);

                break;
            case GTK_ORIENTATION_VERTICAL:
                gdk_draw_drawable(GDK_DRAWABLE(widget->window),
                            gc,
                            pixmap,
                            0, 0,
                            0,
                            viewer->priv->offset + (nr * viewer->priv->dimension),
                            viewer->priv->dimension,
                            viewer->priv->dimension);
                break;
        }
    }
    return TRUE;
}

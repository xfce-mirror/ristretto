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

#include <pango/pango.h>
#include <pango/pangoxft.h>

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
cb_rstto_thumbnailer_nav_new_entry (RsttoNavigator *nav,
                                    gint nr,
                                    RsttoNavigatorEntry *entry,
                                    RsttoThumbnailViewer *viewer);
static void
cb_rstto_thumbnailer_nav_iter_changed (RsttoNavigator *nav,
                                       gint nr,
                                       RsttoNavigatorEntry *entry,
                                       RsttoThumbnailViewer *viewer);
static void
cb_rstto_thumbnailer_nav_reordered (RsttoNavigator *nav,
                                    RsttoThumbnailViewer *viewer);

static void
cb_rstto_thumbnailer_button_press_event (RsttoThumbnailViewer *viewer, GdkEventButton *event);
static void
cb_rstto_thumbnailer_scroll_event (RsttoThumbnailViewer *viewer, GdkEventScroll *event);

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
    g_signal_connect(G_OBJECT(viewer), "button_press_event", G_CALLBACK(cb_rstto_thumbnailer_button_press_event), NULL);
    g_signal_connect(G_OBJECT(viewer), "scroll_event", G_CALLBACK(cb_rstto_thumbnailer_scroll_event), NULL);
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
    RsttoThumbnailViewer *viewer = RSTTO_THUMBNAIL_VIEWER(widget);
    switch(viewer->priv->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            requisition->height = 74;
            break;
        case GTK_ORIENTATION_VERTICAL:
            requisition->width = 74;
            break;
    }
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
            viewer->priv->begin = viewer->priv->offset / viewer->priv->dimension;
            viewer->priv->end = widget->allocation.width / viewer->priv->dimension + viewer->priv->begin;
            break;
        case GTK_ORIENTATION_VERTICAL:
            viewer->priv->dimension = widget->allocation.width;
            viewer->priv->begin = viewer->priv->offset / viewer->priv->dimension;
            viewer->priv->end = widget->allocation.height / viewer->priv->dimension + viewer->priv->begin;
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
        viewer->priv->begin = viewer->priv->offset / viewer->priv->dimension;
        if (viewer->priv->orientation == GTK_ORIENTATION_VERTICAL)
            viewer->priv->end = widget->allocation.height / viewer->priv->dimension + viewer->priv->begin;
        else 
            viewer->priv->end = widget->allocation.width / viewer->priv->dimension + viewer->priv->begin;

        rstto_thumbnail_viewer_paint(viewer);
    }

    return FALSE;
}

static void
rstto_thumbnail_viewer_paint(RsttoThumbnailViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    GdkColor color;

    gint position = rstto_navigator_get_position(viewer->priv->navigator);

    color.pixel = 0xffffffff;
    GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(widget->window));
    GdkGC *gc_bg_normal = gdk_gc_new(GDK_DRAWABLE(widget->window));
    GdkGC *gc_bg_selected = gdk_gc_new(GDK_DRAWABLE(widget->window));

    gdk_gc_set_foreground(gc, &color);
    gdk_gc_set_foreground(gc_bg_selected,
                        &(widget->style->bg[GTK_STATE_SELECTED]));
    gdk_gc_set_foreground(gc_bg_normal,
                        &(widget->style->bg[GTK_STATE_NORMAL]));
    
    gint i;
    switch (viewer->priv->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            if (widget->allocation.width > (viewer->priv->end * viewer->priv->dimension - viewer->priv->offset))
            {
                gdk_window_clear_area(widget->window, 
                                        (viewer->priv->dimension * viewer->priv->end) - viewer->priv->offset, 
                                        0,
                                        widget->allocation.width - (viewer->priv->dimension * viewer->priv->end - viewer->priv->offset) - 16,
                                        widget->allocation.height);
            }
            break;
        case GTK_ORIENTATION_VERTICAL:
            if (widget->allocation.height > (viewer->priv->end * viewer->priv->dimension) - viewer->priv->offset)
            {
                gdk_window_clear_area(widget->window, 
                                        0,
                                        (viewer->priv->dimension * viewer->priv->end) - viewer->priv->offset, 
                                        widget->allocation.width,
                                        widget->allocation.height - (viewer->priv->dimension * viewer->priv->end - viewer->priv->offset) - 16);
            }
            break;
    }
    for(i = viewer->priv->begin; i <= viewer->priv->end; ++i)
    { 
        rstto_thumbnail_viewer_paint_entry(viewer, i , i==position?TRUE:FALSE);
    }
    switch (viewer->priv->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            gtk_paint_box(widget->style,
                            widget->window,
                            GTK_STATE_NORMAL,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            0, 0, 16, viewer->priv->dimension);
            gtk_paint_arrow(widget->style,
                            widget->window,
                            viewer->priv->offset == 0?GTK_STATE_INSENSITIVE:GTK_STATE_NORMAL,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            GTK_ARROW_LEFT,
                            TRUE,
                            0,viewer->priv->dimension / 2 - 7,14,14);

            gtk_paint_box(widget->style,
                            widget->window,
                            GTK_STATE_NORMAL,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            widget->allocation.width - 16, 0, 16, viewer->priv->dimension);
            gtk_paint_arrow(widget->style,
                            widget->window,
                            ((rstto_navigator_get_n_files(viewer->priv->navigator) * viewer->priv->dimension) - viewer->priv->offset) > (widget->allocation.width-32)?GTK_STATE_NORMAL:GTK_STATE_INSENSITIVE,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            GTK_ARROW_RIGHT,
                            TRUE,
                            widget->allocation.width - 16, viewer->priv->dimension / 2 - 7,14,14);
            break;
        case GTK_ORIENTATION_VERTICAL:
            gtk_paint_box(widget->style,
                            widget->window,
                            GTK_STATE_NORMAL,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            0, 0, viewer->priv->dimension, 16);
            gtk_paint_arrow(widget->style,
                            widget->window,
                            viewer->priv->offset == 0?GTK_STATE_INSENSITIVE:GTK_STATE_NORMAL,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            GTK_ARROW_UP,
                            TRUE,
                            viewer->priv->dimension / 2 - 7,0,14,14);

            gtk_paint_box(widget->style,
                            widget->window,
                            GTK_STATE_NORMAL,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            0, widget->allocation.height - 16, viewer->priv->dimension, 16);
            gtk_paint_arrow(widget->style,
                            widget->window,
                            (i * viewer->priv->dimension) - viewer->priv->offset > widget->allocation.height - 32?GTK_STATE_NORMAL:GTK_STATE_INSENSITIVE,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            GTK_ARROW_DOWN,
                            TRUE,
                            viewer->priv->dimension / 2 - 7, widget->allocation.height - 16,14,14);
            break;
    }
    GdkCursor *cursor = gdk_cursor_new(GDK_LEFT_PTR);
    gdk_window_set_cursor(widget->window, cursor);
    gdk_cursor_unref(cursor);
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

    g_signal_connect(G_OBJECT(navigator), "new-entry", G_CALLBACK(cb_rstto_thumbnailer_nav_new_entry), viewer);
    g_signal_connect(G_OBJECT(navigator), "iter-changed", G_CALLBACK(cb_rstto_thumbnailer_nav_iter_changed), viewer);
    g_signal_connect(G_OBJECT(navigator), "reordered", G_CALLBACK(cb_rstto_thumbnailer_nav_reordered), viewer);

    return (GtkWidget *)viewer;
}

static void
cb_rstto_thumbnailer_button_press_event (RsttoThumbnailViewer *viewer,
                                         GdkEventButton *event)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    gint n = 0;
    gint old_offset = viewer->priv->offset;
    if (event->button == 1)
    {
        switch(viewer->priv->orientation)
        {
            case GTK_ORIENTATION_HORIZONTAL:
                if(event->button == 1)
                {
                    if ((event->x < 20) || ((widget->allocation.width - event->x) < 20))
                        n = -1;
                    else
                        n = (event->x - 20 + viewer->priv->offset) / viewer->priv->dimension;
                }
                break;
            case GTK_ORIENTATION_VERTICAL:
                if(event->button == 1)
                {
                    if ((event->y < 20) || ((widget->allocation.height - event->y) < 20))
                        n = -1;
                    else
                        n = (event->y - 20 + viewer->priv->offset) / viewer->priv->dimension;

                }
                break;

        }
        if (n < 0)
        {
            if (((viewer->priv->orientation == GTK_ORIENTATION_HORIZONTAL) && (event->x < 20)) ||
                ((viewer->priv->orientation == GTK_ORIENTATION_VERTICAL) && (event->y < 20)))
            {
                viewer->priv->offset -= viewer->priv->dimension;
                if(viewer->priv->offset < 0)
                {
                    viewer->priv->offset = 0;
                }
                viewer->priv->begin = viewer->priv->offset / viewer->priv->dimension;
                if (viewer->priv->orientation == GTK_ORIENTATION_VERTICAL)
                    viewer->priv->end = widget->allocation.height / viewer->priv->dimension + viewer->priv->begin;
                else 
                    viewer->priv->end = widget->allocation.width / viewer->priv->dimension + viewer->priv->begin;
            }
            else
            {
                if(rstto_navigator_get_n_files(viewer->priv->navigator) == 0)
                    viewer->priv->offset = 0;
                else
                {
                    if(viewer->priv->orientation == GTK_ORIENTATION_VERTICAL)
                    {
                        if((rstto_navigator_get_n_files(viewer->priv->navigator) * viewer->priv->dimension - viewer->priv->offset) > widget->allocation.height)
                        {
                            viewer->priv->offset += viewer->priv->dimension / 2;
                            viewer->priv->begin = viewer->priv->offset / viewer->priv->dimension;
                            viewer->priv->end = widget->allocation.height / viewer->priv->dimension + viewer->priv->begin;
                        }
                    }
                    if(viewer->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
                    {
                        if((rstto_navigator_get_n_files(viewer->priv->navigator) * viewer->priv->dimension - viewer->priv->offset) > widget->allocation.width)
                        {
                            viewer->priv->offset += viewer->priv->dimension / 2;
                            viewer->priv->begin = viewer->priv->offset / viewer->priv->dimension;
                            viewer->priv->end = widget->allocation.width / viewer->priv->dimension + viewer->priv->begin;
                        }
                    }
                }
            }
            if(old_offset != viewer->priv->offset)
                rstto_thumbnail_viewer_paint(viewer);
        }
        else
        {
            if ( n < rstto_navigator_get_n_files(viewer->priv->navigator))
            {
                if(GTK_WIDGET_REALIZED(widget))
                {
                    GdkCursor *cursor = gdk_cursor_new(GDK_WATCH);
                    gdk_window_set_cursor(widget->window, cursor);
                    gdk_cursor_unref(cursor);
                }
                rstto_navigator_set_file(viewer->priv->navigator, n);
            }
        }
    }
}

static void
cb_rstto_thumbnailer_scroll_event (RsttoThumbnailViewer *viewer, GdkEventScroll *event)
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

void
rstto_thumbnail_viewer_set_orientation (RsttoThumbnailViewer *viewer, GtkOrientation orientation)
{
    viewer->priv->orientation = orientation;
}

GtkOrientation
rstto_thumbnail_viewer_get_orientation (RsttoThumbnailViewer *viewer)
{
    return viewer->priv->orientation;
}

static void
cb_rstto_thumbnailer_nav_new_entry(RsttoNavigator *nav, gint nr, RsttoNavigatorEntry *entry, RsttoThumbnailViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    if (GTK_WIDGET_REALIZED(viewer))
    {
        /* Check if the entry is visible */
        if (viewer->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
            viewer->priv->end = widget->allocation.width / viewer->priv->dimension + viewer->priv->begin;
        else
            viewer->priv->end = widget->allocation.height / viewer->priv->dimension + viewer->priv->begin;
        if ((nr >= viewer->priv->begin) && (nr <= viewer->priv->end))
        {
            RsttoNavigatorEntry *s_entry = rstto_navigator_get_file(nav);
            if (s_entry == entry)
            {
                rstto_thumbnail_viewer_paint_entry(viewer, nr, TRUE);
            }
            else
            {
                rstto_thumbnail_viewer_paint_entry(viewer, nr, FALSE);
            }
        }
    }
}

static void
cb_rstto_thumbnailer_nav_iter_changed(RsttoNavigator *nav, gint nr, RsttoNavigatorEntry *entry, RsttoThumbnailViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    if (GTK_WIDGET_REALIZED(viewer))
    {
        GdkCursor *cursor = gdk_cursor_new(GDK_WATCH);
        gdk_window_set_cursor(widget->window, cursor);
        gdk_cursor_unref(cursor);
        
        if (viewer->priv->auto_center == TRUE)
        {
            gint old_offset = viewer->priv->offset;
            switch (viewer->priv->orientation)
            {
                case GTK_ORIENTATION_HORIZONTAL:
                    viewer->priv->offset = nr * viewer->priv->dimension - widget->allocation.width / 2 +viewer->priv->dimension / 2;
                    if (viewer->priv->offset < 0)
                    {
                        viewer->priv->offset = 0;
                        viewer->priv->begin = 0;
                        viewer->priv->end = widget->allocation.width / viewer->priv->dimension;
                    }
                    else
                    {
                        viewer->priv->begin = viewer->priv->offset / viewer->priv->dimension;
                        viewer->priv->end = widget->allocation.width / viewer->priv->dimension + viewer->priv->begin;
                    }
                    break;
                case GTK_ORIENTATION_VERTICAL:
                    viewer->priv->offset = nr * viewer->priv->dimension - widget->allocation.height / 2 +viewer->priv->dimension / 2;
                    if (viewer->priv->offset < 0)
                    {
                        viewer->priv->offset = 0;
                        viewer->priv->begin = 0;
                        viewer->priv->end = widget->allocation.height / viewer->priv->dimension;
                    }
                    else
                    {
                        viewer->priv->begin = viewer->priv->offset / viewer->priv->dimension;
                        viewer->priv->end = widget->allocation.height / viewer->priv->dimension + viewer->priv->begin;
                    }
                    break;
            }
            if (viewer->priv->end > rstto_navigator_get_n_files(viewer->priv->navigator))
            {
                viewer->priv->end = rstto_navigator_get_n_files(viewer->priv->navigator);
            }
            if (viewer->priv->offset == old_offset)
            {
                rstto_thumbnail_viewer_paint_entry(viewer, rstto_navigator_get_old_position(viewer->priv->navigator), FALSE);
                rstto_thumbnail_viewer_paint_entry(viewer, nr, TRUE);
            }
            else
            {
                rstto_thumbnail_viewer_paint(viewer);
            }
        }
        else
        {
            rstto_thumbnail_viewer_paint_entry(viewer, rstto_navigator_get_old_position(viewer->priv->navigator), FALSE);
            RsttoNavigatorEntry *s_entry = rstto_navigator_get_file(nav);
            if (s_entry == entry)
            {
                rstto_thumbnail_viewer_paint_entry(viewer, nr, TRUE);
            }
            else
            {
                rstto_thumbnail_viewer_paint_entry(viewer, nr, FALSE);
            }
        }
        

        cursor = gdk_cursor_new(GDK_LEFT_PTR);
        gdk_window_set_cursor(widget->window, cursor);
        gdk_cursor_unref(cursor);
    }
}

static void
cb_rstto_thumbnailer_nav_reordered (RsttoNavigator *nav, RsttoThumbnailViewer *viewer)
{
    if (GTK_WIDGET_REALIZED(viewer))
    {
        rstto_thumbnail_viewer_paint(viewer);
    }
}

static gboolean
rstto_thumbnail_viewer_paint_entry (RsttoThumbnailViewer *viewer, gint nr, gboolean selected)
{
    GdkColor color;
    GtkWidget *widget = GTK_WIDGET(viewer);
    gint pixmap_offset = 0;
    gint max_size = viewer->priv->dimension;
    RsttoNavigatorEntry *entry = rstto_navigator_get_nth_file(viewer->priv->navigator, nr);
    color.pixel = 0xffffffff;
    GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(widget->window));
    GdkGC *gc_bg_normal = gdk_gc_new(GDK_DRAWABLE(widget->window));
    GdkGC *gc_bg_selected = gdk_gc_new(GDK_DRAWABLE(widget->window));

    gdk_gc_set_foreground(gc, &color);
    gdk_gc_set_foreground(gc_bg_selected,
                        &(widget->style->bg[GTK_STATE_SELECTED]));
    gdk_gc_set_foreground(gc_bg_normal,
                        &(widget->style->bg[GTK_STATE_NORMAL]));
    GdkPixmap *pixmap = NULL;
    if (entry)
    {
        GdkPixbuf *pixbuf = rstto_navigator_entry_get_thumb(entry, viewer->priv->dimension - 8);
        pixmap = gdk_pixmap_new(widget->window, viewer->priv->dimension, viewer->priv->dimension, -1);

        gdk_draw_rectangle(GDK_DRAWABLE(pixmap),
                            gc,
                            TRUE,
                            0,
                            0,
                            viewer->priv->dimension,
                            viewer->priv->dimension);
        if(selected)
        {
            gdk_draw_rectangle(GDK_DRAWABLE(pixmap),
                            gc_bg_selected,
                            TRUE,
                            4, 4, 
                            viewer->priv->dimension - 8,
                            viewer->priv->dimension - 8);
        }
        else
        {
            gdk_draw_rectangle(GDK_DRAWABLE(pixmap),
                            gc_bg_normal,
                            TRUE,
                            4, 4, 
                            viewer->priv->dimension - 8,
                            viewer->priv->dimension - 8);
        }

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

        /* pixmap_offset */
        if (nr == viewer->priv->begin)
        {
            pixmap_offset = viewer->priv->offset - (nr * viewer->priv->dimension );
        }
        /* max_size */
        if (nr == viewer->priv->end)
        {
            switch (viewer->priv->orientation)
            {
                case GTK_ORIENTATION_HORIZONTAL:
                    max_size = widget->allocation.width - (32 + ( nr * viewer->priv->dimension) - viewer->priv->offset + pixmap_offset);
                    break;
                case GTK_ORIENTATION_VERTICAL:
                    max_size = widget->allocation.height - (32 + ( nr * viewer->priv->dimension) - viewer->priv->offset + pixmap_offset);
                    break;
            }
            if (max_size <= 0)
                max_size = viewer->priv->dimension;
        }
        switch (viewer->priv->orientation)
        {
            case GTK_ORIENTATION_HORIZONTAL:
                gdk_draw_drawable(GDK_DRAWABLE(widget->window),
                            gc,
                            pixmap,
                            pixmap_offset, 0,
                            16 + ( nr * viewer->priv->dimension) - viewer->priv->offset + pixmap_offset,
                            0,
                            max_size - pixmap_offset,
                            viewer->priv->dimension);

                break;
            case GTK_ORIENTATION_VERTICAL:
                gdk_draw_drawable(GDK_DRAWABLE(widget->window),
                            gc,
                            pixmap,
                            0, pixmap_offset,
                            0,
                            16 + ( nr * viewer->priv->dimension) - viewer->priv->offset + pixmap_offset,
                            viewer->priv->dimension,
                            max_size - pixmap_offset);
                break;
        }
    }
    return TRUE;
}

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

#include "picture_viewer.h"
#include "navigator.h"
#include "thumbnail_viewer.h"

struct _RsttoThumbnailViewerPriv
{
    GtkOrientation  orientation;
    RsttoNavigator *navigator;
    gint dimension;
    gint offset;
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
static void
rstto_thumbnail_viewer_unrealize(GtkWidget *);
static gboolean 
rstto_thumbnail_viewer_expose(GtkWidget *, GdkEventExpose *);

static void
rstto_thumbnail_viewer_paint(RsttoThumbnailViewer *viewer);

static GtkWidgetClass *parent_class = NULL;

static void
cb_rstto_thumbnailer_nav_file_changed(RsttoNavigator *nav, RsttoThumbnailViewer *viewer);
static void
cb_rstto_thumbnailer_button_press_event (RsttoThumbnailViewer *viewer, GdkEventButton *event);

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

    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(viewer), TRUE);
    gtk_widget_set_events (GTK_WIDGET(viewer),
                           GDK_BUTTON_PRESS_MASK);
    g_signal_connect(G_OBJECT(viewer), "button_press_event", G_CALLBACK(cb_rstto_thumbnailer_button_press_event), NULL);
    viewer->priv->orientation = GTK_ORIENTATION_VERTICAL;
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
	widget_class->unrealize = rstto_thumbnail_viewer_unrealize;
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

    switch(viewer->priv->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            viewer->priv->dimension = widget->allocation.height;
            break;
        case GTK_ORIENTATION_VERTICAL:
            viewer->priv->dimension = widget->allocation.width;
            break;
    }


	if (GTK_WIDGET_REALIZED (widget))
	{
 		gdk_window_move_resize (widget->window,
			allocation->x + border_width,
			allocation->y + border_width,
			allocation->width - border_width * 2,
			allocation->height - border_width * 2);

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

static void
rstto_thumbnail_viewer_unrealize(GtkWidget *widget)
{
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

static void
rstto_thumbnail_viewer_paint(RsttoThumbnailViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
	GdkColor color;

    RsttoNavigatorEntry *current_entry = rstto_navigator_get_file(viewer->priv->navigator);

    color.pixel = 0xffffffff;
	GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(widget->window));

	gdk_gc_set_foreground(gc, &color);
    
    gint i;
    switch (viewer->priv->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            for(i = 0; i < rstto_navigator_get_n_files(viewer->priv->navigator); ++i)
            { 
                RsttoNavigatorEntry *entry = rstto_navigator_get_nth_file(viewer->priv->navigator, i);
                GdkPixbuf *pixbuf = rstto_navigator_entry_get_thumbnail(entry);

                gdk_gc_set_foreground(gc, &color);
                gdk_draw_rectangle(GDK_DRAWABLE(widget->window),
                      gc,
                      TRUE,
                      16+(i*viewer->priv->dimension)-viewer->priv->offset, 0, viewer->priv->dimension, viewer->priv->dimension);

                
                if(current_entry == entry)
                    gtk_paint_box(widget->style,
                                 widget->window,
                                 GTK_STATE_SELECTED,
                                 GTK_SHADOW_NONE,
                                 NULL,NULL,NULL,
                                 (i*viewer->priv->dimension)+20-viewer->priv->offset, 4, viewer->priv->dimension - 8, viewer->priv->dimension - 8);
                else
                    gtk_paint_box(widget->style,
                                 widget->window,
                                 GTK_STATE_NORMAL,
                                 GTK_SHADOW_NONE,
                                 NULL,NULL,NULL,
                                 (i*viewer->priv->dimension)+20-viewer->priv->offset, 4, viewer->priv->dimension - 8, viewer->priv->dimension - 8);

                if(pixbuf)
                    gdk_draw_pixbuf(GDK_DRAWABLE(widget->window),
                                gc,
                                pixbuf,
                                0, 0,
                                16 + i * viewer->priv->dimension + (0.5 * (viewer->priv->dimension - gdk_pixbuf_get_width(pixbuf))) - viewer->priv->offset,
                                (0.5 *(viewer->priv->dimension  - gdk_pixbuf_get_height(pixbuf))),
                                -1, -1,
                                GDK_RGB_DITHER_NORMAL,
                                0, 0);
            }
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
                            GTK_STATE_NORMAL,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            GTK_ARROW_RIGHT,
                            TRUE,
                            widget->allocation.width - 16, viewer->priv->dimension / 2 - 7,14,14);
            gdk_window_clear_area(widget->window, 16 + viewer->priv->dimension * (i+1), 0, widget->allocation.width, viewer->priv->dimension);
            break;
        case GTK_ORIENTATION_VERTICAL:
            for(i = 0; i < rstto_navigator_get_n_files(viewer->priv->navigator); ++i)
            { 
                RsttoNavigatorEntry *entry = rstto_navigator_get_nth_file(viewer->priv->navigator, i);
                GdkPixbuf *pixbuf = rstto_navigator_entry_get_thumbnail(entry);

                gdk_gc_set_foreground(gc, &color);
                gdk_draw_rectangle(GDK_DRAWABLE(widget->window),
                      gc,
                      TRUE,
                      0, 16+(i*viewer->priv->dimension)-viewer->priv->offset, viewer->priv->dimension, viewer->priv->dimension);

                
                if(current_entry == entry)
                    gtk_paint_box(widget->style,
                                 widget->window,
                                 GTK_STATE_SELECTED,
                                 GTK_SHADOW_NONE,
                                 NULL,NULL,NULL,
                                 4, (i*viewer->priv->dimension)+20-viewer->priv->offset, viewer->priv->dimension - 8, viewer->priv->dimension - 8);
                else
                    gtk_paint_box(widget->style,
                                 widget->window,
                                 GTK_STATE_NORMAL,
                                 GTK_SHADOW_NONE,
                                 NULL,NULL,NULL,
                                 4, (i*viewer->priv->dimension)+20-viewer->priv->offset, viewer->priv->dimension - 8, viewer->priv->dimension - 8);

                if(pixbuf)
                    gdk_draw_pixbuf(GDK_DRAWABLE(widget->window),
                                gc,
                                pixbuf,
                                0, 0,
                                (0.5 *(viewer->priv->dimension  - gdk_pixbuf_get_width(pixbuf))),
                                16 + i * viewer->priv->dimension + (0.5 * (viewer->priv->dimension - gdk_pixbuf_get_height(pixbuf))) - viewer->priv->offset,
                                -1, -1,
                                GDK_RGB_DITHER_NORMAL,
                                0, 0);
            }
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
                            GTK_STATE_NORMAL,
                            GTK_SHADOW_NONE,
                            NULL,NULL,NULL,
                            GTK_ARROW_DOWN,
                            TRUE,
                            viewer->priv->dimension / 2 - 7, widget->allocation.height - 16,14,14);
            gdk_window_clear_area(widget->window, 0, 16 + viewer->priv->dimension * (i+1), viewer->priv->dimension, widget->allocation.height);
            break;
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

	g_signal_connect(G_OBJECT(navigator), "file_changed", G_CALLBACK(cb_rstto_thumbnailer_nav_file_changed), viewer);

	return (GtkWidget *)viewer;
}

static void
cb_rstto_thumbnailer_nav_file_changed(RsttoNavigator *nav, RsttoThumbnailViewer *viewer)
{
	if (GTK_WIDGET_REALIZED (viewer))
	{
        rstto_thumbnail_viewer_paint(viewer);   

    }
}

static void
cb_rstto_thumbnailer_button_press_event (RsttoThumbnailViewer *viewer,
                                         GdkEventButton *event)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    gint n = 0;
    gint old_offset = viewer->priv->offset;
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
        }
        else
        {
            viewer->priv->offset += viewer->priv->dimension;
            if((rstto_navigator_get_n_files(viewer->priv->navigator) * viewer->priv->dimension - viewer->priv->offset) < viewer->priv->dimension)
            {
                viewer->priv->offset = (rstto_navigator_get_n_files(viewer->priv->navigator) - 1) * viewer->priv->dimension;
            }
        }
        if(old_offset != viewer->priv->offset)
            rstto_thumbnail_viewer_paint(viewer);
    }
    else
    {
        rstto_navigator_set_file(viewer->priv->navigator, n);
    }
}

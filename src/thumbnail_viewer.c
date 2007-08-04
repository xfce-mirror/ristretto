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
            requisition->height = 64;
            requisition->width = 10;
            break;
        case GTK_ORIENTATION_VERTICAL:
            requisition->width = 64;
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

        switch(viewer->priv->orientation)
        {
            case GTK_ORIENTATION_HORIZONTAL:
            case GTK_ORIENTATION_VERTICAL:
                break;
        }


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

    rstto_thumbnail_viewer_paint(viewer);

	return FALSE;
}

static void
rstto_thumbnail_viewer_paint(RsttoThumbnailViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
	GdkColor color;
	GdkColor color_1;
    gint dimension = 0;

    switch(viewer->priv->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            dimension = widget->allocation.height;
            break;
        case GTK_ORIENTATION_VERTICAL:
            dimension = widget->allocation.width;
            break;
    }

    PangoContext *pc = gtk_widget_get_pango_context(widget);
    PangoLayout *pl = pango_layout_new(pc);

    pango_layout_set_text(pl, "Ristretto Image Viewer", 22);
    pango_layout_set_width(pl, (dimension - 24) * PANGO_SCALE);
    pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_MIDDLE);

    color.pixel = 0xffffffff;
    color_1.pixel = 0;
	GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(widget->window));
	GdkGC *gc_1 = gdk_gc_new(GDK_DRAWABLE(widget->window));

	gdk_gc_set_foreground(gc, &color);
	gdk_gc_set_foreground(gc_1, &color_1);

    gint i;
    for(i = 0; i < rstto_navigator_get_n_files(viewer->priv->navigator); ++i)
    { 
        RsttoNavigatorEntry *entry = rstto_navigator_get_nth_file(viewer->priv->navigator, i);
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(entry);
        GdkPixbuf *pixbuf = rstto_navigator_entry_get_thumbnail(entry);

        pango_layout_set_text(pl, info->display_name, strlen(info->display_name));

        if(viewer->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
        {
            gdk_draw_rectangle(GDK_DRAWABLE(widget->window),
                  gc,
                  TRUE,
                  (i*dimension), 0, dimension, dimension);


            /*
            gdk_draw_rectangle(GDK_DRAWABLE(widget->window),
                  gc_1,
                  TRUE,
                  (i*dimension)+12, 12, inner_dimension, inner_dimension-12);
            */
            gdk_draw_pixbuf(GDK_DRAWABLE(widget->window),
                            gc,
                            pixbuf,
                            0, 0,
                            i*dimension + 12,
                            12,
                            -1, -1,
                            GDK_RGB_DITHER_NORMAL,
                            0, 0);

            gdk_draw_layout(GDK_DRAWABLE(widget->window),
                  gc_1,
                  (i*dimension)+12, dimension-24,
                  pl);

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

	return (GtkWidget *)viewer;
}

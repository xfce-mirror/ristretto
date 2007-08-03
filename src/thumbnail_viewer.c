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

#include <pango/pango.h>
#include <pango/pangoxft.h>

#include "thumbnail_viewer.h"

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
rstto_thumbnail_viewer_set_scroll_adjustments(RsttoThumbnailViewer *, GtkAdjustment *, GtkAdjustment *);

static void
cb_rstto_thumbnail_viewer_value_changed(GtkAdjustment *adjustment, RsttoThumbnailViewer *viewer);


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
    viewer->cb_value_changed = cb_rstto_thumbnail_viewer_value_changed;

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

	viewer_class->set_scroll_adjustments = rstto_thumbnail_viewer_set_scroll_adjustments;

	widget_class->realize = rstto_thumbnail_viewer_realize;
	widget_class->unrealize = rstto_thumbnail_viewer_unrealize;
	widget_class->expose_event = rstto_thumbnail_viewer_expose;

	widget_class->size_request = rstto_thumbnail_viewer_size_request;
	widget_class->size_allocate = rstto_thumbnail_viewer_size_allocate;

	object_class->destroy = rstto_thumbnail_viewer_destroy;


	widget_class->set_scroll_adjustments_signal =
	              g_signal_new ("set_scroll_adjustments",
	                            G_TYPE_FROM_CLASS (object_class),
	                            G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
	                            G_STRUCT_OFFSET (RsttoThumbnailViewerClass, set_scroll_adjustments),
	                            NULL, NULL,
	                            gtk_marshal_VOID__POINTER_POINTER,
	                            G_TYPE_NONE, 2,
	                            GTK_TYPE_ADJUSTMENT,
	                            GTK_TYPE_ADJUSTMENT);

}

static void
rstto_thumbnail_viewer_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    RsttoThumbnailViewer *viewer = RSTTO_THUMBNAIL_VIEWER(widget);
    switch(viewer->orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            requisition->height = 96;
            requisition->width = 10;
            break;
        case GTK_ORIENTATION_VERTICAL:
            requisition->width = 120;
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

        if(viewer->hadjustment)
        {
	    	viewer->hadjustment->page_size = widget->allocation.width;
	    	viewer->hadjustment->upper = 128*10;
	    	viewer->hadjustment->lower = 0;
            viewer->hadjustment->step_increment = 1;
            viewer->hadjustment->page_increment = 100;

			if((viewer->hadjustment->value + viewer->hadjustment->page_size) > viewer->hadjustment->upper)
			{
				viewer->hadjustment->value = viewer->hadjustment->upper - viewer->hadjustment->page_size;
				gtk_adjustment_value_changed(viewer->hadjustment);
			}

			gtk_adjustment_changed(viewer->hadjustment);
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
	GdkColor color;
	GdkColor color_1;

    PangoContext *pc = gtk_widget_get_pango_context(widget);
    PangoLayout *pl = pango_layout_new(pc);

    pango_layout_set_text(pl, "Ristretto Image Viewer", 22);
    pango_layout_set_width(pl, 96 * PANGO_SCALE);
    pango_layout_set_ellipsize(pl, PANGO_ELLIPSIZE_MIDDLE);

    color.pixel = 0xffffffff;
    color_1.pixel = 0;
    RsttoThumbnailViewer *viewer = RSTTO_THUMBNAIL_VIEWER(widget);
	GdkGC *gc = gdk_gc_new(GDK_DRAWABLE(widget->window));
	GdkGC *gc_1 = gdk_gc_new(GDK_DRAWABLE(widget->window));

	gdk_gc_set_foreground(gc, &color);
	gdk_gc_set_foreground(gc_1, &color_1);
    if(viewer->hadjustment)
    {
        viewer->hadjustment->page_size = widget->allocation.width;
        viewer->hadjustment->upper = 72*5;
        viewer->hadjustment->lower = 0;
        viewer->hadjustment->step_increment = 1;
        viewer->hadjustment->page_increment = 100;

        if((viewer->hadjustment->value + viewer->hadjustment->page_size) > viewer->hadjustment->upper)
        {
            viewer->hadjustment->value = viewer->hadjustment->upper - viewer->hadjustment->page_size;
            gtk_adjustment_value_changed(viewer->hadjustment);
        }

//        gtk_adjustment_changed(viewer->hadjustment);
    }
    gint i;
    for(i = 0; i < 10; ++i)
    {
        gdk_draw_rectangle(GDK_DRAWABLE(widget->window),
                  gc,
                  TRUE,
                  (i*128), 0, 128, 128);
        gdk_draw_rectangle(GDK_DRAWABLE(widget->window),
                  gc_1,
                  TRUE,
                  (i*128)+12, 12, 96, 72);
        gdk_draw_layout(GDK_DRAWABLE(widget->window),
                  gc_1,
                  (i*128)+12, 96,
                  pl);
    }

	return FALSE;
}

static void
rstto_thumbnail_viewer_destroy(GtkObject *object)
{

}

static void
rstto_thumbnail_viewer_set_scroll_adjustments(RsttoThumbnailViewer *viewer, GtkAdjustment *hadjustment, GtkAdjustment *vadjustment)
{
	if(viewer->hadjustment)
	{
		g_signal_handlers_disconnect_by_func(viewer->hadjustment, viewer->cb_value_changed, viewer);
		g_object_unref(viewer->hadjustment);
	}
	if(viewer->vadjustment)
	{
		g_signal_handlers_disconnect_by_func(viewer->vadjustment, viewer->cb_value_changed, viewer);
		g_object_unref(viewer->vadjustment);
	}

	viewer->hadjustment = hadjustment;
	viewer->vadjustment = vadjustment;

	if(viewer->hadjustment)
	{
		g_signal_connect(G_OBJECT(viewer->hadjustment), "value-changed", (GCallback)viewer->cb_value_changed, viewer);
		g_object_ref(viewer->hadjustment);
	}
	if(viewer->vadjustment)
	{
		g_signal_connect(G_OBJECT(viewer->vadjustment), "value-changed", (GCallback)viewer->cb_value_changed, viewer);
		g_object_ref(viewer->vadjustment);
	}

}

static void
cb_rstto_thumbnail_viewer_value_changed(GtkAdjustment *adjustment, RsttoThumbnailViewer *viewer)
{

}

GtkWidget *
rstto_thumbnail_viewer_new()
{
	GtkWidget *widget;

	widget = g_object_new(RSTTO_TYPE_THUMBNAIL_VIEWER, NULL);

	return widget;
}

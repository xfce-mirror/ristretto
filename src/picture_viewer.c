/*
 *  Copyright (c) 2006 Stephan Arts <stephan@xfce.org>
 *
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

#include "picture_viewer.h"

static void
rstto_picture_viewer_init(RsttoPictureViewer *);
static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *);
static void
rstto_picture_viewer_destroy(GtkObject *object);

static void
rstto_picture_viewer_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_picture_viewer_size_allocate(GtkWidget *, GtkAllocation *);
static void
rstto_picture_viewer_realize(GtkWidget *);
static void
rstto_picture_viewer_unrealize(GtkWidget *);
static gboolean 
rstto_picture_viewer_expose(GtkWidget *, GdkEventExpose *);

static void
rstto_picture_viewer_paint(GtkWidget *widget);

static void
rstto_picture_viewer_set_scroll_adjustments(RsttoPictureViewer *, GtkAdjustment *, GtkAdjustment *);

static void
cb_rstto_picture_viewer_value_changed(GtkAdjustment *adjustment, RsttoPictureViewer *viewer);

static GtkWidgetClass *parent_class = NULL;

GType
rstto_picture_viewer_get_type ()
{
	static GType rstto_picture_viewer_type = 0;

	if (!rstto_picture_viewer_type)
	{
		static const GTypeInfo rstto_picture_viewer_info = 
		{
			sizeof (RsttoPictureViewerClass),
			(GBaseInitFunc) NULL,
			(GBaseFinalizeFunc) NULL,
			(GClassInitFunc) rstto_picture_viewer_class_init,
			(GClassFinalizeFunc) NULL,
			NULL,
			sizeof (RsttoPictureViewer),
			0,
			(GInstanceInitFunc) rstto_picture_viewer_init,
			NULL
		};

		rstto_picture_viewer_type = g_type_register_static (GTK_TYPE_WIDGET, "RsttoPictureViewer", &rstto_picture_viewer_info, 0);
	}
	return rstto_picture_viewer_type;
}

static void
rstto_picture_viewer_init(RsttoPictureViewer *viewer)
{
	//GTK_WIDGET_SET_FLAGS(viewer, GTK_NO_WINDOW);
	viewer->cb_value_changed = cb_rstto_picture_viewer_value_changed;

	viewer->dst_pixbuf = NULL;
	gtk_widget_set_redraw_on_allocate(GTK_WIDGET(viewer), TRUE);

	viewer->scale = 2;

	//viewer->src_pixbuf = gdk_pixbuf_new_from_file("test.svg", NULL);
	viewer->src_pixbuf = gdk_pixbuf_new_from_file("test.png", NULL);
}

static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *viewer_class)
{
	GtkWidgetClass *widget_class;
	GtkObjectClass *object_class;

	widget_class = (GtkWidgetClass*)viewer_class;
	object_class = (GtkObjectClass*)viewer_class;

	parent_class = g_type_class_peek_parent(viewer_class);

	viewer_class->set_scroll_adjustments = rstto_picture_viewer_set_scroll_adjustments;

	widget_class->realize = rstto_picture_viewer_realize;
	widget_class->unrealize = rstto_picture_viewer_unrealize;
	widget_class->expose_event = rstto_picture_viewer_expose;

	widget_class->size_request = rstto_picture_viewer_size_request;
	widget_class->size_allocate = rstto_picture_viewer_size_allocate;

	object_class->destroy = rstto_picture_viewer_destroy;


  widget_class->set_scroll_adjustments_signal =
    g_signal_new ("set_scroll_adjustments",
		  G_TYPE_FROM_CLASS (object_class),
		  G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
		  G_STRUCT_OFFSET (RsttoPictureViewerClass, set_scroll_adjustments),
		  NULL, NULL,
		  gtk_marshal_VOID__POINTER_POINTER,
		  G_TYPE_NONE, 2,
		  GTK_TYPE_ADJUSTMENT,
		  GTK_TYPE_ADJUSTMENT);

}

static void
rstto_picture_viewer_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	requisition->width = 100;
	requisition->height= 500;
}

static void
rstto_picture_viewer_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	RsttoPictureViewer *viewer = RSTTO_PICTURE_VIEWER(widget);
	gint border_width =  0;//GTK_CONTAINER(widget)->border_width;

	gdouble width = (gdouble)gdk_pixbuf_get_width(viewer->src_pixbuf);
	gdouble height = (gdouble)gdk_pixbuf_get_height(viewer->src_pixbuf);

	viewer->hadjustment->page_size = allocation->width;
	viewer->vadjustment->page_size = allocation->height;
	viewer->hadjustment->upper = width * viewer->scale;
	viewer->vadjustment->upper = height * viewer->scale;
	viewer->hadjustment->lower = 0;
	viewer->vadjustment->lower = 0;
	viewer->hadjustment->step_increment = 1;
	viewer->hadjustment->page_increment = 100;
	viewer->vadjustment->step_increment = 1;
	viewer->vadjustment->page_increment = 100;

	if((viewer->vadjustment->value + viewer->vadjustment->page_size) > viewer->vadjustment->upper)
	{
		viewer->vadjustment->value = viewer->vadjustment->upper - viewer->vadjustment->page_size;
		gtk_adjustment_value_changed(viewer->vadjustment);
	}
	if((viewer->hadjustment->value + viewer->hadjustment->page_size) > viewer->hadjustment->upper)
	{
		viewer->hadjustment->value = viewer->hadjustment->upper - viewer->hadjustment->page_size;
		gtk_adjustment_value_changed(viewer->hadjustment);
	}


	GdkPixbuf *tmp_pixbuf = NULL;
	tmp_pixbuf = gdk_pixbuf_new_subpixbuf(viewer->src_pixbuf,
	                         viewer->hadjustment->value / viewer->scale >= 0? viewer->hadjustment->value / viewer->scale : 0,
	                         viewer->vadjustment->value / viewer->scale >= 0? viewer->vadjustment->value / viewer->scale : 0,
													 ((allocation->width/viewer->scale)+1) < width?allocation->width/viewer->scale+1:width,
													 ((allocation->height/viewer->scale)+1)< height?allocation->height/viewer->scale+1:height);

	if(viewer->dst_pixbuf)
	{
		g_object_unref(viewer->dst_pixbuf);
		viewer->dst_pixbuf = NULL;
	}
	viewer->dst_pixbuf = gdk_pixbuf_scale_simple(tmp_pixbuf, gdk_pixbuf_get_width(tmp_pixbuf)*viewer->scale, gdk_pixbuf_get_height(tmp_pixbuf)*viewer->scale, GDK_INTERP_BILINEAR);

	if(tmp_pixbuf)
	{
		g_object_unref(tmp_pixbuf);
		tmp_pixbuf = NULL;
	}


  widget->allocation = *allocation;

	if (GTK_WIDGET_REALIZED (widget))
	{
 		gdk_window_move_resize (widget->window,
			allocation->x + border_width,
			allocation->y + border_width,
			allocation->width - border_width * 2,
			allocation->height - border_width * 2);
	}

  gtk_adjustment_changed(viewer->hadjustment);
  gtk_adjustment_changed(viewer->vadjustment);

}

static void
rstto_picture_viewer_realize(GtkWidget *widget)
{
	g_return_if_fail (widget != NULL);
	g_return_if_fail (RSTTO_IS_PICTURE_VIEWER(widget));

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
rstto_picture_viewer_unrealize(GtkWidget *widget)
{
}

static gboolean
rstto_picture_viewer_expose(GtkWidget *widget, GdkEventExpose *event)
{
	rstto_picture_viewer_paint(widget);

	return FALSE;
}

static void
rstto_picture_viewer_paint(GtkWidget *widget)
{
	GdkPixbuf *pixbuf = RSTTO_PICTURE_VIEWER(widget)->dst_pixbuf;
	/* required for transparent pixbufs... add double buffering to fix flickering*/
	if(GTK_WIDGET_REALIZED(widget))
	{
		gdk_window_clear(widget->window);
		gdk_draw_pixbuf(GDK_DRAWABLE(widget->window), 
										NULL, 
										pixbuf,
										0,
										0,
										(widget->allocation.width-gdk_pixbuf_get_width(pixbuf))<0?0:(widget->allocation.width-gdk_pixbuf_get_width(pixbuf))/2,
										(widget->allocation.height-gdk_pixbuf_get_height(pixbuf))<0?0:(widget->allocation.height-gdk_pixbuf_get_height(pixbuf))/2,
										gdk_pixbuf_get_width(pixbuf),
										gdk_pixbuf_get_height(pixbuf),
										GDK_RGB_DITHER_NONE,
										0,0);
	}
}

static void
rstto_picture_viewer_destroy(GtkObject *object)
{

}

static void
rstto_picture_viewer_set_scroll_adjustments(RsttoPictureViewer *viewer, GtkAdjustment *hadjustment, GtkAdjustment *vadjustment)
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
cb_rstto_picture_viewer_value_changed(GtkAdjustment *adjustment, RsttoPictureViewer *viewer)
{
	gdouble width = (gdouble)gdk_pixbuf_get_width(viewer->src_pixbuf);
	gdouble height = (gdouble)gdk_pixbuf_get_height(viewer->src_pixbuf);

	GdkPixbuf *tmp_pixbuf = NULL;
	tmp_pixbuf = gdk_pixbuf_new_subpixbuf(viewer->src_pixbuf,
	                         viewer->hadjustment->value / viewer->scale >= 0? viewer->hadjustment->value / viewer->scale : 0,
	                         viewer->vadjustment->value / viewer->scale >= 0? viewer->vadjustment->value / viewer->scale : 0,
													 ((GTK_WIDGET(viewer)->allocation.width/viewer->scale)) < width?GTK_WIDGET(viewer)->allocation.width/viewer->scale:width,
													 ((GTK_WIDGET(viewer)->allocation.height/viewer->scale)) < height?GTK_WIDGET(viewer)->allocation.height/viewer->scale:height);

	if(viewer->dst_pixbuf)
	{
		g_object_unref(viewer->dst_pixbuf);
		viewer->dst_pixbuf = NULL;
	}
	viewer->dst_pixbuf = gdk_pixbuf_scale_simple(tmp_pixbuf, gdk_pixbuf_get_width(tmp_pixbuf)*viewer->scale, gdk_pixbuf_get_height(tmp_pixbuf)*viewer->scale, GDK_INTERP_BILINEAR);

	if(tmp_pixbuf)
	{
		g_object_unref(tmp_pixbuf);
		tmp_pixbuf = NULL;
	}
	rstto_picture_viewer_paint((GtkWidget *)viewer);
}

GtkWidget *
rstto_picture_viewer_new()
{
	GtkWidget *widget;

	widget = g_object_new(RSTTO_TYPE_PICTURE_VIEWER, NULL);

	return widget;
}

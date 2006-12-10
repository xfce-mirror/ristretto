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

#include "picture_viewer.h"

static void
rstto_picture_viewer_init(RsttoPictureViewer *);
static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *);

static void
rstto_picture_viewer_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_picture_viewer_size_allocate(GtkWidget *, GtkAllocation *);
static void
rstto_picture_viewer_realize(GtkWidget *);
static gboolean 
rstto_picture_viewer_expose(GtkWidget *, GdkEventExpose *);

static void
rstto_picture_viewer_forall(GtkContainer *, gboolean, GtkCallback, gpointer);
static void
rstto_picture_viewer_add(GtkContainer *, GtkWidget *);
static void
rstto_picture_viewer_remove(GtkContainer *, GtkWidget *);

static GtkContainerClass *rstto_parent_class = NULL;

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

		rstto_picture_viewer_type = g_type_register_static (GTK_TYPE_CONTAINER, "RsttoPictureViewer", &rstto_picture_viewer_info, 0);
	}
	return rstto_picture_viewer_type;
}

static void
rstto_picture_viewer_init(RsttoPictureViewer *viewer)
{
	GTK_WIDGET_SET_FLAGS(viewer, GTK_NO_WINDOW);
	gtk_widget_set_redraw_on_allocate(GTK_WIDGET(viewer), TRUE);

	viewer->scale = 100;

	viewer->src_pixbuf = gdk_pixbuf_new_from_file("test.png", NULL);
	gint width = gdk_pixbuf_get_width(viewer->src_pixbuf);
	gint height = gdk_pixbuf_get_height(viewer->src_pixbuf);

	GtkAdjustment *h_adjustment = (GtkAdjustment *)gtk_adjustment_new(0, 0, width, width*0.1, width*0.9, 0);
	GtkAdjustment *v_adjustment = (GtkAdjustment *)gtk_adjustment_new(0, 0, height, height*0.1, height*0.9, 0);

	viewer->h_scrollbar = (GtkHScrollbar *)gtk_hscrollbar_new(h_adjustment);
	viewer->v_scrollbar = (GtkVScrollbar *)gtk_vscrollbar_new(v_adjustment);

	viewer->dst_pixbuf = NULL;

	gtk_container_add((GtkContainer *)viewer, (GtkWidget *)viewer->h_scrollbar);
	gtk_container_add((GtkContainer *)viewer, (GtkWidget *)viewer->v_scrollbar);

	gtk_widget_show((GtkWidget *)viewer->h_scrollbar);
	gtk_widget_show((GtkWidget *)viewer->v_scrollbar);
}

static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *viewer_class)
{
	GtkWidgetClass *widget_class;
	GtkContainerClass *container_class;

	widget_class = (GtkWidgetClass*)viewer_class;
	container_class = (GtkContainerClass*)viewer_class;

	widget_class->realize = rstto_picture_viewer_realize;
	widget_class->expose_event = rstto_picture_viewer_expose;
	widget_class->size_request = rstto_picture_viewer_size_request;
	widget_class->size_allocate = rstto_picture_viewer_size_allocate;

	container_class->add = rstto_picture_viewer_add;
	container_class->remove = rstto_picture_viewer_remove;
	container_class->forall = rstto_picture_viewer_forall;

	rstto_parent_class = container_class;
}

static void
rstto_picture_viewer_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	g_debug("size request");
}

static void
rstto_picture_viewer_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	GtkAllocation child_allocation;
	g_debug("size allocate: x:%d y:%d w:%d h:%d", allocation->x, allocation->y, allocation->width, allocation->height);
	RsttoPictureViewer *viewer = RSTTO_PICTURE_VIEWER(widget);
	gtk_widget_set_child_visible(GTK_WIDGET(viewer->h_scrollbar), TRUE);
	gtk_widget_set_child_visible(GTK_WIDGET(viewer->v_scrollbar), TRUE);

	GtkAdjustment *h_adjustment = gtk_range_get_adjustment(GTK_RANGE(viewer->h_scrollbar));
	h_adjustment->page_size = allocation->width-20;
	if((h_adjustment->value + h_adjustment->page_size) > h_adjustment->upper)
		h_adjustment->value = MAX(h_adjustment->upper - h_adjustment->page_size, 0);
	gtk_range_set_adjustment(GTK_RANGE(viewer->h_scrollbar), h_adjustment);

	GtkAdjustment *v_adjustment = gtk_range_get_adjustment(GTK_RANGE(viewer->v_scrollbar));
	v_adjustment->page_size = allocation->height-20;
	if((v_adjustment->value + v_adjustment->page_size) > v_adjustment->upper)
		v_adjustment->value = MAX(v_adjustment->upper - v_adjustment->page_size, 0);
	gtk_range_set_adjustment(GTK_RANGE(viewer->v_scrollbar), v_adjustment);

	GdkPixbuf *tmp = gdk_pixbuf_new_subpixbuf(viewer->src_pixbuf, 
			h_adjustment->value*100/viewer->scale,
			v_adjustment->value*100/viewer->scale,
			MIN(h_adjustment->page_size*100/viewer->scale, gdk_pixbuf_get_width(viewer->src_pixbuf)*100/viewer->scale),
			MIN(v_adjustment->page_size*100/viewer->scale, gdk_pixbuf_get_height(viewer->src_pixbuf)*100/viewer->scale));

	viewer->dst_pixbuf = gdk_pixbuf_scale_simple(tmp, h_adjustment->page_size, v_adjustment->page_size, GDK_INTERP_BILINEAR);

	child_allocation.x = allocation->width-20;
	child_allocation.y = allocation->y;
	child_allocation.width=20;
	child_allocation.height = allocation->height-20;
	gtk_widget_size_allocate(GTK_WIDGET(viewer->v_scrollbar), &child_allocation);

	child_allocation.x = allocation->x;
	child_allocation.y = allocation->height-20;
	child_allocation.width= allocation->width-20;
	child_allocation.height=20;
	gtk_widget_size_allocate(GTK_WIDGET(viewer->h_scrollbar), &child_allocation);
}

static void
rstto_picture_viewer_realize(GtkWidget *widget)
{
	g_debug("realize");
	g_return_if_fail (widget != NULL);
	g_return_if_fail (RSTTO_IS_PICTURE_VIEWER(widget));

	GdkWindowAttr attributes;
	gint attributes_mask;

	GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

	attributes.x = widget->allocation.x;
	attributes.y = widget->allocation.y;
	attributes.width = widget->allocation.width-20;
	attributes.height = widget->allocation.height-20;
	attributes.wclass = GDK_INPUT_OUTPUT;
	attributes.window_type = GDK_WINDOW_CHILD;
	attributes.event_mask = gtk_widget_get_events (widget) | 
	GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | 
	GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
	GDK_POINTER_MOTION_HINT_MASK;
	attributes.visual = gtk_widget_get_visual (widget);
	attributes.colormap = gtk_widget_get_colormap (widget);

	attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
	widget->window = gdk_window_new (widget->parent->window, &attributes, attributes_mask);

  widget->style = gtk_style_attach (widget->style, widget->window);
}

static gboolean
rstto_picture_viewer_expose(GtkWidget *widget, GdkEventExpose *event)
{
	g_debug("expose");
  gdk_window_clear_area (widget->window, 0, 0, widget->allocation.width-20, widget->allocation.height-20);
  (* GTK_WIDGET_CLASS (rstto_parent_class)->expose_event) (widget, event);

	return FALSE;
}

static void
rstto_picture_viewer_forall(GtkContainer *container, gboolean include_internals, GtkCallback callback, gpointer callback_data)
{
	RsttoPictureViewer *viewer = RSTTO_PICTURE_VIEWER(container);

	(*callback)(GTK_WIDGET(viewer->v_scrollbar), callback_data);
	(*callback)(GTK_WIDGET(viewer->h_scrollbar), callback_data);
}

static void
rstto_picture_viewer_add(GtkContainer *container, GtkWidget *child)
{
	g_return_if_fail(GTK_IS_WIDGET(child));

	RsttoPictureViewer *viewer = RSTTO_PICTURE_VIEWER(container);

	gtk_widget_set_parent(child, GTK_WIDGET(viewer));
}

static void
rstto_picture_viewer_remove(GtkContainer *container, GtkWidget *child)
{

}

GtkWidget *
rstto_picture_viewer_new()
{
	GtkWidget *widget;

	widget = g_object_new(RSTTO_TYPE_PICTURE_VIEWER, NULL);

	return widget;
}

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

/**TODO:
 *
 * Fix viewport-dimension > picture-dimension ratio
 */

#include <config.h>
#include <gtk/gtk.h>

#include "picture_viewer.h"

static void
rstto_picture_viewer_init(RsttoPictureViewer *);
static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *);

static void
rstto_picture_viewer_size_alloc(GtkWidget *widget, GtkAllocation*);

static GtkViewportClass *rstto_picture_viewer_parent_class = NULL;

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

		rstto_picture_viewer_type = g_type_register_static (GTK_TYPE_VIEWPORT, "RsttoPictureViewer", &rstto_picture_viewer_info, 0);
	}
	return rstto_picture_viewer_type;
}

static void
rstto_picture_viewer_init(RsttoPictureViewer *viewer)
{
	viewer->scale = 100;

	viewer->image = gtk_image_new();

	gtk_container_add(GTK_CONTAINER(viewer), viewer->image);

	gtk_widget_show(viewer->image);
}

static void
rstto_picture_viewer_class_init(RsttoPictureViewerClass *viewer_class)
{
	GtkWidgetClass *object_class = GTK_WIDGET_CLASS(viewer_class);

	rstto_picture_viewer_parent_class = &viewer_class->parent_class;

	object_class->size_allocate = rstto_picture_viewer_size_alloc;
}

static void
rstto_picture_viewer_size_alloc(GtkWidget *widget, GtkAllocation *alloc)
{
	if(RSTTO_PICTURE_VIEWER(widget)->src_pixbuf)
	{
		RsttoPictureViewer *viewer = RSTTO_PICTURE_VIEWER(widget);
		GtkViewport *viewport = GTK_VIEWPORT(widget);

		viewport->hadjustment->page_size = alloc->width;
		viewport->vadjustment->page_size = alloc->height;
		viewport->hadjustment->upper = gdk_pixbuf_get_width(viewer->src_pixbuf)*viewer->scale/100;
		viewport->vadjustment->upper = gdk_pixbuf_get_height(viewer->src_pixbuf)*viewer->scale/100;
		viewport->hadjustment->lower = 0;
		viewport->vadjustment->lower = 0;

		if((viewport->vadjustment->value + viewport->vadjustment->page_size) > viewport->vadjustment->upper)
		{
			viewport->vadjustment->value = viewport->vadjustment->upper - viewport->vadjustment->page_size;
			gtk_adjustment_value_changed(viewport->vadjustment);
		}

		if((viewport->hadjustment->value + viewport->hadjustment->page_size) > viewport->hadjustment->upper)
		{
			viewport->hadjustment->value = viewport->hadjustment->upper - viewport->hadjustment->page_size;
			gtk_adjustment_value_changed(viewport->hadjustment);
		}
		gtk_adjustment_changed(viewport->hadjustment);
		gtk_adjustment_changed(viewport->vadjustment);
	}
	gtk_widget_size_allocate(GTK_BIN(widget)->child, alloc);
}


void
rstto_picture_viewer_set_pixbuf(RsttoPictureViewer *viewer, GdkPixbuf *pixbuf)
{
	viewer->src_pixbuf = pixbuf;

	gtk_image_set_from_pixbuf(GTK_IMAGE(viewer->image), pixbuf);

	gtk_widget_queue_resize(GTK_WIDGET(viewer));
}


GtkWidget *
rstto_picture_viewer_new()
{
	GtkWidget *widget;

	widget = g_object_new(RSTTO_TYPE_PICTURE_VIEWER, "hadjustment", NULL, "vadjustment", NULL, NULL);

	return widget;
}

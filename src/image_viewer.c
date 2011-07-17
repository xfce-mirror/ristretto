/*
 *  Copyright (c) Stephan Arts 2006-2011 <stephan@xfce.org>
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
#include <string.h>
#include <gio/gio.h>
#include <libexif/exif-data.h>

#include "image.h"
#include "image_list.h"

#include "image_viewer.h"
#include "settings.h"
#include "main_window.h"
#include "marshal.h"

#ifndef RSTTO_IMAGE_VIEWER_BUFFER_SIZE
/* #define RSTTO_IMAGE_VIEWER_BUFFER_SIZE 1024 */
#define RSTTO_IMAGE_VIEWER_BUFFER_SIZE 131072
#endif

typedef struct _RsttoImageViewerTransaction RsttoImageViewerTransaction;

struct _RsttoImageViewerPriv
{
    GFile                       *file;
    RsttoSettings               *settings;

    RsttoImageViewerTransaction *transaction;

    /* Animation data for animated images (like .gif/.mng) */
    /*******************************************************/
    GdkPixbufAnimation     *animation;
    GdkPixbufAnimationIter *iter;
    gint                    animation_timeout_id;

    /* CALLBACKS */
    /*************/
    void (*cb_value_changed)(GtkAdjustment *, RsttoImageViewer *);
};


struct _RsttoImageViewerTransaction
{
    RsttoImageViewer *viewer;
    GFile            *file;
    GCancellable     *cancellable;
    GdkPixbufLoader  *loader;

    /* File I/O data */
    /*****************/
    guchar           *buffer;
};


static void
rstto_image_viewer_init(RsttoImageViewer *);
static void
rstto_image_viewer_class_init(RsttoImageViewerClass *);
static void
rstto_image_viewer_destroy(GtkObject *object);

static void
rstto_image_viewer_size_request(GtkWidget *, GtkRequisition *);
static void
rstto_image_viewer_size_allocate(GtkWidget *, GtkAllocation *);
static void
rstto_image_viewer_realize(GtkWidget *);
static gboolean 
rstto_image_viewer_expose(GtkWidget *, GdkEventExpose *);
static void
rstto_image_viewer_paint (GtkWidget *widget);

static gboolean
rstto_image_viewer_set_scroll_adjustments(RsttoImageViewer *, GtkAdjustment *, GtkAdjustment *);

static void
cb_rstto_image_viewer_value_changed(GtkAdjustment *adjustment, RsttoImageViewer *viewer);
static void
cb_rstto_image_viewer_read_file_ready (GObject *source_object, GAsyncResult *result, gpointer user_data);
static void
cb_rstto_image_viewer_read_input_stream_ready (GObject *source_object, GAsyncResult *result, gpointer user_data);
static void
cb_rstto_image_loader_area_prepared (GdkPixbufLoader *, RsttoImageViewerTransaction *);
static void
cb_rstto_image_loader_size_prepared (GdkPixbufLoader *, gint , gint , RsttoImageViewerTransaction *);
static void
cb_rstto_image_loader_closed (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction);

static void
rstto_image_viewer_load_image (RsttoImageViewer *viewer, GFile *file);
static void
rstto_image_viewer_transaction_free (RsttoImageViewerTransaction *tr);

static GtkWidgetClass *parent_class = NULL;
static GdkScreen      *default_screen = NULL;

GType
rstto_image_viewer_get_type (void)
{
    static GType rstto_image_viewer_type = 0;

    if (!rstto_image_viewer_type)
    {
        static const GTypeInfo rstto_image_viewer_info = 
        {
            sizeof (RsttoImageViewerClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_image_viewer_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoImageViewer),
            0,
            (GInstanceInitFunc) rstto_image_viewer_init,
            NULL
        };

        rstto_image_viewer_type = g_type_register_static (GTK_TYPE_WIDGET, "RsttoImageViewer", &rstto_image_viewer_info, 0);
    }
    return rstto_image_viewer_type;
}

static void
rstto_image_viewer_init(RsttoImageViewer *viewer)
{
    if (default_screen == NULL)
    {
        default_screen = gdk_screen_get_default();
    }

    viewer->priv = g_new0(RsttoImageViewerPriv, 1);
    viewer->priv->cb_value_changed = cb_rstto_image_viewer_value_changed;
    viewer->priv->settings = rstto_settings_new();

    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(viewer), TRUE);
    gtk_widget_set_events (GTK_WIDGET(viewer),
                           GDK_BUTTON_PRESS_MASK |
                           GDK_BUTTON_RELEASE_MASK |
                           GDK_BUTTON1_MOTION_MASK |
                           GDK_ENTER_NOTIFY_MASK |
                           GDK_POINTER_MOTION_MASK);

    /*
    gtk_drag_dest_set(GTK_WIDGET(viewer), 0, drop_targets, G_N_ELEMENTS(drop_targets),
                      GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE | GDK_ACTION_PRIVATE);
    */
}

/**
 * rstto_image_viewer_class_init:
 * @viewer_class:
 *
 * Initialize imageviewer class
 */
static void
rstto_image_viewer_class_init(RsttoImageViewerClass *viewer_class)
{
    GtkWidgetClass *widget_class;
    GtkObjectClass *object_class;

    widget_class = (GtkWidgetClass*)viewer_class;
    object_class = (GtkObjectClass*)viewer_class;

    parent_class = g_type_class_peek_parent(viewer_class);

    viewer_class->set_scroll_adjustments = rstto_image_viewer_set_scroll_adjustments;

    widget_class->realize = rstto_image_viewer_realize;
    widget_class->expose_event = rstto_image_viewer_expose;
    widget_class->size_request = rstto_image_viewer_size_request;
    widget_class->size_allocate = rstto_image_viewer_size_allocate;

    object_class->destroy = rstto_image_viewer_destroy;

    widget_class->set_scroll_adjustments_signal =
                  g_signal_new ("set_scroll_adjustments",
                                G_TYPE_FROM_CLASS (object_class),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_STRUCT_OFFSET (RsttoImageViewerClass, set_scroll_adjustments),
                                NULL, NULL,
                                rstto_marshal_VOID__OBJECT_OBJECT,
                                G_TYPE_NONE, 2,
                                GTK_TYPE_ADJUSTMENT,
                                GTK_TYPE_ADJUSTMENT);
}

/**
 * rstto_image_viewer_realize:
 * @widget:
 *
 */
static void
rstto_image_viewer_realize(GtkWidget *widget)
{
    GdkWindowAttr attributes;
    gint attributes_mask;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (RSTTO_IS_IMAGE_VIEWER(widget));

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

/**
 * rstto_image_viewer_size_request:
 * @widget:
 * @requisition:
 *
 * Request a default size of 300 by 400 pixels
 */
static void
rstto_image_viewer_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
    requisition->width = 400;
    requisition->height= 300;
}


/**
 * rstto_image_viewer_size_allocate:
 * @widget:
 * @allocation:
 *
 *
 */
static void
rstto_image_viewer_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER(widget);
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

    /** 
     * TODO: Check if we really nead a refresh
     */
    //rstto_image_viewer_queued_repaint (viewer, TRUE);
}

/**
 * rstto_image_viewer_expose:
 * @widget:
 * @event:
 *
 */
static gboolean
rstto_image_viewer_expose(GtkWidget *widget, GdkEventExpose *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    /** 
     * TODO: Check if we really nead a refresh
     */
    //rstto_image_viewer_queued_repaint (viewer, TRUE);
    return FALSE;
}

static void
rstto_image_viewer_destroy(GtkObject *object)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER(object);

    if (viewer->priv->settings)
    {
        g_object_unref (viewer->priv->settings);
        viewer->priv->settings = NULL;
    }
}

static gboolean  
rstto_image_viewer_set_scroll_adjustments(RsttoImageViewer *viewer, GtkAdjustment *hadjustment, GtkAdjustment *vadjustment)
{
    if(viewer->hadjustment)
    {
        g_signal_handlers_disconnect_by_func(viewer->hadjustment, viewer->priv->cb_value_changed, viewer);
        g_object_unref(viewer->hadjustment);
    }
    if(viewer->vadjustment)
    {
        g_signal_handlers_disconnect_by_func(viewer->vadjustment, viewer->priv->cb_value_changed, viewer);
        g_object_unref(viewer->vadjustment);
    }

    viewer->hadjustment = hadjustment;
    viewer->vadjustment = vadjustment;

    if(viewer->hadjustment)
    {
        g_signal_connect(G_OBJECT(viewer->hadjustment), "value-changed", (GCallback)viewer->priv->cb_value_changed, viewer);
        g_object_ref(viewer->hadjustment);
    }
    if(viewer->vadjustment)
    {
        g_signal_connect(G_OBJECT(viewer->vadjustment), "value-changed", (GCallback)viewer->priv->cb_value_changed, viewer);
        g_object_ref(viewer->vadjustment);
    }
    return TRUE;
}

static void
rstto_image_viewer_paint (GtkWidget *widget)
{
}


GtkWidget *
rstto_image_viewer_new (void)
{
    GtkWidget *widget;

    widget = g_object_new(RSTTO_TYPE_IMAGE_VIEWER, NULL);

    return widget;
}

/*
 * rstto_image_viewer_set_file:
 * @viewer:
 * @file:
 *
 * Set the file that should be displayed in the image_viewer.
 * 
 * TODO:
 *  - cancellable...
 */
void
rstto_image_viewer_set_file (RsttoImageViewer *viewer, GFile *file)
{
    g_debug("%s", __FUNCTION__);

    /*
     * Check if a file is set, or unset.
     */
    if (file)
    {
        /*
         * Should the 'old' file be cleaned up?
         */
        if (viewer->priv->file)
        {
            /*
             * If the old, and new file are equal, do nothing.
             */
            if (!g_file_equal (viewer->priv->file, file))
            {
                /*
                 * This will first need to return to the 'main' loop before it cleans up after itself.
                 * We can forget about the transaction, once it's cancelled, it will clean-up itself. -- (it should)
                 */
                if (viewer->priv->transaction)
                {
                    g_cancellable_cancel (viewer->priv->transaction->cancellable);
                    viewer->priv->transaction = NULL;
                } 

                g_object_unref (viewer->priv->file);
                viewer->priv->file = g_file_dup(file);
                rstto_image_viewer_load_image (viewer, viewer->priv->file);
            }
        }
        else
        {
            viewer->priv->file = g_file_dup(file);
            rstto_image_viewer_load_image (viewer, viewer->priv->file);
        }
    } 
    else
    {
        if (viewer->priv->file)
        {
            g_object_unref (viewer->priv->file);
            viewer->priv->file = NULL;
        }
    }
}

static void
rstto_image_viewer_load_image (RsttoImageViewer *viewer, GFile *file)
{

    /*
     * This will first need to return to the 'main' loop before it cleans up after itself.
     * We can forget about the transaction, once it's cancelled, it will clean-up itself. -- (it should)
     */
    if (viewer->priv->transaction)
    {
        g_cancellable_cancel (viewer->priv->transaction->cancellable);
        viewer->priv->transaction = NULL;
    }

    RsttoImageViewerTransaction *transaction = g_new0 (RsttoImageViewerTransaction, 1);

    transaction->loader = gdk_pixbuf_loader_new();
    transaction->cancellable = g_cancellable_new();
    transaction->buffer = g_new0 (guchar, RSTTO_IMAGE_VIEWER_BUFFER_SIZE);
    transaction->file = file;
    transaction->viewer = viewer;

    g_signal_connect(transaction->loader, "closed", G_CALLBACK(cb_rstto_image_loader_closed), transaction);

    viewer->priv->transaction = transaction;

    g_file_read_async (transaction->file,
                       0,
                       transaction->cancellable,
                       (GAsyncReadyCallback)cb_rstto_image_viewer_read_file_ready,
                       transaction);
}

static void
rstto_image_viewer_transaction_free (RsttoImageViewerTransaction *tr)
{
    g_debug("%s", __FUNCTION__);
    /*
     * Check if this transaction is current,
     * if so, remove the reference from the viewer.
     */
    if (tr->viewer->priv->transaction == tr)
    {
        tr->viewer->priv->transaction = NULL;
    }
    g_object_unref (tr->cancellable);
    g_object_unref (tr->loader);
    g_object_unref (tr->file);
    g_free (tr->buffer);
    g_free (tr);
}


/************************/
/** CALLBACK FUNCTIONS **/
/************************/

static void
cb_rstto_image_viewer_value_changed(GtkAdjustment *adjustment, RsttoImageViewer *viewer)
{
}

static void
cb_rstto_image_viewer_read_file_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GFile *file = G_FILE (source_object);
    RsttoImageViewerTransaction *transaction = (RsttoImageViewerTransaction *)user_data;

    GFileInputStream *file_input_stream = g_file_read_finish (file, result, NULL);

    /* TODO: FIXME -- Clean up the transaction*/
    if (file_input_stream == NULL)
    {
        rstto_image_viewer_transaction_free (transaction);
        return;
    }

    g_input_stream_read_async (G_INPUT_STREAM (file_input_stream),
                               transaction->buffer,
                               RSTTO_IMAGE_VIEWER_BUFFER_SIZE,
                               G_PRIORITY_DEFAULT,
                               transaction->cancellable,
                               (GAsyncReadyCallback) cb_rstto_image_viewer_read_input_stream_ready,
                               transaction);
}

static void
cb_rstto_image_viewer_read_input_stream_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    g_debug("%s", __FUNCTION__);
    GError *error = NULL;
    RsttoImageViewerTransaction *transaction = (RsttoImageViewerTransaction *)user_data;
    gssize read_bytes = g_input_stream_read_finish (G_INPUT_STREAM (source_object), result, &error);

    /* TODO: FIXME -- Clean up the transaction*/
    if (read_bytes == -1)
    {
        rstto_image_viewer_transaction_free (transaction);
        return;
    }

    if (read_bytes > 0)
    {
        if(gdk_pixbuf_loader_write (transaction->loader, (const guchar *)transaction->buffer, read_bytes, &error) == FALSE)
        {
            g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);

            rstto_image_viewer_transaction_free (transaction);
        }
        else
        {
            g_input_stream_read_async (G_INPUT_STREAM (source_object),
                                       transaction->buffer,
                                       RSTTO_IMAGE_VIEWER_BUFFER_SIZE,
                                       G_PRIORITY_DEFAULT,
                                       transaction->cancellable,
                                       (GAsyncReadyCallback) cb_rstto_image_viewer_read_input_stream_ready,
                                       transaction);
        }
    }
    else {
        /* Loading complete, transaction should not be free-ed */
        g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
        gdk_pixbuf_loader_close (transaction->loader, NULL);

        if (g_cancellable_is_cancelled(transaction->cancellable))
        {
            rstto_image_viewer_transaction_free (transaction);
        }
    }
}

static void
cb_rstto_image_loader_closed (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction)
{
    g_debug("%s", __FUNCTION__);

}

static void
cb_rstto_image_loader_area_prepared (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction)
{

}

static void
cb_rstto_image_loader_size_prepared (GdkPixbufLoader *loader, gint width, gint height, RsttoImageViewerTransaction *transaction)
{
    gint s_width = gdk_screen_get_width (default_screen);
    gint s_height = gdk_screen_get_height (default_screen);

    /*
     * Set the maximum size of the loaded image to the screen-size.
     * TODO: Add some 'smart-stuff' here
     */
    if (s_width < width || s_height < height)
    {
        gdk_pixbuf_loader_set_size (loader, s_width, s_height); 
    }
}


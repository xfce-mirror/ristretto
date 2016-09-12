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

#include <config.h>
#include <gtk/gtk.h>
#include <string.h>
#include <gio/gio.h>
#include <libxfce4ui/libxfce4ui.h>
#include <libexif/exif-data.h>

#include <math.h>

#include "util.h"

#include "file.h"
#include "image_viewer.h"
#include "settings.h"
#include "marshal.h"

/* Do not make this buffer too large,
 * this breaks some pixbufloaders.
 */
#ifndef RSTTO_IMAGE_VIEWER_BUFFER_SIZE
#define RSTTO_IMAGE_VIEWER_BUFFER_SIZE 4096
#endif

#ifndef BACKGROUND_ICON_NAME
#define BACKGROUND_ICON_NAME "ristretto"
#endif

#ifndef BACKGROUND_ICON_SIZE
#define BACKGROUND_ICON_SIZE 256
#endif

#ifndef RSTTO_MAX_SCALE
#define RSTTO_MAX_SCALE 4.0
#endif

enum
{
    PROP_0,
    PROP_SHOW_CLOCK
};

typedef enum
{
    RSTTO_IMAGE_VIEWER_MOTION_STATE_NORMAL = 0,
    RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM,
    RSTTO_IMAGE_VIEWER_MOTION_STATE_MOVE
} RsttoImageViewerMotionState;

typedef struct _RsttoImageViewerTransaction RsttoImageViewerTransaction;

struct _RsttoImageViewerPriv
{
    RsttoFile                   *file;
    RsttoSettings               *settings;
    GdkVisual                   *visual;
    GdkColormap                 *colormap;

    GtkIconTheme                *icon_theme;
    GdkPixbuf                   *missing_icon;
    GdkPixbuf                   *bg_icon;
    GdkColor                    *bg_color;
    GdkColor                    *bg_color_fs;

    gboolean                     limit_quality;

    GError                      *error;

    RsttoImageViewerTransaction *transaction;
    GdkPixbuf                   *pixbuf;
    RsttoImageOrientation        orientation;
    struct
    {
        gdouble x_offset;
        gdouble y_offset;
        gdouble width;
        gdouble height;
    } rendering;

    struct
    {
        gboolean show_clock;
    } props;

    GtkMenu                     *menu;
    gboolean                     invert_zoom_direction;

    /* Animation data for animated images (like .gif/.mng) */
    /*******************************************************/
    GdkPixbufAnimation     *animation;
    GdkPixbufAnimationIter *iter;
    gint                    animation_timeout_id;

    gint                    refresh_timeout_id;

    gdouble                 scale;
    gboolean                auto_scale;

    gdouble                 image_scale;
    gint                    image_width;
    gint                    image_height;

    struct
    {
        gdouble x;
        gdouble y;
        gdouble current_x;
        gdouble current_y;
        gint h_val;
        gint v_val;
        RsttoImageViewerMotionState state;
    } motion;

    /* CALLBACKS */
    /*************/
    void (*cb_value_changed)(GtkAdjustment *, RsttoImageViewer *);
};


struct _RsttoImageViewerTransaction
{
    RsttoImageViewer *viewer;
    RsttoFile        *file;
    GCancellable     *cancellable;
    GdkPixbufLoader  *loader;

    GError           *error;

    gint              image_width;
    gint              image_height;
    gdouble           image_scale;
    gdouble           scale;
    RsttoImageOrientation orientation;

    /* File I/O data */
    /*****************/
    guchar           *buffer;
};

static void
set_scale (
        RsttoImageViewer *viewer,
        gdouble scale );
static void
paint_background (
        GtkWidget *widget,
        cairo_t *ctx );
static void
paint_background_icon (
        GtkWidget *widget,
        cairo_t *ctx );


static void
rstto_image_viewer_init (GObject *);
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
rstto_image_viewer_paint (GtkWidget *widget, cairo_t *);

static void
rstto_image_viewer_set_property (
        GObject      *object,
        guint         property_id,
        const GValue *value,
        GParamSpec   *pspec);
static void
rstto_image_viewer_get_property (
        GObject    *object,
        guint       property_id,
        GValue     *value,
        GParamSpec *pspec);

static void
cb_rstto_image_viewer_file_changed (
        RsttoFile        *r_file,
        RsttoImageViewer *viewer );

static void
rstto_image_viewer_set_motion_state (RsttoImageViewer *viewer, RsttoImageViewerMotionState state);

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
static gboolean
cb_rstto_image_viewer_update_pixbuf (RsttoImageViewer *viewer);
static void
cb_rstto_image_viewer_dnd (GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data,
                           guint info, guint time_, RsttoImageViewer *viewer);

static gboolean
rstto_scroll_event (
        GtkWidget *widget,
        GdkEventScroll *event);

static gboolean 
rstto_motion_notify_event (
        GtkWidget *widget,
        GdkEventMotion *event);

static gboolean
rstto_button_press_event (
        GtkWidget *widget,
        GdkEventButton *event);

static gboolean
rstto_button_release_event (
        GtkWidget *widget,
        GdkEventButton *event);

static gboolean
rstto_popup_menu (
        GtkWidget *widget);

static void
cb_rstto_limit_quality_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data);
static void
cb_rstto_bgcolor_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data);
static void
cb_rstto_zoom_direction_changed (
        GObject *settings,
        GParamSpec *pspec,
        gpointer user_data);

static void
rstto_image_viewer_load_image (
        RsttoImageViewer *viewer,
        RsttoFile *file,
        gdouble scale);
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
rstto_image_viewer_init ( GObject *object )
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (object);

    if (default_screen == NULL)
    {
        default_screen = gdk_screen_get_default();
    }

    viewer->priv = g_new0(RsttoImageViewerPriv, 1);
    viewer->priv->cb_value_changed = cb_rstto_image_viewer_value_changed;
    viewer->priv->settings = rstto_settings_new();
    viewer->priv->image_width = 0;
    viewer->priv->image_height = 0;
    viewer->priv->visual = gdk_rgb_get_visual();
    viewer->priv->colormap = gdk_colormap_new (viewer->priv->visual, TRUE);

    viewer->priv->icon_theme = gtk_icon_theme_get_default ();
    viewer->priv->bg_icon = gtk_icon_theme_load_icon (
            viewer->priv->icon_theme,
            BACKGROUND_ICON_NAME,
            BACKGROUND_ICON_SIZE,
            0,
            NULL);
    viewer->priv->missing_icon = gtk_icon_theme_load_icon (
            viewer->priv->icon_theme,
            "image-missing",
            BACKGROUND_ICON_SIZE,
            0,
            NULL);
    gdk_pixbuf_saturate_and_pixelate (
            viewer->priv->bg_icon,
            viewer->priv->bg_icon,
            0, FALSE);

    g_signal_connect (
            G_OBJECT(viewer->priv->settings),
            "notify::bgcolor",
            G_CALLBACK (cb_rstto_bgcolor_changed),
            viewer);

    g_signal_connect (
            G_OBJECT(viewer->priv->settings),
            "notify::bgcolor-override",
            G_CALLBACK (cb_rstto_bgcolor_changed),
            viewer);
    g_signal_connect (
            G_OBJECT(viewer->priv->settings),
            "notify::limit-quality",
            G_CALLBACK (cb_rstto_limit_quality_changed),
            viewer);

    g_signal_connect (
            G_OBJECT(viewer->priv->settings),
            "notify::invert-zoom-direction",
            G_CALLBACK (cb_rstto_zoom_direction_changed),
            viewer);
    g_signal_connect (
            G_OBJECT(viewer),
            "drag-data-received",
            G_CALLBACK (cb_rstto_image_viewer_dnd),
            viewer);

    gtk_widget_set_events (GTK_WIDGET(viewer),
                           GDK_BUTTON_PRESS_MASK |
                           GDK_BUTTON_RELEASE_MASK |
                           GDK_BUTTON1_MOTION_MASK |
                           GDK_ENTER_NOTIFY_MASK |
                           GDK_POINTER_MOTION_MASK);


    gtk_drag_dest_set(GTK_WIDGET(viewer), GTK_DEST_DEFAULT_ALL, NULL, 0,
                      GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE | GDK_ACTION_PRIVATE);
    gtk_drag_dest_add_uri_targets (GTK_WIDGET(viewer));

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
    GParamSpec *pspec;
    GtkWidgetClass *widget_class;
    GtkObjectClass *object_class;

    widget_class = (GtkWidgetClass*)viewer_class;
    object_class = (GtkObjectClass*)viewer_class;

    parent_class = g_type_class_peek_parent(viewer_class);

    viewer_class->set_scroll_adjustments = rstto_image_viewer_set_scroll_adjustments;

    widget_class->expose_event = rstto_image_viewer_expose;
    widget_class->realize = rstto_image_viewer_realize;
    widget_class->size_request = rstto_image_viewer_size_request;
    widget_class->size_allocate = rstto_image_viewer_size_allocate;
    widget_class->scroll_event = rstto_scroll_event;

    widget_class->button_press_event = rstto_button_press_event;
    widget_class->button_release_event = rstto_button_release_event;
    widget_class->motion_notify_event = rstto_motion_notify_event;
    widget_class->popup_menu = rstto_popup_menu;

    object_class->destroy = rstto_image_viewer_destroy;

    G_OBJECT_CLASS(object_class)->set_property = rstto_image_viewer_set_property;
    G_OBJECT_CLASS(object_class)->get_property = rstto_image_viewer_get_property;


    widget_class->set_scroll_adjustments_signal =
                  g_signal_new ("set_scroll_adjustments",
                                G_TYPE_FROM_CLASS (object_class),
                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                G_STRUCT_OFFSET (RsttoImageViewerClass, set_scroll_adjustments),
                                NULL, NULL,
                                _rstto_marshal_VOID__OBJECT_OBJECT,
                                G_TYPE_NONE, 2,
                                GTK_TYPE_ADJUSTMENT,
                                GTK_TYPE_ADJUSTMENT);
    g_signal_new (
            "size-ready",
            G_TYPE_FROM_CLASS (object_class),
            G_SIGNAL_RUN_FIRST,
            0,
            NULL, NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE, 0);
    g_signal_new ("scale-changed",
            G_TYPE_FROM_CLASS (object_class),
            G_SIGNAL_RUN_FIRST,
            0,
            NULL, NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE, 0);
    g_signal_new ("status-changed",
            G_TYPE_FROM_CLASS (object_class),
            G_SIGNAL_RUN_FIRST,
            0,
            NULL, NULL,
            g_cclosure_marshal_VOID__VOID,
            G_TYPE_NONE, 0);
    g_signal_new ("files-dnd",
            G_TYPE_FROM_CLASS (object_class),
            G_SIGNAL_RUN_FIRST,
            0,
            NULL, NULL,
            g_cclosure_marshal_VOID__POINTER,
            G_TYPE_NONE, 1,
            G_TYPE_POINTER);

    pspec = g_param_spec_boolean (
            "show-clock",
            "",
            "",
            FALSE,
            G_PARAM_READWRITE);
    g_object_class_install_property (
            G_OBJECT_CLASS(object_class),
            PROP_SHOW_CLOCK,
            pspec);
}

/**
 * rstto_image_viewer_realize:
 * @widget:
 *
 */
static void
rstto_image_viewer_realize(GtkWidget *widget)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    GValue val_bg_color = {0, };
    GValue val_bg_color_override = {0, };
    GValue val_bg_color_fs = {0, };
    GValue val_limit_quality = {0, };
    GValue val_invert_zoom = {0, };

    GtkAllocation allocation;
    GdkWindowAttr attributes;
    GdkWindow *window;
    GtkStyle *style;
    gint attributes_mask;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (RSTTO_IS_IMAGE_VIEWER(widget));

    GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

    g_value_init (&val_bg_color, GDK_TYPE_COLOR);
    g_value_init (&val_bg_color_fs, GDK_TYPE_COLOR);
    g_value_init (&val_bg_color_override, G_TYPE_BOOLEAN);
    g_value_init (&val_limit_quality, G_TYPE_BOOLEAN);
    g_value_init (&val_invert_zoom, G_TYPE_BOOLEAN);

    gtk_widget_get_allocation (widget, &allocation);
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events (widget) | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK;
    attributes.visual = gtk_widget_get_visual (widget);
    attributes.colormap = gtk_widget_get_colormap (widget);

    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
    window = gdk_window_new (gtk_widget_get_parent_window(widget), &attributes, attributes_mask);
    gtk_widget_set_window (widget, window);

    style = gtk_style_attach (gtk_widget_get_style (widget), window);
    gtk_widget_set_style (widget, style);
    gdk_window_set_user_data (window, widget);

    gtk_style_set_background (gtk_widget_get_style (widget), window, GTK_STATE_ACTIVE);

    g_object_get_property (
            G_OBJECT(viewer->priv->settings),
            "bgcolor",
            &val_bg_color);
    g_object_get_property (
            G_OBJECT(viewer->priv->settings),
            "bgcolor-override",
            &val_bg_color_override);
    g_object_get_property (
            G_OBJECT(viewer->priv->settings),
            "bgcolor-fullscreen",
            &val_bg_color_fs);
    g_object_get_property (
            G_OBJECT(viewer->priv->settings),
            "limit-quality",
            &val_limit_quality);
    g_object_get_property (
            G_OBJECT(viewer->priv->settings),
            "invert-zoom-direction",
            &val_invert_zoom);

    viewer->priv->limit_quality = g_value_get_boolean (&val_limit_quality);
    viewer->priv->invert_zoom_direction = g_value_get_boolean (&val_invert_zoom);

    if (TRUE == g_value_get_boolean (&val_bg_color_override))
    {
        viewer->priv->bg_color = g_value_get_boxed (&val_bg_color);
    }
    else
    {
        viewer->priv->bg_color = &(gtk_widget_get_style (widget)->bg[GTK_STATE_NORMAL]);
    }

    viewer->priv->bg_color_fs = g_value_get_boxed (&val_bg_color_fs);

    g_value_unset (&val_bg_color);
    g_value_unset (&val_bg_color_fs);
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
 */
static void
rstto_image_viewer_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gint border_width = 0;

    gtk_widget_set_allocation (widget, allocation);
    if (GTK_WIDGET_REALIZED (widget))
    {
        gdk_window_move_resize (gtk_widget_get_window (widget),
                allocation->x + border_width,
                allocation->y + border_width,
                allocation->width - border_width * 2,
                allocation->height - border_width * 2);

        /* Check if auto_scale == TRUE, if so, calculate the new
         * scale based on the new widget-size.
         */
        if (TRUE == viewer->priv->auto_scale)
        {
            set_scale (viewer, 0.0);
        }

        gdk_window_invalidate_rect (
                gtk_widget_get_window (widget),
                NULL,
                FALSE);
    }
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

    cairo_t *ctx;

    /* get a cairo_t */
    ctx = gdk_cairo_create (gtk_widget_get_window (widget));

    /* set a clip region for the expose event */
    if (FALSE == viewer->priv->auto_scale)
    {
        cairo_rectangle (
                ctx,
                event->area.x, event->area.y,
                event->area.width, event->area.height);
        cairo_clip (ctx);
    }

    cairo_save (ctx);

    rstto_image_viewer_paint (widget, ctx);

    cairo_restore (ctx);

    cairo_destroy (ctx);


    return TRUE;
}

static void
rstto_image_viewer_destroy(GtkObject *object)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER(object);

    if (viewer->priv)
    {
        if (viewer->priv->settings)
        {
            g_object_unref (viewer->priv->settings);
            viewer->priv->settings = NULL;
        }
        if (viewer->priv->bg_icon)
        {
            g_object_unref (viewer->priv->bg_icon);
            viewer->priv->bg_icon = NULL;
        }
        if (viewer->priv->missing_icon)
        {
            g_object_unref (viewer->priv->missing_icon);
            viewer->priv->missing_icon = NULL;
        }
        if (viewer->priv->pixbuf)
        {
            g_object_unref (viewer->priv->pixbuf);
            viewer->priv->pixbuf = NULL;
        }
        if (viewer->priv->iter)
        {
            g_object_unref (viewer->priv->iter);
            viewer->priv->iter = NULL;
        }
        g_free (viewer->priv);
        viewer->priv = NULL;
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
        gtk_adjustment_set_lower (viewer->hadjustment, 0);
        gtk_adjustment_set_upper (viewer->hadjustment, 0);

        g_signal_connect(G_OBJECT(viewer->hadjustment), "value-changed", (GCallback)viewer->priv->cb_value_changed, viewer);
        g_object_ref(viewer->hadjustment);
    }
    if(viewer->vadjustment)
    {
        gtk_adjustment_set_lower (viewer->vadjustment, 0);
        gtk_adjustment_set_upper (viewer->vadjustment, 0);

        g_signal_connect(G_OBJECT(viewer->vadjustment), "value-changed", (GCallback)viewer->priv->cb_value_changed, viewer);
        g_object_ref(viewer->vadjustment);
    }
    return TRUE;
}

static void
set_scale (RsttoImageViewer *viewer, gdouble scale )
{
    gboolean auto_scale = FALSE;
    gdouble v_scale;
    gdouble h_scale;
    GtkAllocation allocation;

    gtk_widget_get_allocation (GTK_WIDGET (viewer), &allocation);

    switch (viewer->priv->orientation)
    {
        case RSTTO_IMAGE_ORIENT_90:
        case RSTTO_IMAGE_ORIENT_270:
            v_scale = (gdouble)(allocation.width) / (gdouble)viewer->priv->image_height;
            h_scale = (gdouble)(allocation.height) / (gdouble)viewer->priv->image_width;
            break;
        case RSTTO_IMAGE_ORIENT_NONE:
        case RSTTO_IMAGE_ORIENT_180:
        default:
            v_scale = (gdouble)(allocation.width) / (gdouble)viewer->priv->image_width;
            h_scale = (gdouble)(allocation.height) / (gdouble)viewer->priv->image_height;
            break;
    }

    if (scale == -1.0)
    {
        if (h_scale < v_scale)
        {
            scale = h_scale;
        }
        else
        {
            scale = v_scale;
        }
    
        if (scale < 1.0)
        {
            auto_scale = TRUE;
        }
        else
        {
            scale = 1.0;
            auto_scale = FALSE;
        }
    }
    if (scale == 0)
    {
        auto_scale = TRUE;

        if (h_scale < v_scale)
        {
            scale = h_scale;
        }
        else
        {
            scale = v_scale;
        }
    }
    else
    {
        /* If the original image is larger then the screen-size,
         * make sure the image is not rendered beyond RSTTO_MAX_SCALE
         */
        if (viewer->priv->image_scale < 1.0)
        {
            if (scale > RSTTO_MAX_SCALE)
            {
                scale = RSTTO_MAX_SCALE;
            }
        }
        else
        {
            /* Else, assume image_scale == 1.0 */

            /* If the image is smaller then the screen-size, it can not be
             * zoomed in beyond RSTTO_MAX_SCALE or the window-size.
             *
             * Whichever comes last.
             */
            if ((h_scale > RSTTO_MAX_SCALE) || (v_scale > RSTTO_MAX_SCALE))
            {
                if (h_scale < v_scale)
                {
                    /* If the image is scaled beyond the window-size, 
                     * force the scale to fit the window and set auto_scale = TRUE.
                     */
                    if (scale > h_scale)
                    {
                        auto_scale = TRUE;
                        scale = h_scale;
                    }
                }
                else
                {
                    /* If the image is scaled beyond the window-size, 
                     * force the scale to fit the window and set auto_scale = TRUE.
                     */
                    if (scale > v_scale)
                    {
                        auto_scale = TRUE;
                        scale = v_scale;
                    }
                }
            }
            else
            {
                /* For all remaining conditions, do not scale beyond
                 * RSTTO_MAX_SCALE
                 */
                if (scale > RSTTO_MAX_SCALE)
                {
                    scale = RSTTO_MAX_SCALE;
                }
            }
        }

        /*
         * There is no need to zoom out beyond 64x64 pixels
         * unless, ofcourse the image itself is smaller then 64x64 pixels.
         */
        if (viewer->priv->image_width > viewer->priv->image_height)
        {
            if ((viewer->priv->image_width >= 64) && ((scale * viewer->priv->image_width) < 64))
            {
                scale = (64.0 / (gdouble)viewer->priv->image_width);
            }
            if ((viewer->priv->image_width < 64) && (scale < 1.0))
            {
                scale = 1.0; 
            }
        }
        else
        {
            if ((viewer->priv->image_height >= 64) && ((scale * viewer->priv->image_height) < 64))
            {
                scale = (64.0 / (gdouble)viewer->priv->image_height);
            }
            if ((viewer->priv->image_height < 64) && (scale < 1.0))
            {
                scale = 1.0; 
            }
        }
    }

    viewer->priv->auto_scale = auto_scale;
    viewer->priv->scale = scale;
    g_signal_emit_by_name(viewer, "scale-changed");
}
 
static void
paint_background (GtkWidget *widget, cairo_t *ctx)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    GdkColor *bg_color = NULL;

    /* Determine if we draw the 'default' background-color,
     * or the fullscreen-background-color.
     */
    if (GDK_WINDOW_STATE_FULLSCREEN & gdk_window_get_state (
                gdk_window_get_toplevel (gtk_widget_get_window (widget))))
    {
        bg_color = viewer->priv->bg_color_fs;
    }

    if (NULL == bg_color)
    {
        bg_color = viewer->priv->bg_color;
    }

    /* Paint the background-color */
    /******************************/
    gdk_cairo_set_source_color (ctx, bg_color);
    cairo_paint (ctx);
}

static void
paint_background_icon (GtkWidget *widget, cairo_t *ctx )
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gdouble bg_scale = 1.0;
    GtkAllocation allocation;

    gtk_widget_get_allocation (widget, &allocation);

    /* If there is no image shown, render the ristretto
     * logo on the background.
     */

    /* Calculate the icon-size */
    /***************************/
    bg_scale = (allocation.width < allocation.height)
            ? (gdouble)BACKGROUND_ICON_SIZE / (gdouble)allocation.width * 1.2
            : (gdouble)BACKGROUND_ICON_SIZE / (gdouble)allocation.height * 1.2;

    /* Move the cairo context in position so the
     * background-image is painted in the center
     * of the widget.
     */
    cairo_translate (
            ctx,
            (gdouble)(allocation.width-BACKGROUND_ICON_SIZE/bg_scale)/2.0,
            (gdouble)(allocation.height-BACKGROUND_ICON_SIZE/bg_scale)/2.0);

    /* Scale the context so the image
     * fills the same part of the cairo-context
     */
    cairo_scale (
            ctx,
            1.0 / bg_scale,
            1.0 / bg_scale);

    /* Draw the pixbuf on the cairo-context */
    /****************************************/
    if (viewer->priv->bg_icon != NULL)
    {
        gdk_cairo_set_source_pixbuf (
                ctx,
                viewer->priv->bg_icon,
                0.0,
                0.0);
        cairo_paint_with_alpha (ctx, 0.1);
    }
}

static void
correct_adjustments (RsttoImageViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET (viewer);
    gdouble image_width = (gdouble)viewer->priv->image_width;
    gdouble image_height = (gdouble)viewer->priv->image_height;
    gdouble scale = viewer->priv->scale;
    GtkAllocation allocation;

    /* Check if the image-size makes sense,
     * if not, set the upper limits to 0.0
     */
    if ( (image_width <= 1.0) || (image_height <= 1.0) )
    {
        gtk_adjustment_set_value (
                viewer->hadjustment,
                0.0 );
        gtk_adjustment_set_upper (
                viewer->hadjustment,
                0.0 );
        gtk_adjustment_set_value (
                viewer->vadjustment,
                0.0 );
        gtk_adjustment_set_upper (
                viewer->vadjustment,
                0.0 );
        return;
    }

    g_object_freeze_notify(G_OBJECT(viewer->hadjustment));
    g_object_freeze_notify(G_OBJECT(viewer->vadjustment));

    gtk_widget_get_allocation (widget, &allocation);

    switch (viewer->priv->orientation)
    {
        case RSTTO_IMAGE_ORIENT_NONE:
        case RSTTO_IMAGE_ORIENT_180:
        default:
            gtk_adjustment_set_upper (
                    viewer->hadjustment,
                    floor(image_width * scale));
            gtk_adjustment_set_upper (
                    viewer->vadjustment,
                    floor(image_height * scale));

            gtk_adjustment_set_page_size (
                    viewer->hadjustment,
                    (gdouble)allocation.width);
            gtk_adjustment_set_page_size (
                    viewer->vadjustment,
                    (gdouble)allocation.height);

            if ( ( gtk_adjustment_get_value (viewer->hadjustment) +
                   gtk_adjustment_get_page_size (viewer->hadjustment) ) >
                   gtk_adjustment_get_upper (viewer->hadjustment) )
            {
                gtk_adjustment_set_value (
                        viewer->hadjustment,
                        floor((image_width * scale) - 
                            (gdouble)allocation.width));
            }

            if ( ( gtk_adjustment_get_value (viewer->vadjustment) +
                   gtk_adjustment_get_page_size (viewer->vadjustment) ) >
                   gtk_adjustment_get_upper (viewer->vadjustment) )
            {
                gtk_adjustment_set_value (
                        viewer->vadjustment,
                        floor((image_height * scale) - 
                            (gdouble)allocation.height));
            }
            break;
        case RSTTO_IMAGE_ORIENT_90:
        case RSTTO_IMAGE_ORIENT_270:
            gtk_adjustment_set_upper (
                    viewer->hadjustment,
                    floor(image_height * scale));
            gtk_adjustment_set_upper (
                    viewer->vadjustment,
                    floor(image_width * scale));

            gtk_adjustment_set_page_size (
                    viewer->hadjustment,
                    (gdouble)allocation.width);
            gtk_adjustment_set_page_size (
                    viewer->vadjustment,
                    (gdouble)allocation.height);

            if ( ( gtk_adjustment_get_value (viewer->hadjustment) +
                   gtk_adjustment_get_page_size (viewer->hadjustment) ) >
                   gtk_adjustment_get_upper (viewer->hadjustment) )
            {
                gtk_adjustment_set_value (
                        viewer->hadjustment,
                        (image_height * scale) - 
                            (gdouble)allocation.width);
            }

            if ( ( gtk_adjustment_get_value (viewer->vadjustment) +
                   gtk_adjustment_get_page_size (viewer->vadjustment) ) >
                   gtk_adjustment_get_upper (viewer->vadjustment) )
            {
                gtk_adjustment_set_value (
                        viewer->vadjustment,
                        (image_width * scale) - 
                            (gdouble)allocation.height);
            }
            break;
    }

    g_object_thaw_notify(G_OBJECT(viewer->hadjustment));
    g_object_thaw_notify(G_OBJECT(viewer->vadjustment));
}

static void
paint_clock (GtkWidget *widget, cairo_t *ctx)
{
    //RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gdouble width;
    gdouble height;
    gdouble offset;
    gint i = 0;
    time_t t = time(NULL);
    struct tm *lt = localtime(&t);
    GtkAllocation allocation;

    gdouble hour_angle = (gdouble)(M_PI*2)/12*((gdouble)(lt->tm_hour%12+6)+((M_PI*2)/720.0*(gdouble)lt->tm_min));
    gtk_widget_get_allocation (widget, &allocation);

    width = (allocation.width < allocation.height)
            ? 40 + ((gdouble)allocation.width * 0.07)
            : 40 + ((gdouble)allocation.height * 0.07);

    height = width;
    offset = height * 0.15;

    cairo_save (ctx);

    cairo_translate (
        ctx,
        allocation.width - offset - width,
        allocation.height - offset - height);

    cairo_set_source_rgba (ctx, 0.0, 0.0, 0.0, 0.4);
    cairo_save(ctx);
    cairo_translate (
        ctx,
        width/2,
        height/2);
    cairo_arc (
        ctx,
        00, 00,
        width/2,
        0,
        2*M_PI );
    cairo_fill (ctx);

    cairo_set_source_rgba (ctx, 1.0, 1.0, 1.0, 0.8);

    cairo_save(ctx);
    for (i = 0; i < 12; ++i)
    {
        cairo_rotate (
            ctx,
            30*(M_PI/180));
        cairo_arc (
            ctx,
            00, -1*((width/2)-5),
            width/20,
            0,
            2*M_PI );
        cairo_fill (ctx);
    }
    cairo_restore (ctx);

/***/
    cairo_save (ctx);
    cairo_set_line_width (ctx, width/15);
    cairo_set_line_cap (ctx, CAIRO_LINE_CAP_ROUND);
    cairo_rotate (
        ctx,
        hour_angle);
    cairo_move_to (ctx, 0, 0);
    cairo_line_to (
        ctx,
        0,
        height*0.2);
    cairo_stroke (
        ctx);
    cairo_restore (ctx);
/***/
    cairo_set_line_width (ctx, width/15);
    cairo_set_line_cap (ctx, CAIRO_LINE_CAP_ROUND);
    cairo_rotate (
        ctx,
        (M_PI*2)/60*(lt->tm_min%60+30));
    cairo_move_to (ctx, 0, 0);
    cairo_line_to (
        ctx,
        0,
        height*0.3);
    cairo_stroke (
        ctx);

    cairo_restore (ctx);
    cairo_restore (ctx);
}

static void
paint_image (GtkWidget *widget, cairo_t *ctx)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gint i = 0;
    gint a = 0;
    gdouble x_offset;
    gdouble y_offset;
    gint block_width = 10;
    gint block_height = 10;
    gdouble bg_scale = 1.0;
    GtkAllocation allocation;

    gtk_widget_get_allocation (widget, &allocation);

    if (viewer->priv->pixbuf)
    {
        switch (viewer->priv->orientation)
        {
            case RSTTO_IMAGE_ORIENT_90:
            case RSTTO_IMAGE_ORIENT_270:
                viewer->priv->rendering.x_offset = ((gdouble)allocation.width - (
                            (gdouble)viewer->priv->image_height * 
                                viewer->priv->scale) ) / 2.0;
                viewer->priv->rendering.y_offset = ((gdouble)allocation.height - (
                            (gdouble)viewer->priv->image_width * 
                                viewer->priv->scale) ) / 2.0;
                viewer->priv->rendering.width = 
                        (gdouble)viewer->priv->image_height * viewer->priv->scale;
                viewer->priv->rendering.height = 
                        (gdouble)viewer->priv->image_width * viewer->priv->scale;
                break;
            case RSTTO_IMAGE_ORIENT_NONE:
            case RSTTO_IMAGE_ORIENT_180:
            default:
                viewer->priv->rendering.x_offset = ((gdouble)allocation.width - (
                            (gdouble)viewer->priv->image_width * 
                                viewer->priv->scale) ) / 2.0;
                viewer->priv->rendering.y_offset = ((gdouble)allocation.height - (
                            (gdouble)viewer->priv->image_height * 
                                viewer->priv->scale) ) / 2.0;
                viewer->priv->rendering.width = 
                        (gdouble)viewer->priv->image_width * viewer->priv->scale;
                viewer->priv->rendering.height = 
                        (gdouble)viewer->priv->image_height * viewer->priv->scale;
                break;

        }

        if (viewer->priv->rendering.x_offset < 0)
        {
            viewer->priv->rendering.x_offset = 0;
        }
        if (viewer->priv->rendering.y_offset < 0)
        {
            viewer->priv->rendering.y_offset = 0;
        }

        cairo_save (ctx);
        x_offset = floor ( viewer->priv->rendering.x_offset );
        y_offset = floor ( viewer->priv->rendering.y_offset );

/* BEGIN PAINT CHECKERED BACKGROUND */
        if (TRUE == gdk_pixbuf_get_has_alpha (viewer->priv->pixbuf))
        {
            cairo_set_source_rgba (ctx, 0.8, 0.8, 0.8, 1.0);
            cairo_rectangle (
                    ctx,
                    x_offset,
                    y_offset,
                    viewer->priv->rendering.width,
                    viewer->priv->rendering.height);
            cairo_fill (ctx);

            cairo_set_source_rgba (ctx, 0.7, 0.7, 0.7, 1.0);
            for (i = 0; i < viewer->priv->rendering.width/10; ++i)
            {
                if(i%2){a=1;}
                else{a=0;}

                if ((i+1) <= (viewer->priv->rendering.width/10))
                {
                    block_width = 10;
                }
                else
                {
                    block_width = ((gint)viewer->priv->rendering.width)%10;
                }
                for (; a < viewer->priv->rendering.height/10; a+=2)
                {
                    if ((a+1) <= (viewer->priv->rendering.height/10))
                    {
                        block_height = 10;
                    }
                    else
                    {
                        block_height = ((gint)viewer->priv->rendering.height%10);
                    }
                    cairo_rectangle (
                            ctx,
                            x_offset + i*10,
                            y_offset + a*10,
                            block_width,
                            block_height);
                    cairo_fill (ctx);
                }
            }
        }
/* END PAINT CHECKERED BACKGROUND */
        cairo_restore (ctx);

        /* TODO: make this work for all rotations */
        switch (viewer->priv->orientation)
        {
            case RSTTO_IMAGE_ORIENT_90:
                cairo_rotate (
                        ctx,
                        M_PI*0.5);
                cairo_translate (
                        ctx,
                        floor(0.0 - gtk_adjustment_get_value (viewer->vadjustment)),
                        floor(gtk_adjustment_get_value (viewer->hadjustment)));
                cairo_translate (
                        ctx,
                        0.0,
                        -1.0 * viewer->priv->image_height * viewer->priv->scale);
                cairo_translate (
                        ctx,
                        y_offset,
                        -1.0 * x_offset);
                break;
            case RSTTO_IMAGE_ORIENT_270:
                cairo_rotate (
                        ctx,
                        M_PI*1.5);
                cairo_translate (
                        ctx,
                        floor(gtk_adjustment_get_value (viewer->vadjustment)),
                        0.0 - floor(gtk_adjustment_get_value (viewer->hadjustment)));
                cairo_translate (
                        ctx,
                        -1.0 * viewer->priv->image_width * viewer->priv->scale,
                        0.0);

                cairo_translate (
                        ctx,
                        -1.0 * y_offset,
                        x_offset);
                break;
            case RSTTO_IMAGE_ORIENT_180:
                cairo_rotate (
                        ctx,
                        M_PI);
                cairo_translate (
                        ctx,
                        floor(gtk_adjustment_get_value (viewer->hadjustment)),
                        floor(gtk_adjustment_get_value (viewer->vadjustment)));
                cairo_translate (
                        ctx,
                        -1.0 * viewer->priv->image_width * viewer->priv->scale,
                        -1.0 * viewer->priv->image_height * viewer->priv->scale);

                cairo_translate (
                        ctx,
                        -1.0 * x_offset,
                        -1.0 * y_offset);
                break;
            case RSTTO_IMAGE_ORIENT_NONE:
            default:
                cairo_translate (
                        ctx,
                        0.0 - floor(gtk_adjustment_get_value (viewer->hadjustment)),
                        0.0 - floor(gtk_adjustment_get_value (viewer->vadjustment)));

                cairo_translate (
                        ctx,
                        x_offset,
                        y_offset);
                break;

        }

        cairo_scale (
                ctx,
                (viewer->priv->scale/viewer->priv->image_scale),
                (viewer->priv->scale/viewer->priv->image_scale));

        gdk_cairo_set_source_pixbuf (
                ctx,
                viewer->priv->pixbuf,
                0.0,
                0.0);
        cairo_paint (ctx);
    }
    else
    {
        if (viewer->priv->error)
        {
            /* Calculate the icon-size */
            /***************************/
            bg_scale = (allocation.width < allocation.height)
                    ? (gdouble)BACKGROUND_ICON_SIZE / (gdouble)allocation.width * 1.2
                    : (gdouble)BACKGROUND_ICON_SIZE / (gdouble)allocation.height * 1.2;

            /* Move the cairo context in position so the
             * background-image is painted in the center
             * of the widget.
             */
            cairo_translate (
                    ctx,
                    (gdouble)(allocation.width-BACKGROUND_ICON_SIZE/bg_scale)/2.0,
                    (gdouble)(allocation.height-BACKGROUND_ICON_SIZE/bg_scale)/2.0);

            /* Scale the context so the image
             * fills the same part of the cairo-context
             */
            cairo_scale (
                    ctx,
                    1.0 / bg_scale,
                    1.0 / bg_scale);

            /* Draw the pixbuf on the cairo-context */
            /****************************************/
            if(viewer->priv->missing_icon != NULL)
            {
                gdk_cairo_set_source_pixbuf (
                        ctx,
                        viewer->priv->missing_icon,
                        0.0,
                        0.0);
                cairo_paint_with_alpha (ctx, 1.0);
            }
        }
    }

}

static void
paint_selection_box (GtkWidget *widget, cairo_t *ctx )
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gdouble box_y = 0.0;
    gdouble box_x = 0.0;
    gdouble box_width = 0.0;
    gdouble box_height = 0.0;
    gdouble x_offset = viewer->priv->rendering.x_offset;
    gdouble y_offset = viewer->priv->rendering.y_offset;
    gdouble image_width = viewer->priv->rendering.width;
    gdouble image_height = viewer->priv->rendering.height;

    /* A selection-box can be created moving the cursor from
     * left to right, aswell as from right to left.
     * 
     * Calculate the box dimensions accordingly.
     */
    if (viewer->priv->motion.y < viewer->priv->motion.current_y)
    {
        box_y = (gdouble)viewer->priv->motion.y;
        box_height = (gdouble)viewer->priv->motion.current_y - box_y;
    }
    else
    {
        box_y = (gdouble)viewer->priv->motion.current_y;
        box_height = (gdouble)viewer->priv->motion.y - box_y;
    }

    /* A selection-box can be created moving the cursor from
     * top to bottom, aswell as from bottom to top.
     * 
     * Calculate the box dimensions accordingly.
     */
    if (viewer->priv->motion.x < viewer->priv->motion.current_x)
    {
        box_x = (gdouble)viewer->priv->motion.x;
        box_width = (gdouble)viewer->priv->motion.current_x - box_x;
    }
    else
    {
        box_x = (gdouble)viewer->priv->motion.current_x;
        box_width = (gdouble)viewer->priv->motion.x - box_x;
    }

    /*
     * Constrain the selection-box to the left
     * and top sides of the image.
     */
    if (box_x < x_offset)
    {
        box_width = box_width - (x_offset - box_x);
        box_x = x_offset;
    }
    if (box_y < y_offset)
    {
        box_height = box_height - (y_offset - box_y);
        box_y = y_offset;
    }

    if ((x_offset + image_width) < (box_x + box_width))
    {
        box_width = (x_offset + image_width) - box_x - 1;
    }
    if ((y_offset + image_height) < (box_y + box_height))
    {
        box_height = (y_offset + image_height) - box_y - 1;
    }

    /* Make sure the box dimensions are not negative,
     * This results in rendering-artifacts.
     */
    if (box_width < 0.0)
    {
        /* Return, do not draw the box.  */
        return;
    }
    /* Same as above, for the vertical dimensions this time */
    if (box_height < 0.0)
    {
        /* Return, do not draw the box.  */
        return;
    }

    cairo_rectangle (
        ctx,
        box_x+0.5, box_y+0.5,
        box_width, box_height);

    cairo_set_source_rgba (ctx, 0.9, 0.9, 0.9, 0.2);
    cairo_fill_preserve (ctx);
    cairo_set_source_rgba (ctx, 0.2, 0.2, 0.2, 1.0);
    cairo_set_line_width (ctx, 1.0);
    cairo_stroke (ctx);
}

static void
rstto_image_viewer_paint (GtkWidget *widget, cairo_t *ctx)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    GtkAllocation allocation;
    
    if (GTK_WIDGET_REALIZED (widget))
    {
        gtk_widget_get_allocation (widget, &allocation);
        correct_adjustments (viewer);

        cairo_rectangle (
                ctx,
                0.0,
                0.0,
                (gdouble)allocation.width,
                (gdouble)allocation.height);
        cairo_clip (ctx);
        cairo_save (ctx);
        
        /* Paint the background-color */
        /******************************/
        paint_background (widget, ctx);        

        cairo_restore (ctx);

        /* Check if a file should be rendered */
        /**************************************/
        if ( NULL == viewer->priv->file )
        {
            cairo_save (ctx);
            
            /* Paint the background-image (ristretto icon) */
            /***********************************************/
            paint_background_icon (widget, ctx);        

            cairo_restore (ctx);

            if (viewer->priv->props.show_clock)
            {
                cairo_save (ctx);
                paint_clock (widget, ctx);        
                cairo_restore (ctx);
            }
        }
        else
        {
            cairo_save (ctx);
            paint_image (widget, ctx);        
            cairo_restore (ctx);

            if (viewer->priv->motion.state == RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM)
            {
                cairo_save (ctx);
                paint_selection_box (widget, ctx);        
                cairo_restore (ctx);
            }

            if (viewer->priv->props.show_clock)
            {
                cairo_save (ctx);
                paint_clock (widget, ctx);        
                cairo_restore (ctx);
            }
        }
    }
}

GtkWidget *
rstto_image_viewer_new (void)
{
    GtkWidget *widget;

    widget = g_object_new (RSTTO_TYPE_IMAGE_VIEWER, NULL);

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
rstto_image_viewer_set_file (RsttoImageViewer *viewer, RsttoFile *file, gdouble scale, RsttoImageOrientation orientation)
{
    GtkWidget *widget = GTK_WIDGET (viewer); 

    /*
     * Set the image-orientation
     */
    viewer->priv->orientation = orientation;

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
            if (!rstto_file_equal (viewer->priv->file, file))
            {
                /*
                 * This will first need to return to the 'main' loop before it cleans up after itself.
                 * We can forget about the transaction, once it's cancelled, it will clean-up itself. -- (it should)
                 */
                if (viewer->priv->transaction)
                {
                    if (FALSE == g_cancellable_is_cancelled (viewer->priv->transaction->cancellable))
                    {
                        g_cancellable_cancel (viewer->priv->transaction->cancellable);
                    }
                    viewer->priv->transaction = NULL;
                } 

                g_object_ref (file);
                g_signal_connect (
                        file,
                        "changed",
                        G_CALLBACK (cb_rstto_image_viewer_file_changed),
                        viewer);
                g_signal_handlers_disconnect_by_func (
                        viewer->priv->file,
                        cb_rstto_image_viewer_file_changed,
                        viewer );
                g_object_unref (viewer->priv->file);

                viewer->priv->file = file;
                if (viewer->priv->error)
                {
                    g_error_free (viewer->priv->error);
                    viewer->priv->error = NULL;
                }
                viewer->priv->image_scale = 1.0;
                viewer->priv->image_width = 1.0;
                viewer->priv->image_height = 1.0;

                rstto_image_viewer_load_image (
                        viewer,
                        viewer->priv->file,
                        scale);
            }
        }
        else
        {
            g_signal_connect (
                    file,
                    "changed",
                    G_CALLBACK (cb_rstto_image_viewer_file_changed),
                    viewer);
            g_object_ref (file);
            viewer->priv->file = file;
            rstto_image_viewer_load_image (viewer, viewer->priv->file, scale);
        }
    } 
    else
    {
        if (viewer->priv->iter)
        {
            g_object_unref (viewer->priv->iter);
            viewer->priv->iter = NULL;
        }
        if (viewer->priv->animation)
        {
            g_object_unref (viewer->priv->animation);
            viewer->priv->animation = NULL;
        }
        if (viewer->priv->pixbuf)
        {
            g_object_unref (viewer->priv->pixbuf);
            viewer->priv->pixbuf = NULL;
        }
        if (viewer->priv->transaction)
        {
            if (FALSE == g_cancellable_is_cancelled (viewer->priv->transaction->cancellable))
            {
                g_cancellable_cancel (viewer->priv->transaction->cancellable);
            }
            viewer->priv->transaction = NULL;
        }
        if (viewer->priv->file)
        {
            g_signal_handlers_disconnect_by_func (
                    viewer->priv->file,
                    cb_rstto_image_viewer_file_changed,
                    viewer );
            g_object_unref (viewer->priv->file);
            viewer->priv->file = NULL;

            /* Reset the image-size to 0.0 */
            viewer->priv->image_height = 0.0;
            viewer->priv->image_width = 0.0;
            
            gdk_window_invalidate_rect (
                    gtk_widget_get_window (widget),
                    NULL,
                    FALSE);

            gtk_widget_set_tooltip_text (GTK_WIDGET (viewer), NULL);
        }
    }
}

static void
rstto_image_viewer_load_image (RsttoImageViewer *viewer, RsttoFile *file, gdouble scale)
{
    RsttoImageViewerTransaction *transaction = g_new0 (RsttoImageViewerTransaction, 1);

    /*
     * This will first need to return to the 'main' loop before it cleans up after itself.
     * We can forget about the transaction, once it's cancelled, it will clean-up itself. -- (it should)
     */
    if (viewer->priv->transaction)
    {
        g_cancellable_cancel (viewer->priv->transaction->cancellable);
        viewer->priv->transaction = NULL;
    }

    transaction->loader = gdk_pixbuf_loader_new_with_mime_type (rstto_file_get_content_type (file), NULL);

    /* HACK HACK HACK */
    if (transaction->loader == NULL)
    {
        transaction->loader = gdk_pixbuf_loader_new();
    }

    transaction->cancellable = g_cancellable_new();
    transaction->buffer = g_new0 (guchar, RSTTO_IMAGE_VIEWER_BUFFER_SIZE);
    transaction->file = file;
    transaction->viewer = viewer;
    transaction->scale = scale;

    g_signal_connect(transaction->loader, "area-prepared", G_CALLBACK(cb_rstto_image_loader_area_prepared), transaction);
    g_signal_connect(transaction->loader, "size-prepared", G_CALLBACK(cb_rstto_image_loader_size_prepared), transaction);
    g_signal_connect(transaction->loader, "closed", G_CALLBACK(cb_rstto_image_loader_closed), transaction);

    viewer->priv->transaction = transaction;

    g_file_read_async (rstto_file_get_file (transaction->file),
                       0,
                       transaction->cancellable,
                       (GAsyncReadyCallback)cb_rstto_image_viewer_read_file_ready,
                       transaction);
}

static void
rstto_image_viewer_transaction_free (RsttoImageViewerTransaction *tr)
{
    /*
     * Check if this transaction is current,
     * if so, remove the reference from the viewer.
     */
    if (tr->viewer->priv->transaction == tr)
    {
        tr->viewer->priv->transaction = NULL;
    }
    if (tr->error)
    {
        g_error_free (tr->error);
    }
    g_object_unref (tr->cancellable);
    g_object_unref (tr->loader);
    g_free (tr->buffer);
    g_free (tr);
}

void
rstto_image_viewer_set_scale (RsttoImageViewer *viewer, gdouble scale)
{
    GtkWidget *widget = GTK_WIDGET (viewer);

    gdouble x_offset;
    gdouble y_offset;
    gdouble tmp_x;
    gdouble tmp_y;
    GtkAllocation allocation;

    gtk_widget_get_allocation (widget, &allocation);

    switch (viewer->priv->orientation)
    {
        case RSTTO_IMAGE_ORIENT_90:
        case RSTTO_IMAGE_ORIENT_270:
            x_offset = ((gdouble)allocation.width - (
                (gdouble)viewer->priv->image_height * 
                    viewer->priv->scale) ) / 2.0;
            y_offset = ((gdouble)allocation.height - (
                (gdouble)viewer->priv->image_width * 
                    viewer->priv->scale) ) / 2.0;
            break;
        case RSTTO_IMAGE_ORIENT_NONE:
        default:
            x_offset = ((gdouble)allocation.width - (
                (gdouble)viewer->priv->image_width * 
                    viewer->priv->scale) ) / 2.0;
            y_offset = ((gdouble)allocation.height - (
                (gdouble)viewer->priv->image_height * 
                    viewer->priv->scale) ) / 2.0;
            break;
    }

    if (x_offset < 0) x_offset = 0;
    if (y_offset < 0) y_offset = 0;

    tmp_x = (gtk_adjustment_get_value(viewer->hadjustment) + 
            (gtk_adjustment_get_page_size (viewer->hadjustment) / 2) - 
                x_offset) / viewer->priv->scale;
    tmp_y = (gtk_adjustment_get_value(viewer->vadjustment) + 
            (gtk_adjustment_get_page_size (viewer->vadjustment) / 2) - 
                y_offset) / viewer->priv->scale;
    
    set_scale (viewer, scale);

    g_object_freeze_notify(G_OBJECT(viewer->hadjustment));
    g_object_freeze_notify(G_OBJECT(viewer->vadjustment));

    /* The value here can possibly be set to a wrong value,
     * the _paint function calls 'correct adjustments' to
     * solve this issue. Hence, we do not need to call it
     * here.
     */
    gtk_adjustment_set_value (
            viewer->vadjustment,
            ( tmp_y * viewer->priv->scale -
                ( gtk_adjustment_get_page_size (
                        viewer->vadjustment) / 2 )));
    gtk_adjustment_set_value (
            viewer->hadjustment,
            ( tmp_x * viewer->priv->scale -
                ( gtk_adjustment_get_page_size (
                        viewer->hadjustment) / 2 )));

    g_object_thaw_notify(G_OBJECT(viewer->vadjustment));
    g_object_thaw_notify(G_OBJECT(viewer->hadjustment));

    gdk_window_invalidate_rect (
            gtk_widget_get_window (widget),
            NULL,
            FALSE);
}

gdouble
rstto_image_viewer_get_scale (RsttoImageViewer *viewer)
{
    return viewer->priv->scale;
}

static void
rstto_image_viewer_set_motion_state (RsttoImageViewer *viewer, RsttoImageViewerMotionState state)
{
    viewer->priv->motion.state = state;
}

/*
 * rstto_image_viewer_set_orientation:
 * @viewer:
 * @orientation:
 *
 * Set orientation for the image shown here.
 */
void
rstto_image_viewer_set_orientation (RsttoImageViewer *viewer, RsttoImageOrientation orientation)
{
    GtkWidget *widget = GTK_WIDGET (viewer); 

    viewer->priv->orientation = orientation;

    if (viewer->priv->auto_scale)
    {
        set_scale (viewer, 0.0);
    }

    rstto_file_set_orientation (viewer->priv->file, orientation);

    gdk_window_invalidate_rect (
            gtk_widget_get_window (widget),
            NULL,
            FALSE);

}

RsttoImageOrientation
rstto_image_viewer_get_orientation (RsttoImageViewer *viewer)
{
    if (viewer)
    {
        return viewer->priv->orientation;
    }
    return RSTTO_IMAGE_ORIENT_NONE;
}

gint
rstto_image_viewer_get_width (RsttoImageViewer *viewer)
{
    if (viewer)
    {
        return viewer->priv->image_width;
    }
    return 0;
}
gint
rstto_image_viewer_get_height (RsttoImageViewer *viewer)
{
    if (viewer)
    {
        return viewer->priv->image_height;
    }
    return 0;
}

void
rstto_image_viewer_set_menu (RsttoImageViewer *viewer, GtkMenu *menu)
{
    if (viewer->priv->menu)
    {
        gtk_menu_detach(viewer->priv->menu);
        gtk_widget_destroy(GTK_WIDGET(viewer->priv->menu));
    }

    viewer->priv->menu = menu;

    if (viewer->priv->menu)
    {
        gtk_menu_attach_to_widget(
                viewer->priv->menu,
                GTK_WIDGET(viewer),
                NULL);
    }

}


/************************/
/** CALLBACK FUNCTIONS **/
/************************/

static void
cb_rstto_image_viewer_value_changed (GtkAdjustment *adjustment, RsttoImageViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET (viewer);
    gdk_window_invalidate_rect (
            gtk_widget_get_window (widget),
            NULL,
            FALSE);
}

static void
cb_rstto_image_viewer_read_file_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GFile *file = G_FILE (source_object);
    RsttoImageViewerTransaction *transaction = (RsttoImageViewerTransaction *)user_data;

    GFileInputStream *file_input_stream = g_file_read_finish (file, result, NULL);

    if (file_input_stream == NULL)
    {
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
    RsttoImageViewerTransaction *transaction = (RsttoImageViewerTransaction *)user_data;
    gssize read_bytes = g_input_stream_read_finish (G_INPUT_STREAM (source_object), result, &transaction->error);

    if (read_bytes == -1)
    {
        gdk_pixbuf_loader_close (transaction->loader, NULL);
        return;
    }

    if (read_bytes > 0)
    {
        if(gdk_pixbuf_loader_write (transaction->loader, (const guchar *)transaction->buffer, read_bytes, &transaction->error) == FALSE)
        {
            /* Clean up the input-stream */
            g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
            g_object_unref(source_object);
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
        gdk_pixbuf_loader_close (transaction->loader, &transaction->error);

        /* Clean up the input-stream */
        g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
        g_object_unref(source_object);
    }
}


static void
cb_rstto_image_loader_area_prepared (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction)
{
    gint timeout = 0;
    RsttoImageViewer *viewer = transaction->viewer;

    if (viewer->priv->transaction == transaction)
    {
        if (viewer->priv->iter)
        {
            g_object_unref (viewer->priv->iter);
            viewer->priv->iter = NULL;
        }

        if (viewer->priv->pixbuf)
        {
            g_object_unref (viewer->priv->pixbuf);
            viewer->priv->pixbuf = NULL;
        }

        if (viewer->priv->animation)
        {
            g_object_unref (viewer->priv->animation);
            viewer->priv->animation = NULL;
        }

        viewer->priv->animation = gdk_pixbuf_loader_get_animation (loader);
        viewer->priv->iter = gdk_pixbuf_animation_get_iter (viewer->priv->animation, NULL);

        g_object_ref (viewer->priv->animation);

        timeout = gdk_pixbuf_animation_iter_get_delay_time (viewer->priv->iter);

        if (timeout > 0)
        {
            viewer->priv->animation_timeout_id = g_timeout_add(timeout, (GSourceFunc)cb_rstto_image_viewer_update_pixbuf, viewer);
        }   
        else
        {

            /* This is a single-frame image, there is no need to copy the pixbuf since it won't change.
             */
            viewer->priv->pixbuf = gdk_pixbuf_animation_iter_get_pixbuf (viewer->priv->iter);
            g_object_ref (viewer->priv->pixbuf);
        }
    }
}

static void
cb_rstto_image_loader_size_prepared (GdkPixbufLoader *loader, gint width, gint height, RsttoImageViewerTransaction *transaction)
{
    gint s_width = gdk_screen_get_width (default_screen);
    gint s_height = gdk_screen_get_height (default_screen);
    gboolean limit_quality = transaction->viewer->priv->limit_quality;

    /*
     * By default, the image-size won't be limited to screen-size (since it's smaller)
     * or, because we don't want to reduce it.
     * Set the image_scale to 1.0 (100%)
     */
    transaction->image_scale = 1.0;

    transaction->image_width = width;
    transaction->image_height = height;


    if (limit_quality == TRUE)
    {
        /*
         * Set the maximum size of the loaded image to the screen-size.
         * TODO: Add some 'smart-stuff' here
         */
        if (s_width < width || s_height < height)
        {
            /*
             * The image is loaded at the screen_size, calculate how this fits best.
             *  scale = MIN(width / screen_width, height / screen_height)
             *
             */
            if(((gdouble)width / (gdouble)s_width) < ((gdouble)height / (gdouble)s_height))
            {
                transaction->image_scale = (gdouble)s_width / (gdouble)width;
                gdk_pixbuf_loader_set_size (
                        loader,
                        s_width,
                        (gint)((gdouble)height/(gdouble)width*(gdouble)s_width)); 
            }
            else
            {
                transaction->image_scale = (gdouble)s_height / (gdouble)height;
                gdk_pixbuf_loader_set_size (
                        loader,
                        (gint)((gdouble)width/(gdouble)height*(gdouble)s_height),
                        s_height); 
            }
        }
        else
        {
            /*
             * Image-size won't be limited to screen-size (since it's smaller)
             * Set the image_scale to 1.0 (100%)
             */
            transaction->image_scale = 1.0;
        }
    }

    transaction->orientation = rstto_file_get_orientation (transaction->file);
}

static void
cb_rstto_image_loader_closed (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction)
{
    RsttoImageViewer *viewer = transaction->viewer;
    GtkWidget *widget = GTK_WIDGET(viewer);

    if (viewer->priv->transaction == transaction)
    {
        
        if (NULL == transaction->error)
        {
            gtk_widget_set_tooltip_text (GTK_WIDGET (viewer), NULL);
            viewer->priv->image_scale = transaction->image_scale;
            viewer->priv->image_width = transaction->image_width;
            viewer->priv->image_height = transaction->image_height;
            viewer->priv->orientation = transaction->orientation;
            set_scale (viewer, transaction->scale);
        }
        else
        {
            viewer->priv->image_scale = 1.0;
            viewer->priv->image_width = 1.0;
            viewer->priv->image_height = 1.0;
            if (viewer->priv->pixbuf)
            {
                g_object_unref (viewer->priv->pixbuf);
                viewer->priv->pixbuf = NULL;
            }

            gtk_widget_set_tooltip_text (GTK_WIDGET (viewer), transaction->error->message);
        }


        viewer->priv->error = transaction->error;
        transaction->error = NULL;
        viewer->priv->transaction = NULL;

        gdk_window_invalidate_rect (
                gtk_widget_get_window (widget),
                NULL,
                FALSE);
    }

    g_signal_emit_by_name(transaction->viewer, "size-ready");
    rstto_image_viewer_transaction_free (transaction);
}

static gboolean
cb_rstto_image_viewer_update_pixbuf (RsttoImageViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET(viewer);
    gint timeout = 0;

    if (viewer->priv->iter)
    {
        if(gdk_pixbuf_animation_iter_advance (viewer->priv->iter, NULL))
        {
            /* Cleanup old image */
            if (viewer->priv->pixbuf)
            {
                g_object_unref (viewer->priv->pixbuf);
                viewer->priv->pixbuf = NULL;
            }

            /* The pixbuf returned by the GdkPixbufAnimationIter might be reused, 
             * lets make a copy for myself just in case. Since it's a copy, we
             * own the only reference to it. There is no need to add another.
             */
            viewer->priv->pixbuf = gdk_pixbuf_copy (gdk_pixbuf_animation_iter_get_pixbuf (viewer->priv->iter));
        }

        timeout = gdk_pixbuf_animation_iter_get_delay_time (viewer->priv->iter);

        if (timeout > 0)
        {
            viewer->priv->animation_timeout_id = g_timeout_add(timeout, (GSourceFunc)cb_rstto_image_viewer_update_pixbuf, viewer);
        }

        gdk_window_invalidate_rect (
                gtk_widget_get_window (widget),
                NULL,
                FALSE);

        return FALSE;
    }
    return FALSE;
}

static gboolean
rstto_scroll_event (GtkWidget *widget, GdkEventScroll *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gboolean auto_scale = FALSE;
    gboolean invert_zoom_direction = viewer->priv->invert_zoom_direction;
    gdouble x_offset = viewer->priv->rendering.x_offset;
    gdouble y_offset = viewer->priv->rendering.y_offset;
    gdouble tmp_x = 0.0;
    gdouble tmp_y = 0.0;
    gdouble scale = viewer->priv->scale;
    gdouble v_scale;
    gdouble h_scale;
    GtkAllocation allocation;

    if (event->state & (GDK_CONTROL_MASK))
    {
        if ( NULL != viewer->priv->file )
        {
            viewer->priv->auto_scale = FALSE;

            tmp_x = (gdouble)(gtk_adjustment_get_value(viewer->hadjustment) + 
                    (gdouble)event->x - x_offset) / viewer->priv->scale;
            tmp_y = (gdouble)(gtk_adjustment_get_value(viewer->vadjustment) + 
                    (gdouble)event->y - y_offset) / viewer->priv->scale;

            switch (event->direction)
            {
                case GDK_SCROLL_UP:
                case GDK_SCROLL_LEFT:
                    scale = (invert_zoom_direction) ? viewer->priv->scale / 1.1 : viewer->priv->scale * 1.1;
                    break;
                case GDK_SCROLL_DOWN:
                case GDK_SCROLL_RIGHT:
                    scale = (invert_zoom_direction) ? viewer->priv->scale * 1.1 : viewer->priv->scale / 1.1;
                    break;
            }

            /*
             * If the image is larger then the screen-dimensions,
             * there is no reason to zoom in beyond RSTTO_MAX_SCALE.
             *
             * If the image is smaller then the screen-dimensions,
             * zoom in to RSTTO_MAX_SCALE OR the window-size, whichever
             * comes last.
             *
             */
            if (viewer->priv->image_scale < 1.0)
            {
                if (scale > RSTTO_MAX_SCALE)
                {
                    scale = RSTTO_MAX_SCALE;
                }
            }
            else
            {
                /*
                 * Assuming image_scale == 1.0
                 */

                gtk_widget_get_allocation (widget, &allocation);
                switch (viewer->priv->orientation)
                {
                    case RSTTO_IMAGE_ORIENT_90:
                    case RSTTO_IMAGE_ORIENT_270:
                        v_scale = (gdouble)(allocation.width) / (gdouble)viewer->priv->image_height;
                        h_scale = (gdouble)(allocation.height) / (gdouble)viewer->priv->image_width;
                        break;
                    case RSTTO_IMAGE_ORIENT_NONE:
                    case RSTTO_IMAGE_ORIENT_180:
                    default:
                        v_scale = (gdouble)(allocation.width) / (gdouble)viewer->priv->image_width;
                        h_scale = (gdouble)(allocation.height) / (gdouble)viewer->priv->image_height;
                        break;
                }

                if ((h_scale > RSTTO_MAX_SCALE) || (v_scale > RSTTO_MAX_SCALE))
                {
                    if(h_scale < v_scale)
                    {
                        /* If the image is scaled beyond the
                         * window-size, 
                         * force the scale to fit the window and set
                         * auto_scale = TRUE.
                         */
                        if (scale > h_scale)
                        {
                            auto_scale = TRUE;
                            scale = h_scale;
                        }
                    }
                    else
                    {
                        /* If the image is scaled beyond the
                         * window-size, 
                         * force the scale to fit the window and set
                         * auto_scale = TRUE.
                         */
                        if (scale > v_scale)
                        {
                            auto_scale = TRUE;
                            scale = v_scale;
                        }
                    }
                }
                else
                {
                    if (scale > RSTTO_MAX_SCALE)
                    {
                        scale = RSTTO_MAX_SCALE;
                    }

                }
            }

            /*
             * There is no need to zoom out beyond 64x64 pixels
             * unless, ofcourse the image itself is smaller then 64x64
             * pixels.
             */
            if (viewer->priv->image_width > viewer->priv->image_height)
            {
                if ((viewer->priv->image_width >= 64) && ((scale * viewer->priv->image_width) < 64))
                {
                    scale = (64.0 / (gdouble)viewer->priv->image_width);
                }
                if ((viewer->priv->image_width < 64) && (scale < 1.0))
                {
                    scale = 1.0; 
                }
            }
            else
            {
                if ((viewer->priv->image_height >= 64) && ((scale * viewer->priv->image_height) < 64))
                {
                    scale = (64.0 / (gdouble)viewer->priv->image_height);
                }
                if ((viewer->priv->image_height < 64) && (scale < 1.0))
                {
                    scale = 1.0; 
                }
            }

            viewer->priv->auto_scale = auto_scale;
            viewer->priv->scale = scale;

            g_object_freeze_notify(G_OBJECT(viewer->hadjustment));
            g_object_freeze_notify(G_OBJECT(viewer->vadjustment));

            gtk_adjustment_set_upper (
                    viewer->hadjustment,
                    floor((gdouble)viewer->priv->image_width*viewer->priv->scale));
            gtk_adjustment_set_value (
                    viewer->hadjustment,
                    (tmp_x * scale - event->x));

            gtk_adjustment_set_upper (
                    viewer->vadjustment,
                    floor((gdouble)viewer->priv->image_height*viewer->priv->scale));
            gtk_adjustment_set_value (
                    viewer->vadjustment,
                    (tmp_y * scale - event->y));

            /*
             * Enable signals on the adjustments.
             */
            g_object_thaw_notify(G_OBJECT(viewer->vadjustment));
            g_object_thaw_notify(G_OBJECT(viewer->hadjustment));


            /* Invalidate the entire window */
            gdk_window_invalidate_rect (
                    gtk_widget_get_window (widget),
                    NULL,
                    FALSE); 

            g_signal_emit_by_name(viewer, "scale-changed");
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean 
rstto_motion_notify_event (GtkWidget *widget, GdkEventMotion *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    if (event->state & GDK_BUTTON1_MASK)
    {
        viewer->priv->motion.current_x = event->x;
        viewer->priv->motion.current_y = event->y;

        switch (viewer->priv->motion.state)
        {
            case RSTTO_IMAGE_VIEWER_MOTION_STATE_MOVE:
                if (viewer->priv->motion.x != viewer->priv->motion.current_x)
                {
                    gint val = viewer->hadjustment->value;
                    viewer->hadjustment->value = viewer->priv->motion.h_val + (viewer->priv->motion.x - viewer->priv->motion.current_x);
                    if((viewer->hadjustment->value + viewer->hadjustment->page_size) > viewer->hadjustment->upper)
                    {
                        viewer->hadjustment->value = viewer->hadjustment->upper - viewer->hadjustment->page_size;
                    }
                    if((viewer->hadjustment->value) < viewer->hadjustment->lower)
                    {
                        viewer->hadjustment->value = viewer->hadjustment->lower;
                    }
                    if (val != viewer->hadjustment->value)
                        gtk_adjustment_value_changed(viewer->hadjustment);
                }

                if (viewer->priv->motion.y != viewer->priv->motion.current_y)
                {
                    gint val = viewer->vadjustment->value;
                    viewer->vadjustment->value = viewer->priv->motion.v_val + (viewer->priv->motion.y - viewer->priv->motion.current_y);
                    if((viewer->vadjustment->value + viewer->vadjustment->page_size) > viewer->vadjustment->upper)
                    {
                        viewer->vadjustment->value = viewer->vadjustment->upper - viewer->vadjustment->page_size;
                    }
                    if((viewer->vadjustment->value) < viewer->vadjustment->lower)
                    {
                        viewer->vadjustment->value = viewer->vadjustment->lower;
                    }
                    if (val != viewer->vadjustment->value)
                        gtk_adjustment_value_changed(viewer->vadjustment);
                }
                break;
            case RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM:
                /* TODO: Calculate the rectangle to invalidate.
                 * 
                 * It should be a rectangle covering both the original
                 * selection-box and the new one.
                 */
                gdk_window_invalidate_rect (
                        gtk_widget_get_window (widget),
                        NULL,
                        FALSE); 

                /* Only change the cursor when hovering over the image
                 */
                if ( (event->x > viewer->priv->rendering.x_offset) &&
                     (event->y > viewer->priv->rendering.y_offset) &&
                     (event->y < (viewer->priv->rendering.y_offset + viewer->priv->rendering.height)) &&
                     (event->x < (viewer->priv->rendering.x_offset + viewer->priv->rendering.width)))
                {
                    GdkCursor *cursor = gdk_cursor_new(GDK_UL_ANGLE);
                    gdk_window_set_cursor(gtk_widget_get_window (widget), cursor);
                    gdk_cursor_unref(cursor);
                }
                else
                {
                    /* Set back to default when moving over the
                     * background.
                     */
                    gdk_window_set_cursor(gtk_widget_get_window (widget), NULL);
                }
                break;
            default:
                break;
        }
    }
    return FALSE;
}

static gboolean
rstto_button_press_event (GtkWidget *widget, GdkEventButton *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    if(event->button == 1)
    {
        viewer->priv->motion.x = event->x;
        viewer->priv->motion.y = event->y;
        viewer->priv->motion.current_x = event->x;
        viewer->priv->motion.current_y = event->y;
        viewer->priv->motion.h_val = viewer->hadjustment->value;
        viewer->priv->motion.v_val = viewer->vadjustment->value;

        if (viewer->priv->file != NULL )
        {
            if (!(event->state & (GDK_CONTROL_MASK)))
            {
                if ( (event->x > viewer->priv->rendering.x_offset) &&
                     (event->y > viewer->priv->rendering.y_offset) &&
                     (event->y < (viewer->priv->rendering.y_offset + viewer->priv->rendering.height)) &&
                     (event->x < (viewer->priv->rendering.x_offset + viewer->priv->rendering.width)))
                {
                    GdkCursor *cursor = gdk_cursor_new(GDK_FLEUR);
                    gdk_window_set_cursor(gtk_widget_get_window (widget), cursor);
                    gdk_cursor_unref(cursor);
                    rstto_image_viewer_set_motion_state (viewer, RSTTO_IMAGE_VIEWER_MOTION_STATE_MOVE);
                }
            }

            if (event->state & GDK_CONTROL_MASK)
            {
                /* Only change the cursor when hovering over the image
                 */
                if ( (event->x > viewer->priv->rendering.x_offset) &&
                     (event->y > viewer->priv->rendering.y_offset) &&
                     (event->y < (viewer->priv->rendering.y_offset + viewer->priv->rendering.height)) &&
                     (event->x < (viewer->priv->rendering.x_offset + viewer->priv->rendering.width)))
                {
                    GdkCursor *cursor = gdk_cursor_new(GDK_UL_ANGLE);
                    gdk_window_set_cursor(gtk_widget_get_window (widget), cursor);
                    gdk_cursor_unref(cursor);
                }

                /* Set the zoom-state even if not hovering over the
                 * image, this allows for easier selection.
                 * Dragging from / to somewhere outside the image to
                 * make sure the border is selected too.
                 */
                rstto_image_viewer_set_motion_state (viewer, RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM);
            }
        }
        return TRUE;
    }
    if(event->button == 3)
    {
        if (viewer->priv->menu)
        {
            gtk_widget_show_all(GTK_WIDGET(viewer->priv->menu));
            gtk_menu_popup (
                    viewer->priv->menu,
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    3,
                    event->time);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean
rstto_button_release_event (GtkWidget *widget, GdkEventButton *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gdouble box_y = 0.0;
    gdouble box_x = 0.0;
    gdouble box_width = 0.0;
    gdouble box_height = 0.0;
    gdouble x_offset = viewer->priv->rendering.x_offset;
    gdouble y_offset = viewer->priv->rendering.y_offset;
    gdouble image_width = viewer->priv->rendering.width;
    gdouble image_height = viewer->priv->rendering.height;
    gdouble scale = viewer->priv->scale;
    gdouble tmp_x = 0.0;
    gdouble tmp_y = 0.0;

    gdk_window_set_cursor (gtk_widget_get_window (widget), NULL);

    switch (viewer->priv->motion.state)
    {
        case RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM:
            /* A selection-box can be created moving the cursor from
             * left to right, aswell as from right to left.
             * 
             * Calculate the box dimensions accordingly.
             */
            if (viewer->priv->motion.y < viewer->priv->motion.current_y)
            {
                box_y = (gdouble)viewer->priv->motion.y;
                box_height = (gdouble)viewer->priv->motion.current_y - box_y;
            }
            else
            {
                box_y = (gdouble)viewer->priv->motion.current_y;
                box_height = (gdouble)viewer->priv->motion.y - box_y;
            }

            /* A selection-box can be created moving the cursor from
             * top to bottom, aswell as from bottom to top.
             * 
             * Calculate the box dimensions accordingly.
             */
            if (viewer->priv->motion.x < viewer->priv->motion.current_x)
            {
                box_x = (gdouble)viewer->priv->motion.x;
                box_width = (gdouble)viewer->priv->motion.current_x - box_x;
            }
            else
            {
                box_x = (gdouble)viewer->priv->motion.current_x;
                box_width = (gdouble)viewer->priv->motion.x - box_x;
            }

            /*
             * Constrain the selection-box to the left
             * and top sides of the image.
             */
            if (box_x < x_offset)
            {
                box_width = box_width - (x_offset - box_x);
                box_x = x_offset;
            }
            if (box_y < y_offset)
            {
                box_height = box_height - (y_offset - box_y);
                box_y = y_offset;
            }

            if ((x_offset + image_width) < (box_x + box_width))
            {
                box_width = (x_offset + image_width) - box_x - 1;
            }
            if ((y_offset + image_height) < (box_y + box_height))
            {
                box_height = (y_offset + image_height) - box_y - 1;
            }
            
            if ( box_width > 0 && box_height > 0 )
            {
                /* Set auto_scale to false, we are going manual */
                viewer->priv->auto_scale = FALSE;

                if (scale == RSTTO_MAX_SCALE)
                    break;

                /*
                 * Calculate the center of the selection-box.
                 */
                tmp_y = (gtk_adjustment_get_value(viewer->vadjustment) + (gdouble)box_y +
                        ((gdouble)box_height/ 2) - viewer->priv->rendering.y_offset) / viewer->priv->scale;
                tmp_x = (gtk_adjustment_get_value(viewer->hadjustment) + (gdouble)box_x +
                        ((gdouble)box_width/ 2) - viewer->priv->rendering.x_offset) / viewer->priv->scale;

                /*
                 * Calculate the new scale
                 */
                if ((gtk_adjustment_get_page_size(viewer->hadjustment) / box_width) < 
                    (gtk_adjustment_get_page_size(viewer->vadjustment) / box_height))
                {
                    scale = viewer->priv->scale * (gtk_adjustment_get_page_size(viewer->hadjustment) / box_width);
                }
                else
                {
                    scale = viewer->priv->scale * (gtk_adjustment_get_page_size(viewer->vadjustment) / box_height);
                }

                /*
                 * Prevent the widget from zooming in beyond the
                 * MAX_SCALE.
                 */
                if (scale > RSTTO_MAX_SCALE)
                {
                    scale = RSTTO_MAX_SCALE;
                }

                viewer->priv->scale = scale;

                /*
                 * Prevent the adjustments from emitting the
                 * 'changed' signal,
                 * this way both the upper-limit and value can
                 * be changed before the
                 * rest of the application is informed.
                 */
                g_object_freeze_notify(G_OBJECT(viewer->hadjustment));
                g_object_freeze_notify(G_OBJECT(viewer->vadjustment));

                gtk_adjustment_set_upper (
                        viewer->hadjustment,
                        floor((gdouble)viewer->priv->image_width*viewer->priv->scale));
                gtk_adjustment_set_value (
                        viewer->hadjustment,
                        (tmp_x * scale - ((gdouble)gtk_adjustment_get_page_size(viewer->hadjustment)/2)));

                gtk_adjustment_set_upper (
                        viewer->vadjustment,
                        floor((gdouble)viewer->priv->image_height*viewer->priv->scale));
                gtk_adjustment_set_value (
                        viewer->vadjustment,
                        (tmp_y * scale - ((gdouble)gtk_adjustment_get_page_size(viewer->vadjustment)/2)));

                /*
                 * Enable signals on the adjustments.
                 */
                g_object_thaw_notify(G_OBJECT(viewer->vadjustment));
                g_object_thaw_notify(G_OBJECT(viewer->hadjustment));

                /*
                 * Trigger the 'changed' signal, update the rest
                 * of
                 * the appliaction.
                 */
                gtk_adjustment_changed(viewer->hadjustment);
                gtk_adjustment_changed(viewer->vadjustment);

                g_signal_emit_by_name(viewer, "scale-changed");
            }
            break;
        default:
            break;
    }
    rstto_image_viewer_set_motion_state (viewer, RSTTO_IMAGE_VIEWER_MOTION_STATE_NORMAL);
    gdk_window_invalidate_rect (
            gtk_widget_get_window (widget),
            NULL,
            FALSE);
    return FALSE;
}

static void
cb_rstto_limit_quality_changed (GObject *settings, GParamSpec *pspec, gpointer user_data)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (user_data);

    GValue val_limit_quality = {0, };

    g_value_init (&val_limit_quality, G_TYPE_BOOLEAN);

    g_object_get_property (
            G_OBJECT(viewer->priv->settings),
            "limit-quality",
            &val_limit_quality);

    viewer->priv->limit_quality = g_value_get_boolean (
            &val_limit_quality);

    if ( NULL != viewer->priv->file )
    {
        rstto_image_viewer_load_image (
                viewer,
                viewer->priv->file,
                viewer->priv->scale);
    }
}

static void
cb_rstto_bgcolor_changed (GObject *settings, GParamSpec *pspec, gpointer user_data)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (user_data);
    GtkWidget *widget = GTK_WIDGET (user_data);

    GValue val_bg_color = {0, };
    GValue val_bg_color_override = {0, };
    GValue val_bg_color_fs = {0, };

    g_value_init (&val_bg_color, GDK_TYPE_COLOR);
    g_value_init (&val_bg_color_fs, GDK_TYPE_COLOR);
    g_value_init (&val_bg_color_override, G_TYPE_BOOLEAN);

    g_object_get_property (
            G_OBJECT(viewer->priv->settings),
            "bgcolor",
            &val_bg_color);
    g_object_get_property (
            G_OBJECT(viewer->priv->settings),
            "bgcolor-override",
            &val_bg_color_override);
    g_object_get_property (
            G_OBJECT(viewer->priv->settings),
            "bgcolor-fullscreen",
            &val_bg_color_fs);

    if (TRUE == g_value_get_boolean (&val_bg_color_override))
    {
        viewer->priv->bg_color = g_value_get_boxed (&val_bg_color);
    }
    else
    {
        viewer->priv->bg_color = &(gtk_widget_get_style (widget)->bg[GTK_STATE_NORMAL]);
    }
    viewer->priv->bg_color_fs = g_value_get_boxed (&val_bg_color_fs);

    gdk_window_invalidate_rect (
            gtk_widget_get_window (widget),
            NULL,
            FALSE);
}

static void
cb_rstto_zoom_direction_changed (GObject *settings, GParamSpec *pspec, gpointer user_data)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (user_data);
    viewer->priv->invert_zoom_direction = rstto_settings_get_boolean_property (RSTTO_SETTINGS (settings), "invert-zoom-direction");
}
 
static gboolean
rstto_popup_menu (GtkWidget *widget)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    if (viewer->priv->menu)
    {
        gtk_widget_show_all (GTK_WIDGET (viewer->priv->menu));
        gtk_menu_popup (viewer->priv->menu,
                        NULL, NULL, NULL, NULL, 0,
                        gtk_get_current_event_time());
        return TRUE;
    }
    return FALSE;
}

static void
cb_rstto_image_viewer_dnd (GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data,
                           guint info, guint time_, RsttoImageViewer *viewer)
{
    g_return_if_fail ( RSTTO_IS_IMAGE_VIEWER(viewer) );

    if ((data->length >= 0) && (data->format == 8))
    {
        gchar **uris;

        uris = g_uri_list_extract_uris ((const gchar*)data->data);

        g_signal_emit_by_name (viewer, "files-dnd", uris);

        gtk_drag_finish (context, TRUE, FALSE, time_);
        g_strfreev (uris);
    }
    else
    {
        gtk_drag_finish (context, FALSE, FALSE, time_);
    }
}

GError *
rstto_image_viewer_get_error (RsttoImageViewer *viewer)
{
    if (viewer->priv->error)
    {
        return g_error_copy (viewer->priv->error);
    }
    return NULL;
}

static void
rstto_image_viewer_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (object);

    switch (property_id)
    {
        case PROP_SHOW_CLOCK:
            viewer->priv->props.show_clock = g_value_get_boolean (value);
            break;
    }
}

static void
rstto_image_viewer_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (object);

    switch (property_id)
    {
        case PROP_SHOW_CLOCK:
            g_value_set_boolean (value, viewer->priv->props.show_clock);
            break;
    }
}

static gboolean
cb_rstto_image_viewer_refresh (RsttoImageViewer *viewer)
{
    GtkWidget *widget = GTK_WIDGET (viewer);

    gdk_window_invalidate_rect (
            gtk_widget_get_window (widget),
            NULL,
            FALSE);

    return TRUE;
}

void
rstto_image_viewer_set_show_clock (RsttoImageViewer *viewer, gboolean value)
{
    viewer->priv->props.show_clock = value;
    if (viewer->priv->props.show_clock)
    {
        viewer->priv->refresh_timeout_id = g_timeout_add (
                15000,
                (GSourceFunc)cb_rstto_image_viewer_refresh, viewer);
    }
    else
    {
        if (viewer->priv->refresh_timeout_id)
        {
            g_source_remove (viewer->priv->refresh_timeout_id);
            viewer->priv->refresh_timeout_id = 0;
        }
    }
}

gboolean
rstto_image_viewer_is_busy (RsttoImageViewer *viewer )
{
    if (viewer->priv->transaction != NULL)
    {
        return TRUE;
    }
    return FALSE;
}

static void
cb_rstto_image_viewer_file_changed (RsttoFile *r_file, RsttoImageViewer *viewer )
{
    rstto_image_viewer_load_image (
            viewer,
            r_file,
            viewer->priv->scale);

    g_signal_emit_by_name(viewer, "status-changed");
}

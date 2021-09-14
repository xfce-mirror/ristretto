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

#include <math.h>

#include <gio/gio.h>

#include "util.h"
#include "image_viewer.h"
#include "settings.h"
#include "main_window.h"



/* Do not make this buffer too large,
 * this breaks some pixbufloaders.
 */
#ifndef RSTTO_IMAGE_VIEWER_BUFFER_SIZE
#define RSTTO_IMAGE_VIEWER_BUFFER_SIZE 4096
#endif

#ifndef BACKGROUND_ICON_NAME
#define BACKGROUND_ICON_NAME "org.xfce.ristretto"
#endif

#ifndef BACKGROUND_ICON_SIZE
#define BACKGROUND_ICON_SIZE 128
#endif

#define MAX_OVER_VISIBLE 1.5
#define MIN_VIEW_PERCENT 0.1
#define RSTTO_LINE_WIDTH 1.0

enum
{
    PROP_0,
    PROP_SHOW_CLOCK,

    /* For scrollable interface */
    PROP_HADJUSTMENT,
    PROP_VADJUSTMENT,
    PROP_HSCROLL_POLICY,
    PROP_VSCROLL_POLICY
};

typedef enum
{
    RSTTO_IMAGE_VIEWER_MOTION_STATE_NORMAL = 0,
    RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM,
    RSTTO_IMAGE_VIEWER_MOTION_STATE_MOVE
} RsttoImageViewerMotionState;

static GdkScreen *default_screen = NULL;

typedef struct _RsttoImageViewerTransaction RsttoImageViewerTransaction;



#define RSTTO_GET_CLOCK_WIDTH(w, h) (40 + MIN (w, h) * 0.07)
#define RSTTO_GET_CLOCK_OFFSET(w)   (w * 0.15);

static void
rstto_image_viewer_finalize (GObject *object);
static void
rstto_image_viewer_set_property (GObject *object,
                                 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec);
static void
rstto_image_viewer_get_property (GObject *object,
                                 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec);


static gboolean
rstto_image_viewer_draw (GtkWidget *widget,
                         cairo_t *ctx);
static void
rstto_image_viewer_realize (GtkWidget *widget);
static void
rstto_image_viewer_get_preferred_width (GtkWidget *widget,
                                        gint *minimal_width,
                                        gint *natural_width);
static void
rstto_image_viewer_get_preferred_height (GtkWidget *widget,
                                         gint *minimal_height,
                                         gint *natural_height);
static void
rstto_image_viewer_size_allocate (GtkWidget *widget,
                                  GtkAllocation *allocation);
static gboolean
rstto_scroll_event (GtkWidget *widget,
                    GdkEventScroll *event);
static gboolean
rstto_button_press_event (GtkWidget *widget,
                          GdkEventButton *event);
static gboolean
rstto_button_release_event (GtkWidget *widget,
                            GdkEventButton *event);
static gboolean
rstto_motion_notify_event (GtkWidget *widget,
                           GdkEventMotion *event);
static gboolean
rstto_popup_menu (GtkWidget *widget);


static gboolean
set_scale (RsttoImageViewer *viewer,
           gdouble scale);
static void
set_adjustments (RsttoImageViewer *viewer,
                 gdouble h_value,
                 gdouble v_value);
static void
paint_background (GtkWidget *widget,
                  cairo_t *ctx);
static void
paint_background_icon (GtkWidget *widget,
                       cairo_t *ctx);
static void
rstto_image_viewer_paint (GtkWidget *widget,
                          cairo_t *ctx);
static void
cb_rstto_image_viewer_file_changed (RsttoFile *r_file,
                                    RsttoImageViewer *viewer);
static void
rstto_image_viewer_set_motion_state (RsttoImageViewer *viewer,
                                     RsttoImageViewerMotionState state);
static void
cb_rstto_image_viewer_read_file_ready (GObject *source_object,
                                       GAsyncResult *result,
                                       gpointer user_data);
static void
cb_rstto_image_viewer_read_input_stream_ready (GObject *source_object,
                                               GAsyncResult *result,
                                               gpointer user_data);
static void
cb_rstto_image_loader_image_ready (GdkPixbufLoader *loader,
                                   RsttoImageViewerTransaction *transaction);
static void
cb_rstto_image_loader_size_prepared (GdkPixbufLoader *loader,
                                     gint width,
                                     gint height,
                                     RsttoImageViewerTransaction *transaction);
static void
cb_rstto_image_loader_closed (GdkPixbufLoader *loader,
                              RsttoImageViewerTransaction *transaction);
static gboolean
cb_rstto_image_viewer_update_pixbuf (gpointer user_data);
static void
cb_rstto_image_viewer_dnd (GtkWidget *widget,
                           GdkDragContext *context,
                           gint x,
                           gint y,
                           GtkSelectionData *data,
                           guint info,
                           guint time_,
                           RsttoImageViewer *viewer);
static void
cb_rstto_limit_quality_changed (GObject *settings,
                                GParamSpec *pspec,
                                gpointer user_data);
static void
cb_rstto_bgcolor_changed (GObject *settings,
                          GParamSpec *pspec,
                          gpointer user_data);
static void
cb_rstto_zoom_direction_changed (GObject *settings,
                                 GParamSpec *pspec,
                                 gpointer user_data);
static void
rstto_image_viewer_load_image (RsttoImageViewer *viewer,
                               RsttoFile *file,
                               gdouble scale);
static void
rstto_image_viewer_transaction_free (RsttoImageViewerTransaction *tr);



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

struct _RsttoImageViewerPrivate
{
    RsttoFile                   *file;
    RsttoSettings               *settings;

    GtkIconTheme                *icon_theme;
    GdkPixbuf                   *missing_icon;
    GdkPixbuf                   *bg_icon;
    GdkRGBA                     *bg_color;
    GdkRGBA                     *bg_color_fs;

    gboolean                     limit_quality;

    GError                      *error;

    guint                        hscroll_policy : 1;
    guint                        vscroll_policy : 1;

    RsttoImageViewerTransaction *transaction;
    RsttoImageOrientation        orientation;
    struct
    {
        cairo_pattern_t *pattern;
        gboolean has_alpha;
        gint width;
        gint height;
    } pixbuf;

    struct
    {
        gint x_offset;
        gint y_offset;
        gint width;
        gint height;
    } rendering;

    struct
    {
        gboolean show_clock;
    } props;

    GtkMenu                     *menu;
    gboolean                     invert_zoom_direction;

    /* Animation data for animated images (like .gif/.mng) */
    /*******************************************************/
    GdkPixbufAnimationIter *iter;
    guint                   animation_id;
    gdouble                 scale;
    gboolean                auto_scale;
    GtkAdjustment          *vadjustment;
    GtkAdjustment          *hadjustment;

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
};



G_DEFINE_TYPE_WITH_CODE (RsttoImageViewer, rstto_image_viewer, GTK_TYPE_WIDGET,
                         G_ADD_PRIVATE (RsttoImageViewer)
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_SCROLLABLE, NULL))



static void
rstto_image_viewer_init (RsttoImageViewer *viewer)
{
    if (default_screen == NULL)
    {
        default_screen = gdk_screen_get_default ();
    }

    viewer->priv = rstto_image_viewer_get_instance_private (viewer);
    viewer->priv->settings = rstto_settings_new ();
    viewer->priv->bg_color = NULL;
    viewer->priv->bg_color_fs = NULL;
    viewer->priv->pixbuf.pattern = NULL;
    viewer->priv->animation_id = 0;
    viewer->priv->image_width = 0;
    viewer->priv->image_height = 0;

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
    if (viewer->priv->bg_icon != NULL)
    {
        gdk_pixbuf_saturate_and_pixelate (
                viewer->priv->bg_icon,
                viewer->priv->bg_icon,
                0, FALSE);
    }

    g_signal_connect (viewer->priv->settings, "notify::bgcolor",
                      G_CALLBACK (cb_rstto_bgcolor_changed), viewer);

    g_signal_connect (viewer->priv->settings, "notify::bgcolor-override",
                      G_CALLBACK (cb_rstto_bgcolor_changed), viewer);
    g_signal_connect (viewer->priv->settings, "notify::limit-quality",
                      G_CALLBACK (cb_rstto_limit_quality_changed), viewer);

    g_signal_connect (viewer->priv->settings, "notify::invert-zoom-direction",
                      G_CALLBACK (cb_rstto_zoom_direction_changed), viewer);
    g_signal_connect (viewer, "drag-data-received",
                      G_CALLBACK (cb_rstto_image_viewer_dnd), viewer);

    gtk_widget_set_events (GTK_WIDGET (viewer),
                           GDK_BUTTON_PRESS_MASK |
                           GDK_BUTTON_RELEASE_MASK |
                           GDK_BUTTON1_MOTION_MASK |
                           GDK_ENTER_NOTIFY_MASK |
                           GDK_POINTER_MOTION_MASK |
                           GDK_SCROLL_MASK);

    gtk_drag_dest_set (GTK_WIDGET (viewer), GTK_DEST_DEFAULT_ALL, NULL, 0,
                      GDK_ACTION_COPY | GDK_ACTION_LINK | GDK_ACTION_MOVE | GDK_ACTION_PRIVATE);
    gtk_drag_dest_add_uri_targets (GTK_WIDGET (viewer));
}

/**
 * rstto_image_viewer_class_init:
 * @viewer_class:
 *
 * Initialize imageviewer class
 */
static void
rstto_image_viewer_class_init (RsttoImageViewerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

    object_class->finalize = rstto_image_viewer_finalize;
    object_class->set_property = rstto_image_viewer_set_property;
    object_class->get_property = rstto_image_viewer_get_property;

    widget_class->draw = rstto_image_viewer_draw;
    widget_class->realize = rstto_image_viewer_realize;
    widget_class->get_preferred_width = rstto_image_viewer_get_preferred_width;
    widget_class->get_preferred_height = rstto_image_viewer_get_preferred_height;
    widget_class->size_allocate = rstto_image_viewer_size_allocate;
    widget_class->scroll_event = rstto_scroll_event;
    widget_class->button_press_event = rstto_button_press_event;
    widget_class->button_release_event = rstto_button_release_event;
    widget_class->motion_notify_event = rstto_motion_notify_event;
    widget_class->popup_menu = rstto_popup_menu;

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

    g_object_class_install_property (
            G_OBJECT_CLASS (object_class),
            PROP_SHOW_CLOCK,
            g_param_spec_boolean (
                    "show-clock",
                    "",
                    "",
                    FALSE,
                    G_PARAM_READWRITE));

    /* Scrollable interface properties */
    g_object_class_override_property (
            object_class,
            PROP_HADJUSTMENT,
            "hadjustment");
    g_object_class_override_property (
            object_class,
            PROP_VADJUSTMENT,
            "vadjustment");
    g_object_class_override_property (
            object_class,
            PROP_HSCROLL_POLICY,
            "hscroll-policy");
    g_object_class_override_property (
            object_class,
            PROP_VSCROLL_POLICY,
            "vscroll-policy");
}

/**
 * rstto_image_viewer_realize:
 * @widget:
 *
 */
static void
rstto_image_viewer_realize (GtkWidget *widget)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    GtkAllocation allocation;
    GdkWindowAttr attributes;
    GdkWindow *window;
    gint attributes_mask;
    gboolean bg_color_override;

    g_return_if_fail (widget != NULL);
    g_return_if_fail (RSTTO_IS_IMAGE_VIEWER (widget));

    gtk_widget_set_realized (widget, TRUE);

    gtk_widget_get_allocation (widget, &allocation);
    attributes.x = allocation.x;
    attributes.y = allocation.y;
    attributes.width = allocation.width;
    attributes.height = allocation.height;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events (widget) | GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK;
    attributes.visual = gtk_widget_get_visual (widget);

    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;
    window = gdk_window_new (gtk_widget_get_parent_window (widget), &attributes, attributes_mask);
    gtk_widget_set_window (widget, window);
    gdk_window_set_user_data (window, widget);
    g_object_ref (window);

    g_object_get (viewer->priv->settings, "bgcolor-fullscreen",
                  &(viewer->priv->bg_color_fs), NULL);
    g_object_get (viewer->priv->settings, "limit-quality",
                  &(viewer->priv->limit_quality), NULL);
    g_object_get (viewer->priv->settings, "invert-zoom-direction",
                  &(viewer->priv->invert_zoom_direction), NULL);

    g_object_get (viewer->priv->settings, "bgcolor-override", &bg_color_override, NULL);
    if (bg_color_override)
        g_object_get (viewer->priv->settings, "bgcolor", &(viewer->priv->bg_color), NULL);
    else
        viewer->priv->bg_color = NULL;
}

/**
 * rstto_image_viewer_get_preferred_width / height:
 *
 * Request a default size of 300 by 400 pixels
 */
static void
rstto_image_viewer_get_preferred_width (GtkWidget *widget, gint *minimal_width, gint *natural_width)
{
    *minimal_width = 100;
    *natural_width = 400;
}

static void
rstto_image_viewer_get_preferred_height (GtkWidget *widget, gint *minimal_height, gint *natural_height)
{
    *minimal_height = 100;
    *natural_height = 300;
}

/**
 * rstto_image_viewer_size_allocate:
 * @widget:
 * @allocation:
 *
 */
static void
rstto_image_viewer_size_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    gtk_widget_set_allocation (widget, allocation);
    if (gtk_widget_get_realized (widget))
    {
        gdk_window_move_resize (gtk_widget_get_window (widget), allocation->x, allocation->y,
                                allocation->width, allocation->height);

        if (viewer->priv->auto_scale)
            set_scale (viewer, RSTTO_SCALE_FIT_TO_VIEW);

        set_adjustments (viewer,
                         gtk_adjustment_get_value (viewer->priv->hadjustment),
                         gtk_adjustment_get_value (viewer->priv->vadjustment));
    }
}

/**
 * rstto_image_viewer_draw:
 * @widget:
 * @cairo_t:
 *
 */
static gboolean
rstto_image_viewer_draw (GtkWidget *widget, cairo_t *ctx)
{
    cairo_save (ctx);

    rstto_image_viewer_paint (widget, ctx);

    cairo_restore (ctx);

    return TRUE;
}

static void
rstto_image_viewer_finalize (GObject *object)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (object);

    if (viewer->priv->settings)
    {
        g_object_unref (viewer->priv->settings);
        viewer->priv->settings = NULL;
    }
    if (viewer->priv->bg_color)
    {
        gdk_rgba_free (viewer->priv->bg_color);
        viewer->priv->bg_color = NULL;
    }
    if (viewer->priv->bg_color_fs)
    {
        gdk_rgba_free (viewer->priv->bg_color_fs);
        viewer->priv->bg_color_fs = NULL;
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
    if (viewer->priv->pixbuf.pattern)
    {
        cairo_pattern_destroy (viewer->priv->pixbuf.pattern);
        viewer->priv->pixbuf.pattern = NULL;
    }
    if (viewer->priv->iter)
    {
        g_object_unref (viewer->priv->iter);
        viewer->priv->iter = NULL;
    }

    G_OBJECT_CLASS (rstto_image_viewer_parent_class)->finalize (object);
}

static gdouble
scale_get_max (RsttoImageViewer *viewer)
{
    GtkAllocation allocation;
    gdouble max_scale;

    gtk_widget_get_allocation (GTK_WIDGET (viewer), &allocation);
    max_scale = MAX_OVER_VISIBLE
                * MAX (allocation.width, allocation.height)
                / MIN (viewer->priv->image_width, viewer->priv->image_height);

    return MAX (max_scale, 1.0);
}

/**
 * Set scale.
 * @viewer: the image viewer structure
 * @scale: 0 means 'fit to view', 1 means 100%/1:1 scale
 *
 * Return value: FALSE if @scale was out of bounds and no changes were applied,
 *               TRUE otherwise (always TRUE id @scale == RSTTO_SCALE_FIT_TO_VIEW)
 */
static gboolean
set_scale (RsttoImageViewer *viewer, gdouble scale)
{
    gboolean auto_scale = FALSE;
    gdouble in_scale = scale, v_scale, h_scale;
    GtkAllocation allocation;

    gtk_widget_get_allocation (GTK_WIDGET (viewer), &allocation);

    switch (viewer->priv->orientation)
    {
        case RSTTO_IMAGE_ORIENT_90:
        case RSTTO_IMAGE_ORIENT_270:
        case RSTTO_IMAGE_ORIENT_FLIP_TRANSVERSE:
        case RSTTO_IMAGE_ORIENT_FLIP_TRANSPOSE:
            v_scale = (gdouble) allocation.width / viewer->priv->image_height;
            h_scale = (gdouble) allocation.height / viewer->priv->image_width;
            break;
        case RSTTO_IMAGE_ORIENT_NONE:
        case RSTTO_IMAGE_ORIENT_180:
        case RSTTO_IMAGE_ORIENT_FLIP_HORIZONTAL:
        case RSTTO_IMAGE_ORIENT_FLIP_VERTICAL:
        default:
            v_scale = (gdouble) allocation.width / viewer->priv->image_width;
            h_scale = (gdouble) allocation.height / viewer->priv->image_height;
            break;
    }

    if (scale == RSTTO_SCALE_IMAGE_LOADING)
    {
        if (h_scale > 1 && v_scale > 1)
        {
            /* for small images fitting scale to 1:1 size, others fit-to-view */
            scale = RSTTO_SCALE_REAL_SIZE;
        }
        else
        {
            /* for large images exceeding window size, fit-to-view */
            scale = RSTTO_SCALE_FIT_TO_VIEW;
        }
    }

    if (scale < RSTTO_SCALE_REAL_SIZE)
    {
        if (scale == RSTTO_SCALE_FIT_TO_VIEW)
        {
            auto_scale = TRUE;
            scale = MIN (h_scale, v_scale);
        }
        else /* minimum scale is a percent of display area, unless image is smaller */
        {
            if ((scale * MAX (viewer->priv->image_width, viewer->priv->image_height))
                < (MIN_VIEW_PERCENT * MIN (allocation.width, allocation.height)))
            {
                scale = viewer->priv->scale;
            }
        }
    }
    else
        scale = MIN (scale_get_max (viewer), scale);

    viewer->priv->auto_scale = auto_scale;
    if (viewer->priv->scale != scale)
    {
        viewer->priv->scale = scale;
        g_signal_emit_by_name (viewer, "scale-changed");
    }

    return scale == in_scale || in_scale == RSTTO_SCALE_FIT_TO_VIEW;
}

static void
set_adjustments (RsttoImageViewer *viewer,
                 gdouble h_value,
                 gdouble v_value)
{
    GtkAllocation alloc;
    gdouble width, height;

    gtk_widget_get_allocation (GTK_WIDGET (viewer), &alloc);

    /* if there is no usable image, reset values */
    if (viewer->priv->image_width == 0)
    {
        viewer->priv->rendering.x_offset = 0;
        viewer->priv->rendering.y_offset = 0;
        viewer->priv->rendering.width = 0;
        viewer->priv->rendering.height = 0;

        gtk_adjustment_configure (viewer->priv->hadjustment, 0, 0, 0, 0, 0, 0);
        gtk_adjustment_configure (viewer->priv->vadjustment, 0, 0, 0, 0, 0, 0);

        return;
    }

    /* set rendering geometry */
    switch (viewer->priv->orientation)
    {
        case RSTTO_IMAGE_ORIENT_90:
        case RSTTO_IMAGE_ORIENT_270:
        case RSTTO_IMAGE_ORIENT_FLIP_TRANSPOSE:
        case RSTTO_IMAGE_ORIENT_FLIP_TRANSVERSE:
            width = viewer->priv->image_height * viewer->priv->scale;
            height = viewer->priv->image_width * viewer->priv->scale;
            break;
        case RSTTO_IMAGE_ORIENT_NONE:
        case RSTTO_IMAGE_ORIENT_180:
        case RSTTO_IMAGE_ORIENT_FLIP_HORIZONTAL:
        case RSTTO_IMAGE_ORIENT_FLIP_VERTICAL:
        default:
            width = viewer->priv->image_width * viewer->priv->scale;
            height = viewer->priv->image_height * viewer->priv->scale;
            break;
    }

    viewer->priv->rendering.width = width;
    viewer->priv->rendering.height = height;
    viewer->priv->rendering.x_offset = MAX (0, (alloc.width - width) / 2.0);
    viewer->priv->rendering.y_offset = MAX (0, (alloc.height - height) / 2.0);

    /* set adjustemts */
    gtk_adjustment_configure (viewer->priv->hadjustment, h_value, 0,
                              MAX (viewer->priv->rendering.width, alloc.width),
                              0, 0, alloc.width);
    gtk_adjustment_configure (viewer->priv->vadjustment, v_value, 0,
                              MAX (viewer->priv->rendering.height, alloc.height),
                              0, 0, alloc.height);
}

static void
paint_background (GtkWidget *widget, cairo_t *ctx)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    GdkRGBA *bg_color = NULL;
    GdkWindow *window = gtk_widget_get_window (widget);
    GtkAllocation allocation;
    GtkStyleContext *context = gtk_widget_get_style_context (widget);

    /* Determine if we draw the 'default' background-color,
     * or the fullscreen-background-color.
     */
    if (GDK_WINDOW_STATE_FULLSCREEN & gdk_window_get_state (
                gdk_window_get_toplevel (window)))
    {
        bg_color = viewer->priv->bg_color_fs;
    }

    if (NULL == bg_color)
    {
        bg_color = viewer->priv->bg_color;
    }

    /* Paint the background-color */
    /******************************/
    if (NULL != bg_color)
    {
        gdk_cairo_set_source_rgba (ctx, bg_color);
        cairo_paint (ctx);
    }
    else
    {
        gtk_widget_get_allocation (widget, &allocation);
        gtk_render_background (context, ctx, 0, 0, allocation.width, allocation.height);
    }
}

static void
paint_background_icon (GtkWidget *widget, cairo_t *ctx)
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
               ? 1.2 * BACKGROUND_ICON_SIZE / allocation.width
               : 1.2 * BACKGROUND_ICON_SIZE / allocation.height;

    /* Move the cairo context in position so the
     * background-image is painted in the center
     * of the widget.
     */
    cairo_translate (
            ctx,
            (allocation.width - BACKGROUND_ICON_SIZE / bg_scale) / 2.0,
            (allocation.height - BACKGROUND_ICON_SIZE / bg_scale) / 2.0);

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
        rstto_util_set_source_pixbuf (ctx, viewer->priv->bg_icon, 0, 0);
        cairo_paint_with_alpha (ctx, 0.1);
    }
}

static void
paint_clock (GtkWidget *widget, cairo_t *ctx)
{
    GtkAllocation allocation;
    gdouble width, height, offset;
    time_t t = time (NULL);
    struct tm *lt = localtime (&t);
    gdouble hour_angle = M_PI * 2 / 12 * (lt->tm_hour % 12 + 6 + (M_PI * 2 / 720.0 * lt->tm_min));
    gint i;

    gtk_widget_get_allocation (widget, &allocation);

    width = RSTTO_GET_CLOCK_WIDTH (allocation.width, allocation.height);
    height = width;
    offset = RSTTO_GET_CLOCK_OFFSET (width);

    cairo_save (ctx);

    cairo_translate (
        ctx,
        allocation.width - offset - width,
        allocation.height - offset - height);

    cairo_set_source_rgba (ctx, 0.0, 0.0, 0.0, 0.4);
    cairo_save (ctx);
    cairo_translate (
        ctx,
        width / 2,
        height / 2);
    cairo_arc (
        ctx,
        00, 00,
        width / 2,
        0,
        2 * M_PI);
    cairo_fill (ctx);

    cairo_set_source_rgba (ctx, 1.0, 1.0, 1.0, 0.8);

    cairo_save (ctx);
    for (i = 0; i < 12; ++i)
    {
        cairo_rotate (
            ctx,
            30 * M_PI / 180);
        cairo_arc (
            ctx,
            00, -1 * (width / 2 - 5),
            width / 20,
            0,
            2 * M_PI);
        cairo_fill (ctx);
    }
    cairo_restore (ctx);

/***/
    cairo_save (ctx);
    cairo_set_line_width (ctx, width / 15);
    cairo_set_line_cap (ctx, CAIRO_LINE_CAP_ROUND);
    cairo_rotate (
        ctx,
        hour_angle);
    cairo_move_to (ctx, 0, 0);
    cairo_line_to (
        ctx,
        0,
        height * 0.2);
    cairo_stroke (
        ctx);
    cairo_restore (ctx);
/***/
    cairo_set_line_width (ctx, width / 15);
    cairo_set_line_cap (ctx, CAIRO_LINE_CAP_ROUND);
    cairo_rotate (
        ctx,
        M_PI * 2 / 60 * (lt->tm_min % 60 + 30));
    cairo_move_to (ctx, 0, 0);
    cairo_line_to (
        ctx,
        0,
        height * 0.3);
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
    gint block_width = 10;
    gint block_height = 10;
    gdouble bg_scale = 1.0;
    gdouble x_scale, y_scale;
    GtkAllocation allocation;
    cairo_matrix_t transform_matrix;

    gtk_widget_get_allocation (widget, &allocation);

    /* image and rendering are set */
    if (viewer->priv->image_width > 0 && viewer->priv->rendering.width > 0)
    {
        cairo_save (ctx);

/* BEGIN PAINT CHECKERED BACKGROUND */
        if (viewer->priv->pixbuf.has_alpha)
        {
            cairo_set_source_rgba (ctx, 0.8, 0.8, 0.8, 1.0);
            cairo_rectangle (
                    ctx,
                    viewer->priv->rendering.x_offset,
                    viewer->priv->rendering.y_offset,
                    viewer->priv->rendering.width,
                    viewer->priv->rendering.height);
            cairo_fill (ctx);

            cairo_set_source_rgba (ctx, 0.7, 0.7, 0.7, 1.0);
            for (i = 0; i < viewer->priv->rendering.width / 10.0; ++i)
            {
                if (i % 2)
                    a = 1;
                else
                    a = 0;

                if (i + 1 <= viewer->priv->rendering.width / 10.0)
                {
                    block_width = 10;
                }
                else
                {
                    block_width = viewer->priv->rendering.width % 10;
                }
                for (; a < viewer->priv->rendering.height / 10.0; a += 2)
                {
                    if (a + 1 <= viewer->priv->rendering.height / 10.0)
                    {
                        block_height = 10;
                    }
                    else
                    {
                        block_height = viewer->priv->rendering.height % 10;
                    }
                    cairo_rectangle (
                            ctx,
                            viewer->priv->rendering.x_offset + i * 10,
                            viewer->priv->rendering.y_offset + a * 10,
                            block_width,
                            block_height);
                    cairo_fill (ctx);
                }
            }
        }
/* END PAINT CHECKERED BACKGROUND */
        cairo_restore (ctx);

        switch (viewer->priv->orientation)
        {
            case RSTTO_IMAGE_ORIENT_FLIP_HORIZONTAL:
                cairo_translate (
                        ctx,
                        0.0 - floor (gtk_adjustment_get_value (viewer->priv->hadjustment)),
                        0.0 - floor (gtk_adjustment_get_value (viewer->priv->vadjustment)));
                cairo_translate (
                        ctx,
                        viewer->priv->image_width * viewer->priv->scale,
                        0.0);
                cairo_translate (
                        ctx,
                        viewer->priv->rendering.x_offset,
                        viewer->priv->rendering.y_offset);
                cairo_matrix_init_identity (&transform_matrix);
                transform_matrix.xx = -1.0;
                cairo_transform (ctx, &transform_matrix);
                break;
            case RSTTO_IMAGE_ORIENT_FLIP_VERTICAL:
                cairo_translate (
                        ctx,
                        0.0 - floor (gtk_adjustment_get_value (viewer->priv->hadjustment)),
                        0.0 - floor (gtk_adjustment_get_value (viewer->priv->vadjustment)));
                cairo_translate (
                        ctx,
                        0.0,
                        viewer->priv->image_height * viewer->priv->scale);
                cairo_translate (
                        ctx,
                        viewer->priv->rendering.x_offset,
                        viewer->priv->rendering.y_offset);
                cairo_matrix_init_identity (&transform_matrix);
                transform_matrix.yy = -1.0;
                cairo_transform (ctx, &transform_matrix);
                break;
            case RSTTO_IMAGE_ORIENT_FLIP_TRANSPOSE:
                cairo_rotate (ctx, M_PI * 1.5);
                cairo_translate (
                        ctx,
                        floor (gtk_adjustment_get_value (viewer->priv->vadjustment)),
                        floor (0.0 - gtk_adjustment_get_value (viewer->priv->hadjustment)));
                cairo_translate (
                        ctx,
                        -1.0 * viewer->priv->rendering.y_offset,
                        viewer->priv->rendering.x_offset);
                cairo_matrix_init_identity (&transform_matrix);
                transform_matrix.xx = -1.0;
                cairo_transform (ctx, &transform_matrix);
                break;
            case RSTTO_IMAGE_ORIENT_FLIP_TRANSVERSE:
                cairo_rotate (ctx, M_PI * 0.5);
                cairo_translate (
                        ctx,
                        floor (0.0 - gtk_adjustment_get_value (viewer->priv->vadjustment)),
                        floor (gtk_adjustment_get_value (viewer->priv->hadjustment)));
                cairo_translate (
                        ctx,
                        viewer->priv->image_width * viewer->priv->scale,
                        -1.0 * viewer->priv->image_height * viewer->priv->scale);
                cairo_translate (
                        ctx,
                        viewer->priv->rendering.y_offset,
                        -1.0 * viewer->priv->rendering.x_offset);
                cairo_matrix_init_identity (&transform_matrix);
                transform_matrix.xx = -1.0;
                cairo_transform (ctx, &transform_matrix);
                break;
            case RSTTO_IMAGE_ORIENT_90:
                cairo_rotate (
                        ctx,
                        M_PI * 0.5);
                cairo_translate (
                        ctx,
                        floor (0.0 - gtk_adjustment_get_value (viewer->priv->vadjustment)),
                        floor (gtk_adjustment_get_value (viewer->priv->hadjustment)));
                cairo_translate (
                        ctx,
                        0.0,
                        -1.0 * viewer->priv->image_height * viewer->priv->scale);
                cairo_translate (
                        ctx,
                        viewer->priv->rendering.y_offset,
                        -1.0 * viewer->priv->rendering.x_offset);
                break;
            case RSTTO_IMAGE_ORIENT_270:
                cairo_rotate (
                        ctx,
                        M_PI * 1.5);
                cairo_translate (
                        ctx,
                        floor (gtk_adjustment_get_value (viewer->priv->vadjustment)),
                        0.0 - floor (gtk_adjustment_get_value (viewer->priv->hadjustment)));
                cairo_translate (
                        ctx,
                        -1.0 * viewer->priv->image_width * viewer->priv->scale,
                        0.0);

                cairo_translate (
                        ctx,
                        -1.0 * viewer->priv->rendering.y_offset,
                        viewer->priv->rendering.x_offset);
                break;
            case RSTTO_IMAGE_ORIENT_180:
                cairo_rotate (
                        ctx,
                        M_PI);
                cairo_translate (
                        ctx,
                        floor (gtk_adjustment_get_value (viewer->priv->hadjustment)),
                        floor (gtk_adjustment_get_value (viewer->priv->vadjustment)));
                cairo_translate (
                        ctx,
                        -1.0 * viewer->priv->image_width * viewer->priv->scale,
                        -1.0 * viewer->priv->image_height * viewer->priv->scale);

                cairo_translate (
                        ctx,
                        -1.0 * viewer->priv->rendering.x_offset,
                        -1.0 * viewer->priv->rendering.y_offset);
                break;
            case RSTTO_IMAGE_ORIENT_NONE:
            default:
                cairo_translate (
                        ctx,
                        0.0 - floor (gtk_adjustment_get_value (viewer->priv->hadjustment)),
                        0.0 - floor (gtk_adjustment_get_value (viewer->priv->vadjustment)));

                cairo_translate (
                        ctx,
                        viewer->priv->rendering.x_offset,
                        viewer->priv->rendering.y_offset);
                break;
        }

        switch (viewer->priv->orientation)
        {
            case RSTTO_IMAGE_ORIENT_90:
            case RSTTO_IMAGE_ORIENT_270:
            case RSTTO_IMAGE_ORIENT_FLIP_TRANSPOSE:
            case RSTTO_IMAGE_ORIENT_FLIP_TRANSVERSE:
                /* adjusted scales for painting, so that the painted image is strictly
                 * included in the rendering rectangle, and the drawing area restriction
                 * in gdk_window_invalidate_*() is really taken into account (typical
                 * example where it is necessary: when redrawing the background in
                 * cb_rstto_bgcolor_changed()) */
                x_scale = (1 - DBL_EPSILON) * viewer->priv->rendering.width
                                            / viewer->priv->image_height;
                y_scale = (1 - DBL_EPSILON) * viewer->priv->rendering.height
                                            / viewer->priv->image_width;
                break;
            case RSTTO_IMAGE_ORIENT_NONE:
            case RSTTO_IMAGE_ORIENT_180:
            case RSTTO_IMAGE_ORIENT_FLIP_HORIZONTAL:
            case RSTTO_IMAGE_ORIENT_FLIP_VERTICAL:
            default:
                x_scale = (1 - DBL_EPSILON) * viewer->priv->rendering.width
                                            / viewer->priv->image_width;
                y_scale = (1 - DBL_EPSILON) * viewer->priv->rendering.height
                                            / viewer->priv->image_height;
                break;
        }

        cairo_scale (ctx, x_scale / viewer->priv->image_scale, y_scale / viewer->priv->image_scale);
        cairo_set_source (ctx, viewer->priv->pixbuf.pattern);
        cairo_paint (ctx);
    }
    else
    {
        if (viewer->priv->error)
        {
            /* Calculate the icon-size */
            /***************************/
            bg_scale = (allocation.width < allocation.height)
                       ? 1.2 * BACKGROUND_ICON_SIZE / allocation.width
                       : 1.2 * BACKGROUND_ICON_SIZE / allocation.height;

            /* Move the cairo context in position so the
             * background-image is painted in the center
             * of the widget.
             */
            cairo_translate (
                    ctx,
                    (allocation.width - BACKGROUND_ICON_SIZE / bg_scale) / 2.0,
                    (allocation.height - BACKGROUND_ICON_SIZE / bg_scale) / 2.0);

            /* Scale the context so the image
             * fills the same part of the cairo-context
             */
            cairo_scale (
                    ctx,
                    1.0 / bg_scale,
                    1.0 / bg_scale);

            /* Draw the pixbuf on the cairo-context */
            /****************************************/
            if (viewer->priv->missing_icon != NULL)
            {
                rstto_util_set_source_pixbuf (ctx, viewer->priv->missing_icon, 0, 0);
                cairo_paint_with_alpha (ctx, 1.0);
            }
        }
    }
}

static void
paint_selection_box (GtkWidget *widget, cairo_t *ctx)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gdouble box_y, box_x, box_width, box_height;
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
        box_y = viewer->priv->motion.y;
        box_height = viewer->priv->motion.current_y - box_y;
    }
    else
    {
        box_y = viewer->priv->motion.current_y;
        box_height = viewer->priv->motion.y - box_y;
    }

    /* A selection-box can be created moving the cursor from
     * top to bottom, aswell as from bottom to top.
     *
     * Calculate the box dimensions accordingly.
     */
    if (viewer->priv->motion.x < viewer->priv->motion.current_x)
    {
        box_x = viewer->priv->motion.x;
        box_width = viewer->priv->motion.current_x - box_x;
    }
    else
    {
        box_x = viewer->priv->motion.current_x;
        box_width = viewer->priv->motion.x - box_x;
    }

    /*
     * Constrain the selection-box to the left
     * and top sides of the image.
     */
    if (box_x < x_offset)
    {
        box_width -= x_offset - box_x;
        box_x = x_offset;
    }
    if (box_y < y_offset)
    {
        box_height -= y_offset - box_y;
        box_y = y_offset;
    }

    if (x_offset + image_width < box_x + box_width)
    {
        box_width = (x_offset + image_width) - box_x - 1;
    }
    if (y_offset + image_height < box_y + box_height)
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
        box_x + RSTTO_LINE_WIDTH / 2.0, box_y + RSTTO_LINE_WIDTH / 2.0,
        box_width, box_height);

    cairo_set_source_rgba (ctx, 0.9, 0.9, 0.9, 0.2);
    cairo_fill_preserve (ctx);
    cairo_set_source_rgba (ctx, 0.2, 0.2, 0.2, 1.0);
    cairo_set_line_width (ctx, RSTTO_LINE_WIDTH);
    cairo_stroke (ctx);
}

static void
rstto_image_viewer_paint (GtkWidget *widget, cairo_t *ctx)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    if (gtk_widget_get_realized (widget))
    {
        cairo_rectangle (
                ctx,
                0.0,
                0.0,
                gtk_widget_get_allocated_width (widget),
                gtk_widget_get_allocated_height (widget));
        cairo_clip (ctx);
        cairo_save (ctx);

        /* Paint the background-color */
        /******************************/
        paint_background (widget, ctx);

        cairo_restore (ctx);

        /* Check if a file should be rendered */
        /**************************************/
        if (NULL == viewer->priv->file)
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
                    if (! g_cancellable_is_cancelled (viewer->priv->transaction->cancellable))
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
                        viewer);
                g_object_unref (viewer->priv->file);

                viewer->priv->file = file;
                if (viewer->priv->error)
                {
                    g_error_free (viewer->priv->error);
                    viewer->priv->error = NULL;
                }
                viewer->priv->image_scale = 1.0;
                viewer->priv->image_width = 0;
                viewer->priv->image_height = 0;

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
        if (viewer->priv->animation_id != 0)
            REMOVE_SOURCE (viewer->priv->animation_id);
        if (viewer->priv->iter)
        {
            g_object_unref (viewer->priv->iter);
            viewer->priv->iter = NULL;
        }
        if (viewer->priv->pixbuf.pattern)
        {
            cairo_pattern_destroy (viewer->priv->pixbuf.pattern);
            viewer->priv->pixbuf.pattern = NULL;
        }
        if (viewer->priv->transaction)
        {
            if (! g_cancellable_is_cancelled (viewer->priv->transaction->cancellable))
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
                    viewer);
            g_object_unref (viewer->priv->file);
            viewer->priv->file = NULL;

            /* Reset the image-size to 0.0 */
            viewer->priv->image_height = 0.0;
            viewer->priv->image_width = 0.0;

            set_adjustments (viewer, 0, 0);
            gdk_window_invalidate_rect (gtk_widget_get_window (widget), NULL, FALSE);

            gtk_widget_set_tooltip_text (widget, NULL);
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
        transaction->loader = gdk_pixbuf_loader_new ();
    }

    transaction->cancellable = g_cancellable_new ();
    transaction->buffer = g_new0 (guchar, RSTTO_IMAGE_VIEWER_BUFFER_SIZE);
    transaction->file = file;
    transaction->viewer = viewer;
    transaction->scale = scale;

    g_signal_connect (transaction->loader, "size-prepared", G_CALLBACK (cb_rstto_image_loader_size_prepared), transaction);
    g_signal_connect (transaction->loader, "closed", G_CALLBACK (cb_rstto_image_loader_closed), transaction);

    viewer->priv->transaction = transaction;

    g_file_read_async (rstto_file_get_file (transaction->file),
                       0,
                       transaction->cancellable,
                       cb_rstto_image_viewer_read_file_ready,
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
    GtkAllocation allocation;
    gdouble x_offset, y_offset, tmp_x, tmp_y, h_page_size, v_page_size;

    gtk_widget_get_allocation (widget, &allocation);

    switch (viewer->priv->orientation)
    {
        case RSTTO_IMAGE_ORIENT_90:
        case RSTTO_IMAGE_ORIENT_270:
            x_offset = (allocation.width - viewer->priv->image_height * viewer->priv->scale) / 2.0;
            y_offset = (allocation.height - viewer->priv->image_width * viewer->priv->scale) / 2.0;
            break;
        case RSTTO_IMAGE_ORIENT_NONE:
        default:
            x_offset = (allocation.width - viewer->priv->image_width * viewer->priv->scale) / 2.0;
            y_offset = (allocation.height - viewer->priv->image_height * viewer->priv->scale) / 2.0;
            break;
    }

    x_offset = MAX (x_offset, 0);
    y_offset = MAX (y_offset, 0);

    h_page_size = gtk_adjustment_get_page_size (viewer->priv->hadjustment);
    v_page_size = gtk_adjustment_get_page_size (viewer->priv->vadjustment);

    tmp_x = (gtk_adjustment_get_value (viewer->priv->hadjustment) + h_page_size / 2 - x_offset)
            / viewer->priv->scale;
    tmp_y = (gtk_adjustment_get_value (viewer->priv->vadjustment) + v_page_size / 2 - y_offset)
            / viewer->priv->scale;

    if (set_scale (viewer, scale))
    {
        set_adjustments (viewer,
                         tmp_x * viewer->priv->scale - h_page_size / 2,
                         tmp_y * viewer->priv->scale - v_page_size / 2);
        gdk_window_invalidate_rect (gtk_widget_get_window (GTK_WIDGET (viewer)), NULL, FALSE);
    }
}

GdkPixbuf *
rstto_image_viewer_get_pixbuf (RsttoImageViewer *viewer)
{
    cairo_surface_t *surface;

    cairo_pattern_get_surface (viewer->priv->pixbuf.pattern, &surface);

    return gdk_pixbuf_get_from_surface (surface, 0, 0,
                                        viewer->priv->pixbuf.width,
                                        viewer->priv->pixbuf.height);
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
    viewer->priv->orientation = orientation;

    if (viewer->priv->auto_scale)
        set_scale (viewer, RSTTO_SCALE_FIT_TO_VIEW);

    rstto_file_set_orientation (viewer->priv->file, orientation);

    set_adjustments (viewer,
                     gtk_adjustment_get_value (viewer->priv->hadjustment),
                     gtk_adjustment_get_value (viewer->priv->vadjustment));
    gdk_window_invalidate_rect (gtk_widget_get_window (GTK_WIDGET (viewer)), NULL, FALSE);
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
        gtk_menu_detach (viewer->priv->menu);
        gtk_widget_destroy (GTK_WIDGET (viewer->priv->menu));
    }

    viewer->priv->menu = menu;

    if (viewer->priv->menu)
    {
        gtk_menu_attach_to_widget (
                viewer->priv->menu,
                GTK_WIDGET (viewer),
                NULL);
    }

}


/************************/
/** CALLBACK FUNCTIONS **/
/************************/
static void
cb_rstto_image_viewer_read_file_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    GFile *file = G_FILE (source_object);
    RsttoImageViewerTransaction *transaction = user_data;

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
                               cb_rstto_image_viewer_read_input_stream_ready,
                               transaction);
}

static void
cb_rstto_image_viewer_read_input_stream_ready (GObject *source_object, GAsyncResult *result, gpointer user_data)
{
    RsttoImageViewerTransaction *transaction = user_data;
    gssize read_bytes = g_input_stream_read_finish (G_INPUT_STREAM (source_object), result, &transaction->error);

    if (read_bytes == -1)
    {
        gdk_pixbuf_loader_close (transaction->loader, NULL);
        return;
    }

    if (read_bytes > 0)
    {
        if (! gdk_pixbuf_loader_write (transaction->loader, transaction->buffer,
                                       read_bytes, &transaction->error))
        {
            /* Clean up the input-stream */
            g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
            g_object_unref (source_object);
        }
        else
        {
            g_input_stream_read_async (G_INPUT_STREAM (source_object),
                                       transaction->buffer,
                                       RSTTO_IMAGE_VIEWER_BUFFER_SIZE,
                                       G_PRIORITY_DEFAULT,
                                       transaction->cancellable,
                                       cb_rstto_image_viewer_read_input_stream_ready,
                                       transaction);
        }
    }
    else
    {
        /* Loading complete, transaction should not be free-ed */
        gdk_pixbuf_loader_close (transaction->loader, &transaction->error);

        /* Clean up the input-stream */
        g_input_stream_close (G_INPUT_STREAM (source_object), NULL, NULL);
        g_object_unref (source_object);
    }
}


static void
cb_rstto_image_loader_image_ready (GdkPixbufLoader *loader, RsttoImageViewerTransaction *transaction)
{
    RsttoImageViewer *viewer = transaction->viewer;
    GdkPixbuf *pixbuf;
    gint timeout = 0;

    if (viewer->priv->transaction == transaction)
    {
        if (viewer->priv->animation_id != 0)
            REMOVE_SOURCE (viewer->priv->animation_id);

        if (viewer->priv->iter)
        {
            g_object_unref (viewer->priv->iter);
            viewer->priv->iter = NULL;
        }

        if (viewer->priv->pixbuf.pattern)
        {
            cairo_pattern_destroy (viewer->priv->pixbuf.pattern);
            viewer->priv->pixbuf.pattern = NULL;
        }

        viewer->priv->iter =
            gdk_pixbuf_animation_get_iter (gdk_pixbuf_loader_get_animation (loader), NULL);

        /* set pixbuf data */
        pixbuf = gdk_pixbuf_animation_iter_get_pixbuf (viewer->priv->iter);
        viewer->priv->pixbuf.pattern = rstto_util_set_source_pixbuf (NULL, pixbuf, 0, 0);
        viewer->priv->pixbuf.has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);
        viewer->priv->pixbuf.width = gdk_pixbuf_get_width (pixbuf);
        viewer->priv->pixbuf.height = gdk_pixbuf_get_height (pixbuf);

        /* schedule next frame diplay if needed */
        timeout = gdk_pixbuf_animation_iter_get_delay_time (viewer->priv->iter);
        if (timeout > 0)
            viewer->priv->animation_id = g_timeout_add (timeout,
                                                        cb_rstto_image_viewer_update_pixbuf,
                                                        rstto_util_source_autoremove (viewer));
        else
        {
            /* this is a single-frame image, we only need to keep the pixbuf as a pattern */
            g_object_unref (viewer->priv->iter);
            viewer->priv->iter = NULL;
        }
    }
}

static void
cb_rstto_image_loader_size_prepared (GdkPixbufLoader *loader, gint width, gint height, RsttoImageViewerTransaction *transaction)
{
    gboolean limit_quality = transaction->viewer->priv->limit_quality;

    /*
     * By default, the image-size won't be limited to screen-size (since it's smaller)
     * or, because we don't want to reduce it.
     * Set the image_scale to 1.0 (100%)
     */
    transaction->image_scale = 1.0;

    transaction->image_width = width;
    transaction->image_height = height;

    if (limit_quality)
    {
        GdkMonitor *monitor = gdk_display_get_monitor_at_window (
                gdk_screen_get_display (default_screen),
                gtk_widget_get_window (GTK_WIDGET (transaction->viewer)));
        gdouble s_width, s_height;
        GdkRectangle monitor_geometry;

        gdk_monitor_get_geometry (monitor, &monitor_geometry);
        s_width = monitor_geometry.width;
        s_height = monitor_geometry.height;

        /*
         * Set the maximum size of the loaded image to the screen-size.
         * TODO: Add some 'smart-stuff' here
         */
        if (s_width < width || s_height < height)
        {
            /*
             * The image is loaded at the screen_size, calculate how this fits best.
             *  scale = MIN (width / screen_width, height / screen_height)
             *
             */
            if (width / s_width < height / s_height)
            {
                transaction->image_scale = s_width / width;
                gdk_pixbuf_loader_set_size (loader, s_width, s_width * height / width);
            }
            else
            {
                transaction->image_scale = s_height / height;
                gdk_pixbuf_loader_set_size (loader, s_height * width / height, s_height);
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
    GtkWidget *widget = GTK_WIDGET (viewer);

    if (viewer->priv->transaction == transaction)
    {
        if (transaction->error == NULL
            || g_error_matches (transaction->error, GDK_PIXBUF_ERROR,
                                GDK_PIXBUF_ERROR_CORRUPT_IMAGE))
        {
            gtk_widget_set_tooltip_text (widget, NULL);
            viewer->priv->image_scale = transaction->image_scale;
            viewer->priv->image_width = transaction->image_width;
            viewer->priv->image_height = transaction->image_height;
            viewer->priv->orientation = transaction->orientation;
            set_scale (viewer, transaction->scale);

            cb_rstto_image_loader_image_ready (loader, transaction);
        }
        else
        {
            viewer->priv->image_scale = 1.0;
            viewer->priv->image_width = 0;
            viewer->priv->image_height = 0;
            if (viewer->priv->pixbuf.pattern)
            {
                cairo_pattern_destroy (viewer->priv->pixbuf.pattern);
                viewer->priv->pixbuf.pattern = NULL;
            }

            gtk_widget_set_tooltip_text (widget, transaction->error->message);
        }

        viewer->priv->error = transaction->error;
        transaction->error = NULL;
        viewer->priv->transaction = NULL;

        set_adjustments (viewer,
                         gtk_adjustment_get_value (viewer->priv->hadjustment),
                         gtk_adjustment_get_value (viewer->priv->vadjustment));
        gdk_window_invalidate_rect (gtk_widget_get_window (widget), NULL, FALSE);
    }

    g_signal_emit_by_name (transaction->viewer, "size-ready");
    rstto_image_viewer_transaction_free (transaction);
}

static gboolean
cb_rstto_image_viewer_update_pixbuf (gpointer user_data)
{
    RsttoImageViewer *viewer = user_data;
    GdkPixbuf *pixbuf;
    GdkRectangle rect;
    gint timeout = 0;

    if (gdk_pixbuf_animation_iter_advance (viewer->priv->iter, NULL))
    {
        /* update pixbuf data (a copy of the pixbuf is kept as a pattern, so we can access
         * it safely regardless of the iter state) */
        cairo_pattern_destroy (viewer->priv->pixbuf.pattern);
        pixbuf = gdk_pixbuf_animation_iter_get_pixbuf (viewer->priv->iter);
        viewer->priv->pixbuf.pattern = rstto_util_set_source_pixbuf (NULL, pixbuf, 0, 0);
        viewer->priv->pixbuf.has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);
        viewer->priv->pixbuf.width = gdk_pixbuf_get_width (pixbuf);
        viewer->priv->pixbuf.height = gdk_pixbuf_get_height (pixbuf);

        /* redraw only the image */
        rect.x = viewer->priv->rendering.x_offset;
        rect.y = viewer->priv->rendering.y_offset;
        rect.width = viewer->priv->rendering.width;
        rect.height = viewer->priv->rendering.height;
        gdk_window_invalidate_rect (gtk_widget_get_window (user_data), &rect, FALSE);

        /* schedule next frame display */
        timeout = gdk_pixbuf_animation_iter_get_delay_time (viewer->priv->iter);
        if (timeout > 0)
            viewer->priv->animation_id = g_timeout_add (timeout,
                                                        cb_rstto_image_viewer_update_pixbuf,
                                                        rstto_util_source_autoremove (viewer));
    }

    if (timeout <= 0)
    {
        /* if the animation stops, we only need to keep the pixbuf as a pattern */
        g_object_unref (viewer->priv->iter);
        viewer->priv->iter = NULL;

        /* reset the timeout id only in this case */
        viewer->priv->animation_id = 0;
    }

    return FALSE;
}

static gboolean
rstto_scroll_event (GtkWidget *widget, GdkEventScroll *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gboolean invert_zoom_direction = viewer->priv->invert_zoom_direction;
    gdouble x_offset = viewer->priv->rendering.x_offset;
    gdouble y_offset = viewer->priv->rendering.y_offset;
    gdouble scale = viewer->priv->scale;
    gdouble tmp_x, tmp_y;

    if (event->state & (GDK_CONTROL_MASK))
    {
        if (NULL != viewer->priv->file)
        {
            tmp_x = (gtk_adjustment_get_value (viewer->priv->hadjustment) + event->x - x_offset)
                    / viewer->priv->scale;
            tmp_y = (gtk_adjustment_get_value (viewer->priv->vadjustment) + event->y - y_offset)
                    / viewer->priv->scale;

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
                case GDK_SCROLL_SMOOTH:
                    /* TODO */
                    break;
            }

            if (set_scale (viewer, scale))
            {
                set_adjustments (viewer, tmp_x * scale - event->x, tmp_y * scale - event->y);
                gdk_window_invalidate_rect (gtk_widget_get_window (widget), NULL, FALSE);
            }
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean
rstto_motion_notify_event (GtkWidget *widget, GdkEventMotion *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    GdkRectangle previous_box, current_box, union_box;
    gdouble previous_x, previous_y;

    if (event->state & GDK_BUTTON1_MASK)
    {
        previous_x = viewer->priv->motion.current_x;
        previous_y = viewer->priv->motion.current_y;
        viewer->priv->motion.current_x = event->x;
        viewer->priv->motion.current_y = event->y;

        switch (viewer->priv->motion.state)
        {
            case RSTTO_IMAGE_VIEWER_MOTION_STATE_MOVE:
                if (viewer->priv->motion.x != viewer->priv->motion.current_x
                    || viewer->priv->motion.y != viewer->priv->motion.current_y)
                {
                    set_adjustments (viewer,
                                     viewer->priv->motion.h_val + viewer->priv->motion.x
                                        - viewer->priv->motion.current_x,
                                     viewer->priv->motion.v_val + viewer->priv->motion.y
                                        - viewer->priv->motion.current_y);
                    gdk_window_invalidate_rect (gtk_widget_get_window (widget), NULL, FALSE);
                }
                break;
            case RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM:
                /* redraw only the union of the previous and current selection boxes */
                previous_box.x = MIN (viewer->priv->motion.x, previous_x);
                previous_box.y = MIN (viewer->priv->motion.y, previous_y);
                previous_box.width = ceil (MAX (viewer->priv->motion.x, previous_x))
                                     - previous_box.x + 2 * RSTTO_LINE_WIDTH;
                previous_box.height = ceil (MAX (viewer->priv->motion.y, previous_y))
                                      - previous_box.y + 2 * RSTTO_LINE_WIDTH;

                current_box.x = MIN (viewer->priv->motion.x, event->x);
                current_box.y = MIN (viewer->priv->motion.y, event->y);
                current_box.width = ceil (MAX (viewer->priv->motion.x, event->x))
                                    - current_box.x + 2 * RSTTO_LINE_WIDTH;
                current_box.height = ceil (MAX (viewer->priv->motion.y, event->y))
                                     - current_box.y + 2 * RSTTO_LINE_WIDTH;

                gdk_rectangle_union (&previous_box, &current_box, &union_box);
                gdk_window_invalidate_rect (gtk_widget_get_window (widget), &union_box, FALSE);

                /* Only change the cursor when hovering over the image
                 */
                if ((event->x > viewer->priv->rendering.x_offset) &&
                    (event->y > viewer->priv->rendering.y_offset) &&
                    (event->y < (viewer->priv->rendering.y_offset + viewer->priv->rendering.height)) &&
                    (event->x < (viewer->priv->rendering.x_offset + viewer->priv->rendering.width)))
                {
                    GdkCursor *cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_UL_ANGLE);
                    gdk_window_set_cursor (gtk_widget_get_window (widget), cursor);
                    g_object_unref (cursor);
                }
                else
                {
                    /* Set back to default when moving over the
                     * background.
                     */
                    gdk_window_set_cursor (gtk_widget_get_window (widget), NULL);
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

    if (event->button == 1 && event->type == GDK_BUTTON_PRESS)
    {
        viewer->priv->motion.x = event->x;
        viewer->priv->motion.y = event->y;
        viewer->priv->motion.current_x = event->x;
        viewer->priv->motion.current_y = event->y;
        viewer->priv->motion.h_val = gtk_adjustment_get_value (viewer->priv->hadjustment);
        viewer->priv->motion.v_val = gtk_adjustment_get_value (viewer->priv->vadjustment);

        if (viewer->priv->file != NULL)
        {
            if (! (event->state & (GDK_CONTROL_MASK)))
            {
                if ((event->x > viewer->priv->rendering.x_offset) &&
                    (event->y > viewer->priv->rendering.y_offset) &&
                    (event->y < (viewer->priv->rendering.y_offset + viewer->priv->rendering.height)) &&
                    (event->x < (viewer->priv->rendering.x_offset + viewer->priv->rendering.width)))
                {
                    GdkCursor *cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_FLEUR);
                    gdk_window_set_cursor (gtk_widget_get_window (widget), cursor);
                    g_object_unref (cursor);
                    rstto_image_viewer_set_motion_state (viewer, RSTTO_IMAGE_VIEWER_MOTION_STATE_MOVE);
                }
            }

            if (event->state & GDK_CONTROL_MASK)
            {
                /* Only change the cursor when hovering over the image
                 */
                if ((event->x > viewer->priv->rendering.x_offset) &&
                    (event->y > viewer->priv->rendering.y_offset) &&
                    (event->y < (viewer->priv->rendering.y_offset + viewer->priv->rendering.height)) &&
                    (event->x < (viewer->priv->rendering.x_offset + viewer->priv->rendering.width)))
                {
                    GdkCursor *cursor = gdk_cursor_new_for_display (gdk_display_get_default (), GDK_UL_ANGLE);
                    gdk_window_set_cursor (gtk_widget_get_window (widget), cursor);
                    g_object_unref (cursor);
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
    else if (event->button == 1 && event->type == GDK_2BUTTON_PRESS)
    {
        /* toggle fullscreen mode only when double-clicking on the image */
        if (viewer->priv->file != NULL
            && event->x >= viewer->priv->rendering.x_offset
            && event->x <= (viewer->priv->rendering.x_offset + viewer->priv->rendering.width)
            && event->y >= viewer->priv->rendering.y_offset
            && event->y <= (viewer->priv->rendering.y_offset + viewer->priv->rendering.height))
        {
            GtkWidget *window;

            window = gtk_widget_get_ancestor (widget, RSTTO_TYPE_MAIN_WINDOW);
            if (window != NULL)
                rstto_main_window_fullscreen (widget, RSTTO_MAIN_WINDOW (window));
        }

        return TRUE;
    }
    else if (event->button == 3)
    {
        if (viewer->priv->menu)
        {
            gtk_widget_show_all (GTK_WIDGET (viewer->priv->menu));
            gtk_menu_popup_at_pointer (viewer->priv->menu, NULL);
        }
        return TRUE;
    }
    return FALSE;
}

static gboolean
rstto_button_release_event (GtkWidget *widget, GdkEventButton *event)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);
    gdouble box_y, box_x, box_width, box_height, tmp_x, tmp_y, max_scale,
            h_page_size, v_page_size;
    gdouble x_offset = viewer->priv->rendering.x_offset;
    gdouble y_offset = viewer->priv->rendering.y_offset;
    gdouble image_width = viewer->priv->rendering.width;
    gdouble image_height = viewer->priv->rendering.height;
    gdouble scale = viewer->priv->scale;

    gdk_window_set_cursor (gtk_widget_get_window (widget), NULL);

    switch (viewer->priv->motion.state)
    {
        case RSTTO_IMAGE_VIEWER_MOTION_STATE_BOX_ZOOM:
            max_scale = scale_get_max (viewer);
            /* A selection-box can be created moving the cursor from
             * left to right, aswell as from right to left.
             *
             * Calculate the box dimensions accordingly.
             */
            if (viewer->priv->motion.y < viewer->priv->motion.current_y)
            {
                box_y = viewer->priv->motion.y;
                box_height = viewer->priv->motion.current_y - box_y;
            }
            else
            {
                box_y = viewer->priv->motion.current_y;
                box_height = viewer->priv->motion.y - box_y;
            }

            /* A selection-box can be created moving the cursor from
             * top to bottom, aswell as from bottom to top.
             *
             * Calculate the box dimensions accordingly.
             */
            if (viewer->priv->motion.x < viewer->priv->motion.current_x)
            {
                box_x = viewer->priv->motion.x;
                box_width = viewer->priv->motion.current_x - box_x;
            }
            else
            {
                box_x = viewer->priv->motion.current_x;
                box_width = viewer->priv->motion.x - box_x;
            }

            /*
             * Constrain the selection-box to the left
             * and top sides of the image.
             */
            if (box_x < x_offset)
            {
                box_width -= x_offset - box_x;
                box_x = x_offset;
            }
            if (box_y < y_offset)
            {
                box_height -= y_offset - box_y;
                box_y = y_offset;
            }

            if (x_offset + image_width < box_x + box_width)
            {
                box_width = (x_offset + image_width) - box_x - 1;
            }
            if (y_offset + image_height < box_y + box_height)
            {
                box_height = (y_offset + image_height) - box_y - 1;
            }

            if (box_width > 0 && box_height > 0)
            {
                /* Set auto_scale to false, we are going manual */
                viewer->priv->auto_scale = FALSE;

                if (scale == max_scale)
                    break;

                /*
                 * Calculate the center of the selection-box.
                 */
                tmp_y = (gtk_adjustment_get_value (viewer->priv->vadjustment)
                         + box_y + box_height / 2 - y_offset) / viewer->priv->scale;
                tmp_x = (gtk_adjustment_get_value (viewer->priv->hadjustment)
                         + box_x + box_width / 2 - x_offset) / viewer->priv->scale;

                /*
                 * Calculate the new scale
                 */
                h_page_size = gtk_adjustment_get_page_size (viewer->priv->hadjustment);
                v_page_size = gtk_adjustment_get_page_size (viewer->priv->vadjustment);
                if (h_page_size / box_width < v_page_size / box_height)
                {
                    scale = viewer->priv->scale * h_page_size / box_width;
                }
                else
                {
                    scale = viewer->priv->scale * v_page_size / box_height;
                }

                /*
                 * Prevent the widget from zooming in beyond the
                 * MAX_SCALE.
                 */
                scale = MIN (scale, max_scale);
                viewer->priv->scale = scale;
                g_signal_emit_by_name (viewer, "scale-changed");

                set_adjustments (viewer,
                                 tmp_x * scale - h_page_size / 2,
                                 tmp_y * scale - v_page_size / 2);
                gdk_window_invalidate_rect (gtk_widget_get_window (widget), NULL, FALSE);
            }
            break;

        default:
            break;
    }

    rstto_image_viewer_set_motion_state (viewer, RSTTO_IMAGE_VIEWER_MOTION_STATE_NORMAL);

    return FALSE;
}

static void
cb_rstto_limit_quality_changed (GObject *settings, GParamSpec *pspec, gpointer user_data)
{
    RsttoImageViewer *viewer = user_data;

    g_object_get (viewer->priv->settings, "limit-quality", &(viewer->priv->limit_quality), NULL);

    if (NULL != viewer->priv->file)
        rstto_image_viewer_load_image (viewer, viewer->priv->file, viewer->priv->scale);
}

static void
cb_rstto_bgcolor_changed (GObject *settings, GParamSpec *pspec, gpointer user_data)
{
    RsttoImageViewer *viewer = user_data;
    cairo_region_t *region;
    cairo_status_t status;
    GtkAllocation alloc;
    const gchar *message = "Undetermined error";
    gboolean bg_color_override;

    gdk_rgba_free (viewer->priv->bg_color_fs);
    g_object_get (viewer->priv->settings, "bgcolor-fullscreen",
                  &(viewer->priv->bg_color_fs), NULL);

    gdk_rgba_free (viewer->priv->bg_color);
    g_object_get (viewer->priv->settings, "bgcolor-override", &bg_color_override, NULL);
    if (bg_color_override)
        g_object_get (viewer->priv->settings, "bgcolor", &(viewer->priv->bg_color), NULL);
    else
        viewer->priv->bg_color = NULL;

    /* do not redraw the image */
    gtk_widget_get_allocation (user_data, &alloc);
    region = cairo_region_create_rectangle (&alloc);
    alloc.x = viewer->priv->rendering.x_offset;
    alloc.y = viewer->priv->rendering.y_offset;
    alloc.width = viewer->priv->rendering.width;
    alloc.height = viewer->priv->rendering.height;

    status = cairo_region_subtract_rectangle (region, &alloc);
    if (status != CAIRO_STATUS_SUCCESS)
    {
        if (status == CAIRO_STATUS_NO_MEMORY)
            message = "Not enough memory";

        g_warning ("Failed to optimize image drawing: %s", message);
    }

    gdk_window_invalidate_region (gtk_widget_get_window (user_data), region, FALSE);
    cairo_region_destroy (region);
}

static void
cb_rstto_zoom_direction_changed (GObject *settings, GParamSpec *pspec, gpointer user_data)
{
    RsttoImageViewer *viewer = user_data;
    viewer->priv->invert_zoom_direction = rstto_settings_get_boolean_property (RSTTO_SETTINGS (settings), "invert-zoom-direction");
}

static gboolean
rstto_popup_menu (GtkWidget *widget)
{
    RsttoImageViewer *viewer = RSTTO_IMAGE_VIEWER (widget);

    if (viewer->priv->menu)
    {
        gtk_widget_show_all (GTK_WIDGET (viewer->priv->menu));
        gtk_menu_popup_at_pointer (viewer->priv->menu, NULL);
        return TRUE;
    }
    return FALSE;
}

static void
cb_rstto_image_viewer_dnd (GtkWidget *widget, GdkDragContext *context, gint x, gint y, GtkSelectionData *data,
                           guint info, guint time_, RsttoImageViewer *viewer)
{
    g_return_if_fail (RSTTO_IS_IMAGE_VIEWER (viewer));

    if ((gtk_selection_data_get_length (data) >= 0) && (gtk_selection_data_get_format (data) == 8))
    {
        gchar **uris;

        uris = g_uri_list_extract_uris ((const gchar *) gtk_selection_data_get_data (data));

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
        case PROP_HADJUSTMENT:
            if (viewer->priv->hadjustment)
                g_object_unref (viewer->priv->hadjustment);

            viewer->priv->hadjustment = g_value_get_object (value);
            if (viewer->priv->hadjustment)
                g_object_ref (viewer->priv->hadjustment);
            break;
        case PROP_VADJUSTMENT:
            if (viewer->priv->vadjustment)
                g_object_unref (viewer->priv->vadjustment);

            viewer->priv->vadjustment = g_value_get_object (value);
            if (viewer->priv->vadjustment)
                g_object_ref (viewer->priv->vadjustment);
            break;
        case PROP_HSCROLL_POLICY:
            viewer->priv->hscroll_policy = g_value_get_enum (value);
            gtk_widget_queue_resize (GTK_WIDGET (viewer));
            break;
        case PROP_VSCROLL_POLICY:
            viewer->priv->vscroll_policy = g_value_get_enum (value);
            gtk_widget_queue_resize (GTK_WIDGET (viewer));
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
        case PROP_HADJUSTMENT:
            g_value_set_object (value, viewer->priv->hadjustment);
            break;
        case PROP_VADJUSTMENT:
            g_value_set_object (value, viewer->priv->vadjustment);
            break;
        case PROP_HSCROLL_POLICY:
            g_value_set_enum (value, viewer->priv->hscroll_policy);
            break;
        case PROP_VSCROLL_POLICY:
            g_value_set_enum (value, viewer->priv->vscroll_policy);
            break;
    }
}

static gboolean
cb_rstto_image_viewer_refresh (gpointer user_data)
{
    GtkAllocation alloc;
    gdouble width, offset;

    /* redraw only the clock square */
    gtk_widget_get_allocation (user_data, &alloc);
    width = RSTTO_GET_CLOCK_WIDTH (alloc.width, alloc.height);
    offset = RSTTO_GET_CLOCK_OFFSET (width);
    alloc.x = alloc.width - width - offset;
    alloc.y = alloc.height - width - offset;
    alloc.width = alloc.height = ceil (width);

    gdk_window_invalidate_rect (gtk_widget_get_window (user_data), &alloc, FALSE);

    return TRUE;
}

void
rstto_image_viewer_set_show_clock (RsttoImageViewer *viewer, gboolean value)
{
    static guint id = 0;

    viewer->priv->props.show_clock = value;

    if (value)
        id = g_timeout_add (15000, cb_rstto_image_viewer_refresh,
                            rstto_util_source_autoremove (viewer));
    else if (id != 0)
        REMOVE_SOURCE (id);
}

gboolean
rstto_image_viewer_is_busy (RsttoImageViewer *viewer)
{
    if (viewer->priv->transaction != NULL)
    {
        return TRUE;
    }
    return FALSE;
}

static void
cb_rstto_image_viewer_file_changed (RsttoFile *r_file, RsttoImageViewer *viewer)
{
    rstto_image_viewer_load_image (
            viewer,
            r_file,
            viewer->priv->scale);

    g_signal_emit_by_name (viewer, "status-changed");
}

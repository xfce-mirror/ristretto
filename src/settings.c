/*
 *  Copyright (c) 2009 Stephan Arts <stephan@xfce.org>
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

#include <glib.h>
#include <gtk/gtk.h>
#include <xfconf/xfconf.h>
#include <libxfce4util/libxfce4util.h>

#include "settings.h"

static void
rstto_settings_init (GObject *);
static void
rstto_settings_class_init (GObjectClass *);

static void
rstto_settings_dispose (GObject *object);
static void
rstto_settings_finalize (GObject *object);

static void
rstto_settings_set_property    (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec);
static void
rstto_settings_get_property    (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec);

static GObjectClass *parent_class = NULL;

static RsttoSettings *settings_object;

enum
{
    PROP_0,
    PROP_SHOW_TOOLBAR,
    PROP_TOOLBAR_OPEN,
    PROP_ENABLE_CACHE,
    PROP_PRELOAD_IMAGES,
    PROP_CACHE_SIZE,
    PROP_IMAGE_QUALITY,
    PROP_WINDOW_WIDTH,
    PROP_WINDOW_HEIGHT,
    PROP_BGCOLOR,
    PROP_BGCOLOR_OVERRIDE,
    PROP_BGCOLOR_FULLSCREEN,
    PROP_CURRENT_URI,
    PROP_SLIDESHOW_TIMEOUT,
    PROP_SCROLLWHEEL_ACTION,
};

GType
rstto_settings_get_type ()
{
    static GType rstto_settings_type = 0;

    if (!rstto_settings_type)
    {
        static const GTypeInfo rstto_settings_info = 
        {
            sizeof (RsttoSettingsClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_settings_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoSettings),
            0,
            (GInstanceInitFunc) rstto_settings_init,
            NULL
        };

        rstto_settings_type = g_type_register_static (G_TYPE_OBJECT, "RsttoSettings", &rstto_settings_info, 0);
    }
    return rstto_settings_type;
}

struct _RsttoSettingsPriv
{
    XfconfChannel *channel;

    gboolean  show_toolbar;
    gchar    *toolbar_open;
    gboolean  preload_images;
    gboolean  enable_cache;
    guint     cache_size;
    guint     image_quality;
    guint     window_width;
    guint     window_height;
    gchar    *last_file_path;
    guint     slideshow_timeout;
    GdkColor *bgcolor;
    gboolean  bgcolor_override;
    GdkColor *bgcolor_fullscreen;
    gchar    *scrollwheel_action;
};


static void
rstto_settings_init (GObject *object)
{
    gchar *accelmap_path = NULL;

    RsttoSettings *settings = RSTTO_SETTINGS (object);

    settings->priv = g_new0 (RsttoSettingsPriv, 1);
    settings->priv->channel = xfconf_channel_new ("ristretto");

    accelmap_path = xfce_resource_lookup (XFCE_RESOURCE_CONFIG, "ristretto/accels.scm");
    if (accelmap_path)
    {
        gtk_accel_map_load (accelmap_path);
        g_free (accelmap_path);
        accelmap_path = NULL;
    }
    
    settings->priv->slideshow_timeout = 5000;
    settings->priv->bgcolor = g_new0 (GdkColor, 1);
    settings->priv->bgcolor_fullscreen = g_new0 (GdkColor, 1);
    settings->priv->image_quality = 2;
    settings->priv->toolbar_open = "file";

    xfconf_g_property_bind (settings->priv->channel, "/window/width", G_TYPE_UINT, settings, "window-width");
    xfconf_g_property_bind (settings->priv->channel, "/window/height", G_TYPE_UINT, settings, "window-height");

    xfconf_g_property_bind (settings->priv->channel, "/file/current-uri", G_TYPE_STRING, settings, "current-uri");

    xfconf_g_property_bind (settings->priv->channel, "/window/show-toolbar", G_TYPE_BOOLEAN, settings, "show-toolbar");
    xfconf_g_property_bind (settings->priv->channel, "/window/scrollwheel-action", G_TYPE_STRING, settings, "scrollwheel-action");
    xfconf_g_property_bind (settings->priv->channel, "/window/toolbar-open", G_TYPE_STRING, settings, "toolbar-open");

    xfconf_g_property_bind (settings->priv->channel, "/slideshow/timeout", G_TYPE_UINT, settings, "slideshow-timeout");

    xfconf_g_property_bind_gdkcolor (settings->priv->channel, "/window/bgcolor", settings, "bgcolor");
    xfconf_g_property_bind (settings->priv->channel, "/window/bgcolor-override", G_TYPE_BOOLEAN, settings, "bgcolor-override");

    xfconf_g_property_bind_gdkcolor (settings->priv->channel, "/window/bgcolor-fullscreen", settings, "bgcolor-fullscreen");
    xfconf_g_property_bind (settings->priv->channel, "/image/preload", G_TYPE_BOOLEAN, settings, "preload-images");
    xfconf_g_property_bind (settings->priv->channel, "/image/cache", G_TYPE_BOOLEAN, settings, "enable-cache");
    xfconf_g_property_bind (settings->priv->channel, "/image/cache-size", G_TYPE_UINT, settings, "cache-size");
    xfconf_g_property_bind (settings->priv->channel, "/image/quality", G_TYPE_UINT, settings, "image-quality");
}


static void
rstto_settings_class_init (GObjectClass *object_class)
{
    GParamSpec *pspec;

    RsttoSettingsClass *settings_class = RSTTO_SETTINGS_CLASS (object_class);

    parent_class = g_type_class_peek_parent (settings_class);

    object_class->dispose = rstto_settings_dispose;
    object_class->finalize = rstto_settings_finalize;

    object_class->set_property = rstto_settings_set_property;
    object_class->get_property = rstto_settings_get_property;

    pspec = g_param_spec_uint    ("window-width",
                                  "",
                                  "",
                                  0,
                                  G_MAXUINT,
                                  600,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_WINDOW_WIDTH,
                                     pspec);

    pspec = g_param_spec_uint    ("window-height",
                                  "",
                                  "",
                                  0,
                                  G_MAXUINT,
                                  400,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_WINDOW_HEIGHT,
                                     pspec);

    pspec = g_param_spec_boolean ("show-toolbar",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SHOW_TOOLBAR,
                                     pspec);

    pspec = g_param_spec_string ("toolbar-open",
                                  "",
                                  "",
                                  "file",
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_TOOLBAR_OPEN,
                                     pspec);


    pspec = g_param_spec_boolean ("preload-images",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_PRELOAD_IMAGES,
                                     pspec);

    pspec = g_param_spec_boolean ("enable-cache",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_ENABLE_CACHE,
                                     pspec);

    pspec = g_param_spec_uint    ("cache-size",
                                  "",
                                  "",
                                  0,
                                  G_MAXUINT,
                                  256,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_CACHE_SIZE,
                                     pspec);

    pspec = g_param_spec_uint    ("image-quality",
                                  "",
                                  "",
                                  0,
                                  50,
                                  2,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_IMAGE_QUALITY,
                                     pspec);

    pspec = g_param_spec_string  ("current-uri",
                                  "",
                                  "",
                                  "file://~/",
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_CURRENT_URI,
                                     pspec);

    pspec = g_param_spec_string  ("scrollwheel-action",
                                  "",
                                  "",
                                  "zoom",
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_CURRENT_URI,
                                     pspec);

    pspec = g_param_spec_uint    ("slideshow-timeout",
                                  "",
                                  "",
                                  1000,
                                  G_MAXUINT,
                                  5000,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SLIDESHOW_TIMEOUT,
                                     pspec);

    pspec = g_param_spec_boxed   ("bgcolor",
                                  "",
                                  "",
                                  GDK_TYPE_COLOR,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_BGCOLOR,
                                     pspec);

    pspec = g_param_spec_boolean ("bgcolor-override",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_BGCOLOR_OVERRIDE,
                                     pspec);

    pspec = g_param_spec_boxed   ("bgcolor-fullscreen",
                                  "",
                                  "",
                                  GDK_TYPE_COLOR,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_BGCOLOR_FULLSCREEN,
                                     pspec);
}

/**
 * rstto_settings_dispose:
 * @object:
 *
 */
static void
rstto_settings_dispose (GObject *object)
{
    RsttoSettings *settings = RSTTO_SETTINGS (object);

    if (settings->priv)
    {
        g_free (settings->priv);
        settings->priv = NULL;
    }
}

/**
 * rstto_settings_finalize:
 * @object:
 *
 */
static void
rstto_settings_finalize (GObject *object)
{
    gchar *accelmap_path = NULL;
    /*RsttoSettings *settings = RSTTO_SETTINGS (object);*/

    accelmap_path = xfce_resource_save_location (XFCE_RESOURCE_CONFIG, "ristretto/accels.scm", TRUE);
    if (accelmap_path)
    {
        gtk_accel_map_save (accelmap_path);
        g_free (accelmap_path);
        accelmap_path = NULL;
    }
    
}



/**
 * rstto_settings_new:
 *
 *
 * Singleton
 */
RsttoSettings *
rstto_settings_new ()
{
    if (settings_object == NULL)
    {
        settings_object = g_object_new (RSTTO_TYPE_SETTINGS, NULL);
    }
    else
    {
        g_object_ref (settings_object);
    }

    return settings_object;
}


static void
rstto_settings_set_property    (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    GdkColor *color;
    RsttoSettings *settings = RSTTO_SETTINGS (object);

    switch (property_id)
    {
        case PROP_SHOW_TOOLBAR:
            settings->priv->show_toolbar = g_value_get_boolean (value);
            break;
        case PROP_TOOLBAR_OPEN:
            settings->priv->toolbar_open = g_value_dup_string (value);
            break;
        case PROP_PRELOAD_IMAGES:
            settings->priv->preload_images = g_value_get_boolean (value);
            break;
        case PROP_ENABLE_CACHE:
            settings->priv->enable_cache = g_value_get_boolean (value);
            break;
        case PROP_IMAGE_QUALITY:
            settings->priv->image_quality = g_value_get_uint (value);
            break;
        case PROP_CACHE_SIZE:
            settings->priv->cache_size = g_value_get_uint (value);
            break;
        case PROP_WINDOW_WIDTH:
            settings->priv->window_width = g_value_get_uint (value);
            break;
        case PROP_WINDOW_HEIGHT:
            settings->priv->window_height = g_value_get_uint (value);
            break;
        case PROP_BGCOLOR:
            color = g_value_get_boxed (value);
            settings->priv->bgcolor->red = color->red;
            settings->priv->bgcolor->green = color->green;
            settings->priv->bgcolor->blue = color->blue;
            break;
        case PROP_BGCOLOR_OVERRIDE:
            settings->priv->bgcolor_override = g_value_get_boolean (value);
            break;
        case PROP_CURRENT_URI:
            if (settings->priv->last_file_path)
                g_free (settings->priv->last_file_path);
            settings->priv->last_file_path = g_value_dup_string (value);
            break;
        case PROP_SLIDESHOW_TIMEOUT:
            settings->priv->slideshow_timeout = g_value_get_uint (value);
            break;
        case PROP_BGCOLOR_FULLSCREEN:
            color = g_value_get_boxed (value);
            settings->priv->bgcolor_fullscreen->red = color->red;
            settings->priv->bgcolor_fullscreen->green = color->green;
            settings->priv->bgcolor_fullscreen->blue = color->blue;
            break;
        case PROP_SCROLLWHEEL_ACTION:
            if (settings->priv->scrollwheel_action)
                g_free (settings->priv->scrollwheel_action);
            settings->priv->scrollwheel_action = g_value_dup_string (value);
            break;
        default:
            break;
    }

}

static void
rstto_settings_get_property    (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    RsttoSettings *settings = RSTTO_SETTINGS (object);

    switch (property_id)
    {
        case PROP_SHOW_TOOLBAR:
            g_value_set_boolean (value, settings->priv->show_toolbar);
            break;
        case PROP_TOOLBAR_OPEN:
            g_value_set_string (value, settings->priv->toolbar_open);
            break;
        case PROP_PRELOAD_IMAGES:
            g_value_set_boolean (value, settings->priv->preload_images);
            break;
        case PROP_ENABLE_CACHE:
            g_value_set_boolean (value, settings->priv->enable_cache);
            break;
        case PROP_IMAGE_QUALITY:
            g_value_set_uint (value, settings->priv->image_quality);
            break;
        case PROP_CACHE_SIZE:
            g_value_set_uint (value, settings->priv->cache_size);
            break;
        case PROP_WINDOW_WIDTH:
            g_value_set_uint (value, settings->priv->window_width);
            break;
        case PROP_WINDOW_HEIGHT:
            g_value_set_uint (value, settings->priv->window_height);
            break;
        case PROP_CURRENT_URI:
            g_value_set_string (value, settings->priv->last_file_path);
            break;
        case PROP_SLIDESHOW_TIMEOUT:
            g_value_set_uint (value, settings->priv->slideshow_timeout);
            break;
        case PROP_BGCOLOR_FULLSCREEN:
            g_value_set_boxed (value, settings->priv->bgcolor_fullscreen);
            break;
        case PROP_BGCOLOR:
            g_value_set_boxed (value, settings->priv->bgcolor);
            break;
        case PROP_BGCOLOR_OVERRIDE:
            g_value_set_boolean (value, settings->priv->bgcolor_override);
            break;
        case PROP_SCROLLWHEEL_ACTION:
            g_value_set_string (value, settings->priv->scrollwheel_action);
            break;
        default:
            break;

    }
}

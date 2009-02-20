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
    PROP_WINDOW_WIDTH,
    PROP_WINDOW_HEIGHT,
    PROP_CURRENT_URI,
    PROP_SLIDESHOW_TIMEOUT,
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

    gboolean show_toolbar;
    guint    window_width;
    guint    window_height;
    gchar   *last_file_path;
    guint    slideshow_timeout;
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

    xfconf_g_property_bind (settings->priv->channel, "/window/width", G_TYPE_UINT, settings, "window-width");
    xfconf_g_property_bind (settings->priv->channel, "/window/height", G_TYPE_UINT, settings, "window-height");
    xfconf_g_property_bind (settings->priv->channel, "/window/show-toolbar", G_TYPE_BOOLEAN, settings, "show-toolbar");
    xfconf_g_property_bind (settings->priv->channel, "/file/current-uri", G_TYPE_STRING, settings, "current-uri");
    xfconf_g_property_bind (settings->priv->channel, "/slideshow/timeout", G_TYPE_UINT, settings, "slideshow-timeout");
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

    pspec = g_param_spec_boolean ("show-toolbar",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SHOW_TOOLBAR,
                                     pspec);

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

    pspec = g_param_spec_string  ("current-uri",
                                  "",
                                  "",
                                  "file://~/",
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
    RsttoSettings *settings = RSTTO_SETTINGS (object);

    switch (property_id)
    {
        case PROP_SHOW_TOOLBAR:
            settings->priv->show_toolbar = g_value_get_boolean (value);
            break;
        case PROP_WINDOW_WIDTH:
            settings->priv->window_width = g_value_get_uint (value);
            break;
        case PROP_WINDOW_HEIGHT:
            settings->priv->window_height = g_value_get_uint (value);
            break;
        case PROP_CURRENT_URI:
            if (settings->priv->last_file_path)
                g_free (settings->priv->last_file_path);
            settings->priv->last_file_path = g_value_dup_string (value);
            break;
        case PROP_SLIDESHOW_TIMEOUT:
            settings->priv->slideshow_timeout = g_value_get_uint (value);
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
        default:
            break;

    }
}

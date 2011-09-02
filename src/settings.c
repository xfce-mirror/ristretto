/*
 *  Copyright (c) Stephan Arts 2009-2010 <stephan@xfce.org>
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
    PROP_SHOW_FILE_TOOLBAR,
    PROP_SHOW_NAV_TOOLBAR,
    PROP_NAVBAR_POSITION,
    PROP_SHOW_THUMBNAILBAR,
    PROP_HIDE_THUMBNAILBAR_FULLSCREEN,
    PROP_TOOLBAR_OPEN,
    PROP_IMAGE_QUALITY,
    PROP_WINDOW_WIDTH,
    PROP_WINDOW_HEIGHT,
    PROP_BGCOLOR,
    PROP_BGCOLOR_OVERRIDE,
    PROP_BGCOLOR_FULLSCREEN,
    PROP_CURRENT_URI,
    PROP_SLIDESHOW_TIMEOUT,
    PROP_SCROLLWHEEL_PRIMARY_ACTION,
    PROP_SCROLLWHEEL_SECONDARY_ACTION,
    PROP_OPEN_ENTIRE_FOLDER,
    PROP_WRAP_IMAGES,
    PROP_THUMBNAILBAR_SIZE,
    PROP_DESKTOP_TYPE,
    PROP_REVERT_ZOOM_DIRECTION,
};

GType
rstto_settings_get_type (void)
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

    gboolean  show_file_toolbar;
    gboolean  show_nav_toolbar;
    gboolean  show_thumbnailbar;
    gboolean  hide_thumbnailbar_fullscreen;
    gboolean  open_entire_folder;
    gchar    *navigationbar_position;
    gboolean  revert_zoom_direction;
    guint     image_quality;
    guint     window_width;
    guint     window_height;
    gchar    *last_file_path;
    guint     slideshow_timeout;
    GdkColor *bgcolor;
    gboolean  bgcolor_override;
    GdkColor *bgcolor_fullscreen;
    gchar    *scrollwheel_primary_action;
    gchar    *scrollwheel_secondary_action;
    gboolean  wrap_images;
    gint      thumbnailbar_size;
    gchar    *desktop_type;
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
    
    settings->priv->slideshow_timeout = 5;
    settings->priv->bgcolor = g_new0 (GdkColor, 1);
    settings->priv->bgcolor_fullscreen = g_new0 (GdkColor, 1);
    settings->priv->image_quality = 2000000;
    settings->priv->navigationbar_position = g_strdup ("bottom");
    settings->priv->show_file_toolbar = TRUE;
    settings->priv->show_nav_toolbar = TRUE;
    settings->priv->window_width = 600;
    settings->priv->window_height = 400;
    settings->priv->wrap_images = TRUE;

    xfconf_g_property_bind (settings->priv->channel, "/window/width", G_TYPE_UINT, settings, "window-width");
    xfconf_g_property_bind (settings->priv->channel, "/window/height", G_TYPE_UINT, settings, "window-height");

    xfconf_g_property_bind (settings->priv->channel, "/file/current-uri", G_TYPE_STRING, settings, "current-uri");
    xfconf_g_property_bind (settings->priv->channel, "/file/open-entire-folder", G_TYPE_BOOLEAN, settings, "open-entire-folder");

    xfconf_g_property_bind (settings->priv->channel, "/window/show-file-toolbar", G_TYPE_BOOLEAN, settings, "show-file-toolbar");
    xfconf_g_property_bind (settings->priv->channel, "/window/show-navigation-toolbar", G_TYPE_BOOLEAN, settings, "show-nav-toolbar");
    xfconf_g_property_bind (settings->priv->channel, "/window/show-thumbnailbar", G_TYPE_BOOLEAN, settings, "show-thumbnailbar");
    xfconf_g_property_bind (settings->priv->channel, "/window/hide-thumbnailbar-fullscreen", G_TYPE_BOOLEAN, settings, "hide-thumbnailbar-fullscreen");
    xfconf_g_property_bind (settings->priv->channel, "/window/navigationbar-position", G_TYPE_STRING, settings, "navigationbar-position");
    xfconf_g_property_bind (settings->priv->channel, "/window/scrollwheel-primary-action", G_TYPE_STRING, settings, "scrollwheel-primary-action");
    xfconf_g_property_bind (settings->priv->channel, "/window/scrollwheel-secondary-action", G_TYPE_STRING, settings, "scrollwheel-secondary-action");

    xfconf_g_property_bind (settings->priv->channel, "/slideshow/timeout", G_TYPE_UINT, settings, "slideshow-timeout");

    xfconf_g_property_bind_gdkcolor (settings->priv->channel, "/window/bgcolor", settings, "bgcolor");
    xfconf_g_property_bind (settings->priv->channel, "/window/bgcolor-override", G_TYPE_BOOLEAN, settings, "bgcolor-override");

    xfconf_g_property_bind_gdkcolor (settings->priv->channel, "/window/bgcolor-fullscreen", settings, "bgcolor-fullscreen");
    xfconf_g_property_bind (settings->priv->channel, "/window/revert-zoom-direction", G_TYPE_BOOLEAN, settings, "revert-zoom-direction");
    xfconf_g_property_bind (settings->priv->channel, "/image/quality", G_TYPE_UINT, settings, "image-quality");
    xfconf_g_property_bind (settings->priv->channel, "/image/wrap", G_TYPE_BOOLEAN, settings, "wrap-images");
    xfconf_g_property_bind (settings->priv->channel, "/window/thumbnailbar/size", G_TYPE_INT, settings, "thumbnailbar-size");
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

    pspec = g_param_spec_boolean ("show-file-toolbar",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SHOW_FILE_TOOLBAR,
                                     pspec);

    pspec = g_param_spec_boolean ("show-nav-toolbar",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SHOW_NAV_TOOLBAR,
                                     pspec);

    pspec = g_param_spec_boolean ("show-thumbnailbar",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SHOW_THUMBNAILBAR,
                                     pspec);

    pspec = g_param_spec_boolean ("hide-thumbnailbar-fullscreen",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_HIDE_THUMBNAILBAR_FULLSCREEN,
                                     pspec);

    pspec = g_param_spec_boolean ("open-entire-folder",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_OPEN_ENTIRE_FOLDER,
                                     pspec);

    pspec = g_param_spec_string  ("navigationbar-position",
                                  "",
                                  "",
                                  "bottom",
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_NAVBAR_POSITION,
                                     pspec);

    pspec = g_param_spec_boolean ("revert-zoom-direction",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_REVERT_ZOOM_DIRECTION,
                                     pspec);

    pspec = g_param_spec_uint    ("image-quality",
                                  "",
                                  "",
                                  0,
                                  50000000,
                                  2000000,
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

    pspec = g_param_spec_string  ("scrollwheel-primary-action",
                                  "",
                                  "",
                                  "navigate",
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SCROLLWHEEL_PRIMARY_ACTION,
                                     pspec);

    pspec = g_param_spec_string  ("scrollwheel-secondary-action",
                                  "",
                                  "",
                                  "zoom",
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_SCROLLWHEEL_SECONDARY_ACTION,
                                     pspec);

    pspec = g_param_spec_uint    ("slideshow-timeout",
                                  "",
                                  "",
                                  1,
                                  300,
                                  5,
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

    pspec = g_param_spec_boolean ("wrap-images",
                                  "",
                                  "",
                                  TRUE,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_WRAP_IMAGES,
                                     pspec);

    pspec = g_param_spec_int    ("thumbnailbar-size",
                                  "",
                                  "",
                                  -1,
                                  4000,
                                  70,
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_THUMBNAILBAR_SIZE,
                                     pspec);

    pspec = g_param_spec_string  ("desktop-type",
                                  "",
                                  "",
                                  "xfce",
                                  G_PARAM_READWRITE);
    g_object_class_install_property (object_class,
                                     PROP_DESKTOP_TYPE,
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
        if (settings->priv->channel)
        {
            xfconf_g_property_unbind_all (settings->priv->channel);
            g_object_unref (settings->priv->channel);
            settings->priv->channel = NULL;
        }
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
rstto_settings_new (void)
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
    const gchar *str_val = NULL;
    RsttoSettings *settings = RSTTO_SETTINGS (object);

    switch (property_id)
    {
        case PROP_SHOW_FILE_TOOLBAR:
            settings->priv->show_file_toolbar = g_value_get_boolean (value);
            break;
        case PROP_SHOW_NAV_TOOLBAR:
            settings->priv->show_nav_toolbar = g_value_get_boolean (value);
            break;
        case PROP_SHOW_THUMBNAILBAR:
            settings->priv->show_thumbnailbar = g_value_get_boolean (value);
            break;
        case PROP_HIDE_THUMBNAILBAR_FULLSCREEN:
            settings->priv->hide_thumbnailbar_fullscreen = g_value_get_boolean (value);
            break;
        case PROP_OPEN_ENTIRE_FOLDER:
            settings->priv->open_entire_folder= g_value_get_boolean (value);
            break;
        case PROP_NAVBAR_POSITION:
            str_val = g_value_get_string (value);

            if ((!g_strcasecmp (str_val, "left")) ||
                (!g_strcasecmp (str_val, "right")) ||
                (!g_strcasecmp (str_val, "bottom")) ||
                (!g_strcasecmp (str_val, "top")))
            {
                if (settings->priv->navigationbar_position)
                    g_free (settings->priv->navigationbar_position);
                settings->priv->navigationbar_position = g_strdup (str_val);
            }
            break;
        case PROP_REVERT_ZOOM_DIRECTION:
            settings->priv->revert_zoom_direction = g_value_get_boolean (value);
            break;
        case PROP_IMAGE_QUALITY:
            settings->priv->image_quality = g_value_get_uint (value);
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
        case PROP_SCROLLWHEEL_PRIMARY_ACTION:
            if (settings->priv->scrollwheel_primary_action)
                g_free (settings->priv->scrollwheel_primary_action);
            settings->priv->scrollwheel_primary_action = g_value_dup_string (value);
            break;
        case PROP_SCROLLWHEEL_SECONDARY_ACTION:
            if (settings->priv->scrollwheel_secondary_action)
                g_free (settings->priv->scrollwheel_secondary_action);
            settings->priv->scrollwheel_secondary_action = g_value_dup_string (value);
            break;
        case PROP_WRAP_IMAGES:
            settings->priv->wrap_images = g_value_get_boolean (value);
            break;
        case PROP_THUMBNAILBAR_SIZE:
            settings->priv->thumbnailbar_size = g_value_get_int (value);
            break;
        case PROP_DESKTOP_TYPE:
            if (settings->priv->desktop_type)
                g_free (settings->priv->desktop_type);
            settings->priv->desktop_type = g_value_dup_string (value);
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
        case PROP_SHOW_FILE_TOOLBAR:
            g_value_set_boolean (value, settings->priv->show_file_toolbar);
            break;
        case PROP_SHOW_NAV_TOOLBAR:
            g_value_set_boolean (value, settings->priv->show_nav_toolbar);
            break;
        case PROP_SHOW_THUMBNAILBAR:
            g_value_set_boolean (value, settings->priv->show_thumbnailbar);
            break;
        case PROP_HIDE_THUMBNAILBAR_FULLSCREEN:
            g_value_set_boolean (value, settings->priv->hide_thumbnailbar_fullscreen);
            break;
        case PROP_OPEN_ENTIRE_FOLDER:
            g_value_set_boolean (value, settings->priv->open_entire_folder);
            break;
        case PROP_NAVBAR_POSITION:
            g_value_set_string (value, settings->priv->navigationbar_position);
            break;
        case PROP_REVERT_ZOOM_DIRECTION:
            g_value_set_boolean (value, settings->priv->revert_zoom_direction);
            break;
        case PROP_IMAGE_QUALITY:
            g_value_set_uint (value, settings->priv->image_quality);
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
        case PROP_SCROLLWHEEL_PRIMARY_ACTION:
            g_value_set_string (value, settings->priv->scrollwheel_primary_action);
            break;
        case PROP_SCROLLWHEEL_SECONDARY_ACTION:
            g_value_set_string (value, settings->priv->scrollwheel_secondary_action);
        case PROP_WRAP_IMAGES:
            g_value_set_boolean (value, settings->priv->wrap_images);
            break;
        case PROP_THUMBNAILBAR_SIZE:
            g_value_set_int (value, settings->priv->thumbnailbar_size);
            break;
        case PROP_DESKTOP_TYPE:
            g_value_set_string (value, settings->priv->desktop_type);
            break;
        default:
            break;

    }
}

void
rstto_settings_set_navbar_position (RsttoSettings *settings, guint pos)
{
    GValue val = {0, };
    g_value_init (&val, G_TYPE_STRING);
 
    switch (pos)
    {
        default:
            g_value_set_string (&val, "left");
            break;
        case 1:
            g_value_set_string (&val, "right");
            break;
        case 2:
            g_value_set_string (&val, "top");
            break;
        case 3:
            g_value_set_string (&val, "bottom");
            break;
    }

    g_object_set_property (G_OBJECT(settings), "navigationbar-position", &val);

    g_value_reset (&val);
}

guint
rstto_settings_get_navbar_position (RsttoSettings *settings) {
    if (settings->priv->navigationbar_position == NULL)
        return 0;

    if (!strcmp (settings->priv->navigationbar_position, "left"))
        return 0;
    if (!strcmp (settings->priv->navigationbar_position, "right"))
        return 1;
    if (!strcmp (settings->priv->navigationbar_position, "top"))
        return 2;
    if (!strcmp (settings->priv->navigationbar_position, "bottom"))
        return 3;

    return 0;
}

/** Convenience functions */
void
rstto_settings_set_uint_property (RsttoSettings *settings,
                                  const gchar *property_name,
                                  guint value)
{
    GValue val = {0, };
    g_value_init (&val, G_TYPE_UINT);

    g_value_set_uint (&val, value);

    g_object_set_property (G_OBJECT(settings), property_name, &val);

    g_value_reset (&val);
}

guint
rstto_settings_get_uint_property (RsttoSettings *settings,
                                  const gchar *property_name)
{
    guint value;
    GValue val = {0, };
    g_value_init (&val, G_TYPE_UINT);

    g_object_get_property (G_OBJECT(settings), property_name, &val);
    value = g_value_get_uint (&val);

    g_value_reset (&val);

    return value;
}

void
rstto_settings_set_int_property (RsttoSettings *settings,
                                  const gchar *property_name,
                                  gint value)
{
    GValue val = {0, };
    g_value_init (&val, G_TYPE_INT);

    g_value_set_int (&val, value);

    g_object_set_property (G_OBJECT(settings), property_name, &val);

    g_value_reset (&val);
}

gint
rstto_settings_get_int_property (RsttoSettings *settings,
                                  const gchar *property_name)
{
    gint value;
    GValue val = {0, };
    g_value_init (&val, G_TYPE_INT);

    g_object_get_property (G_OBJECT(settings), property_name, &val);
    value = g_value_get_int (&val);

    g_value_reset (&val);

    return value;
}

void
rstto_settings_set_string_property (RsttoSettings *settings,
                                    const gchar *property_name,
                                    const gchar *value)
{
    GValue val = {0, };
    g_value_init (&val, G_TYPE_STRING);

    g_value_set_string (&val, value);

    g_object_set_property (G_OBJECT(settings), property_name, &val);

    g_value_reset (&val);
}

gchar *
rstto_settings_get_string_property (RsttoSettings *settings,
                                  const gchar *property_name)
{
    gchar *value = NULL;
    GValue val = {0, };
    g_value_init (&val, G_TYPE_STRING);

    g_object_get_property (G_OBJECT(settings), property_name, &val);
    value = g_value_dup_string (&val);

    g_value_reset (&val);

    return value;
}

void
rstto_settings_set_boolean_property (RsttoSettings *settings,
                                     const gchar *property_name,
                                     gboolean value)
{
    GValue val = {0, };
    g_value_init (&val, G_TYPE_BOOLEAN);

    g_value_set_boolean (&val, value);

    g_object_set_property (G_OBJECT(settings), property_name, &val);

    g_value_reset (&val);
}

gboolean
rstto_settings_get_boolean_property (RsttoSettings *settings,
                                     const gchar *property_name)
{
    gboolean value;
    GValue val = {0, };
    g_value_init (&val, G_TYPE_BOOLEAN);

    g_object_get_property (G_OBJECT(settings), property_name, &val);
    value = g_value_get_boolean (&val);

    g_value_reset (&val);

    return value;
}

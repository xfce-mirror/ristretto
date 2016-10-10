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

#ifndef __RISTRETTO_SETTINGS_H__
#define __RISTRETTO_SETTINGS_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RSTTO_TYPE_SETTINGS rstto_settings_get_type()

#define RSTTO_SETTINGS(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_SETTINGS, \
                RsttoSettings))

#define RSTTO_IS_SETTINGS(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_SETTINGS))

#define RSTTO_SETTINGS_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_SETTINGS, \
                RsttoSettingsClass))

#define RSTTO_IS_SETTINGS_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_SETTINGS()))


typedef struct _RsttoSettings RsttoSettings;
typedef struct _RsttoSettingsPriv RsttoSettingsPriv;

struct _RsttoSettings
{
    GObject parent;

    RsttoSettingsPriv *priv;
};

typedef struct _RsttoSettingsClass RsttoSettingsClass;

struct _RsttoSettingsClass
{
    GObjectClass parent_class;
};

RsttoSettings *rstto_settings_new (void);
GType          rstto_settings_get_type (void);

void   rstto_settings_set_navbar_position (RsttoSettings *, guint);
guint  rstto_settings_get_navbar_position (RsttoSettings *);

void        rstto_settings_set_uint_property (RsttoSettings *, const gchar *, guint);
guint       rstto_settings_get_uint_property (RsttoSettings *, const gchar *);
void        rstto_settings_set_int_property (RsttoSettings *, const gchar *, gint);
gint        rstto_settings_get_int_property (RsttoSettings *, const gchar *);
void        rstto_settings_set_string_property (RsttoSettings *, const gchar *, const gchar *);
gchar      *rstto_settings_get_string_property (RsttoSettings *, const gchar *);
void        rstto_settings_set_boolean_property (RsttoSettings *, const gchar *, gboolean);
gboolean    rstto_settings_get_boolean_property (RsttoSettings *, const gchar *);

G_END_DECLS

#endif /* __RISTRETTO_SETTINGS_H__ */

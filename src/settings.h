/*
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

#ifndef __RISTRETTO_SETTINGS_H__
#define __RISTRETTO_SETTINGS_H__

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

G_END_DECLS

#endif /* __RISTRETTO_SETTINGS_H__ */

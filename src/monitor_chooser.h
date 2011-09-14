/*
 *  Copyright (C) Stephan Arts 2006-2010 <stephan@xfce.org>
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

#ifndef __RISTRETTO_MONITOR_CHOOSER_H__
#define __RISTRETTO_MONITOR_CHOOSER_H__

G_BEGIN_DECLS

typedef enum
{
    MONITOR_STYLE_AUTOMATIC = 0,
    MONITOR_STYLE_CENTERED,
    MONITOR_STYLE_TILED,
    MONITOR_STYLE_STRETCHED,
    MONITOR_STYLE_SCALED,
    MONITOR_STYLE_ZOOMED
} RsttoMonitorStyle;

#define RSTTO_TYPE_MONITOR_CHOOSER rstto_monitor_chooser_get_type()

#define RSTTO_MONITOR_CHOOSER(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_MONITOR_CHOOSER, \
                RsttoMonitorChooser))

#define RSTTO_IS_MONITOR_CHOOSER(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_MONITOR_CHOOSER))

#define RSTTO_MONITOR_CHOOSER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_MONITOR_CHOOSER, \
                RsttoMonitorChooserClass))

#define RSTTO_IS_MONITOR_CHOOSER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_MONITOR_CHOOSER()))

typedef struct _RsttoMonitorChooserPriv RsttoMonitorChooserPriv;

typedef struct _RsttoMonitorChooser RsttoMonitorChooser;

struct _RsttoMonitorChooser
{
    GtkWidget parent;
    RsttoMonitorChooserPriv *priv;
};

typedef struct _RsttoMonitorChooserClass RsttoMonitorChooserClass;

struct _RsttoMonitorChooserClass
{
    GtkWidgetClass  parent_class;
};

GType      rstto_monitor_chooser_get_type();

GtkWidget  *rstto_monitor_chooser_new ();

gint
rstto_monitor_chooser_add ( 
        RsttoMonitorChooser *,
        gint width,
        gint height);

gint
rstto_monitor_chooser_set_pixbuf (
        RsttoMonitorChooser *,
        gint monitor_id,
        GdkPixbuf *pixbuf,
        GError **);

gint
rstto_monitor_chooser_get_selected (
        RsttoMonitorChooser *);

gboolean
rstto_monitor_chooser_set_style (
        RsttoMonitorChooser *chooser,
        gint monitor_id,
        RsttoMonitorStyle style );

G_END_DECLS

#endif /* __RISTRETTO_MONITOR_CHOOSER_H__ */

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

#ifndef __RISTRETTO_THUMBNAILER_H__
#define __RISTRETTO_THUMBNAILER_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RSTTO_TYPE_THUMBNAILER rstto_thumbnailer_get_type()

#define RSTTO_THUMBNAILER(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_THUMBNAILER, \
                RsttoThumbnailer))

#define RSTTO_IS_THUMBNAILER(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_THUMBNAILER))

#define RSTTO_THUMBNAILER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_THUMBNAILER, \
                RsttoThumbnailerClass))

#define RSTTO_IS_THUMBNAILER_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_THUMBNAILER()))


typedef struct _RsttoThumbnailer RsttoThumbnailer;
typedef struct _RsttoThumbnailerPriv RsttoThumbnailerPriv;

struct _RsttoThumbnailer
{
    GObject parent;

    RsttoThumbnailerPriv *priv;
};

typedef struct _RsttoThumbnailerClass RsttoThumbnailerClass;

struct _RsttoThumbnailerClass
{
    GObjectClass parent_class;
};

RsttoThumbnailer *
rstto_thumbnailer_new (void);

GType
rstto_thumbnailer_get_type (void);

void
rstto_thumbnailer_queue_file (
        RsttoThumbnailer *thumbnailer,
        RsttoFile *file);
void
rstto_thumbnailer_dequeue_file (
        RsttoThumbnailer *thumbnailer,
        RsttoFile *file );

G_END_DECLS

#endif /* __RISTRETTO_THUMBNAILER_H__ */

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

#ifndef __RISTRETTO_MIME_DB_H__
#define __RISTRETTO_MIME_DB_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RSTTO_TYPE_MIME_DB rstto_mime_db_get_type()

#define RSTTO_MIME_DB(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_MIME_DB, \
                RsttoMimeDB))

#define RSTTO_IS_MIME_DB(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_MIME_DB))

#define RSTTO_MIME_DB_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_MIME_DB, \
                RsttoMimeDBClass))

#define RSTTO_IS_MIME_DB_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_MIME_DB()))


typedef struct _RsttoMimeDB RsttoMimeDB;
typedef struct _RsttoMimeDBPriv RsttoMimeDBPriv;

struct _RsttoMimeDB
{
    GObject parent;

    RsttoMimeDBPriv *priv;
};

typedef struct _RsttoMimeDBClass RsttoMimeDBClass;

struct _RsttoMimeDBClass
{
    GObjectClass parent_class;
};

GType
rstto_mime_db_get_type (void);

RsttoMimeDB *
rstto_mime_db_new (const gchar *path, GError **);

const gchar *
rstto_mime_db_lookup (
        RsttoMimeDB *mime_db,
        const gchar *key);

void
rstto_mime_db_store (
        RsttoMimeDB *mime_db,
        const gchar *key,
        const gchar *value);

G_END_DECLS

#endif /* __RSTTO_MIME_DB_H__ */

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
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>


#include <glib.h>
#include <gio/gio.h>
#include <locale.h>

#include <libxfce4util/libxfce4util.h>

#include "mime_db.h"

static void
rstto_mime_db_init (GObject *);
static void
rstto_mime_db_class_init (GObjectClass *);

static void
rstto_mime_db_dispose (GObject *object);
static void
rstto_mime_db_finalize (GObject *object);

static void
rstto_mime_db_set_property (
        GObject      *object,
        guint         property_id,
        const GValue *value,
        GParamSpec   *pspec );

static void
rstto_mime_db_get_property (
        GObject    *object,
        guint       property_id,
        GValue     *value,
        GParamSpec *pspec);

enum
{
    PROP_0,
    PROP_FILE
};

static GObjectClass *parent_class = NULL;

GType
rstto_mime_db_get_type (void)
{
    static GType rstto_mime_db_type = 0;

    if (!rstto_mime_db_type)
    {
        static const GTypeInfo rstto_mime_db_info = 
        {
            sizeof (RsttoMimeDBClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_mime_db_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoMimeDB),
            0,
            (GInstanceInitFunc) rstto_mime_db_init,
            NULL
        };

        rstto_mime_db_type = g_type_register_static (G_TYPE_OBJECT, "RsttoMimeDB", &rstto_mime_db_info, 0);
    }
    return rstto_mime_db_type;
}

struct _RsttoMimeDBPriv
{
    GFile   *file;
    XfceRc  *rc;
};

static void
rstto_mime_db_init (GObject *object)
{
    RsttoMimeDB *mime_db = RSTTO_MIME_DB (object);

    mime_db->priv = g_new0 (RsttoMimeDBPriv, 1);
}

static void
rstto_mime_db_class_init (GObjectClass *object_class)
{
    RsttoMimeDBClass *mime_db_class = RSTTO_MIME_DB_CLASS (object_class);

    parent_class = g_type_class_peek_parent (mime_db_class);

    object_class->dispose = rstto_mime_db_dispose;
    object_class->finalize = rstto_mime_db_finalize;

    object_class->set_property = rstto_mime_db_set_property;
    object_class->get_property = rstto_mime_db_get_property;

}

/**
 * rstto_mime_db_dispose:
 * @object:
 *
 */
static void
rstto_mime_db_dispose (GObject *object)
{
    RsttoMimeDB *mime_db = RSTTO_MIME_DB (object);

    if (mime_db->priv)
    {
        xfce_rc_close (mime_db->priv->rc);
        g_free (mime_db->priv);
        mime_db->priv = NULL;
    }
}

/**
 * rstto_mime_db_finalize:
 * @object:
 *
 */
static void
rstto_mime_db_finalize (GObject *object)
{
}

static void
rstto_mime_db_set_property (
        GObject      *object,
        guint         property_id,
        const GValue *value,
        GParamSpec   *pspec )
{
    switch (property_id)
    {
        case PROP_FILE:
            break;
        default:
            break;
    }
}

static void
rstto_mime_db_get_property (
        GObject    *object,
        guint       property_id,
        GValue     *value,
        GParamSpec *pspec)
{

    switch (property_id)
    {
        case PROP_FILE:
            break;
        default:
            break;
    }

}

RsttoMimeDB *
rstto_mime_db_new (const gchar *path, GError **error)
{
    
    RsttoMimeDB   *mime_db = NULL;
    XfceRc        *rc = NULL;

    rc = xfce_rc_simple_open (path, FALSE);

    if (rc != NULL)
    {
        mime_db = g_object_new (RSTTO_TYPE_MIME_DB, NULL);
        mime_db->priv->rc = rc;

        xfce_rc_set_group (rc, "preferred-editor");
    }

    return mime_db;
}


const gchar *
rstto_mime_db_lookup (
        RsttoMimeDB *mime_db,
        const gchar *key)
{
    return xfce_rc_read_entry (mime_db->priv->rc, key, NULL);
}


void
rstto_mime_db_store (
        RsttoMimeDB *mime_db,
        const gchar *key,
        const gchar *value)
{
    xfce_rc_write_entry (mime_db->priv->rc, key, value);
    xfce_rc_flush (mime_db->priv->rc);
}


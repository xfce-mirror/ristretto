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

#ifndef __RISTRETTO_APP_MENU_ITEM_H__
#define __RISTRETTO_APP_MENU_ITEM_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RSTTO_TYPE_APP_MENU_ITEM rstto_app_menu_item_get_type()

#define RSTTO_APP_MENU_ITEM(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_APP_MENU_ITEM, \
                RsttoAppMenuItem))

#define RSTTO_IS_APP_MENU_ITEM(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_APP_MENU_ITEM))

#define RSTTO_APP_MENU_ITEM_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_APP_MENU_ITEM, \
                RsttoAppMenuItemClass))

#define RSTTO_IS_APP_MENU_ITEM_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_APP_MENU_ITEM()))

typedef struct _RsttoAppMenuItemPriv RsttoAppMenuItemPriv;

typedef struct _RsttoAppMenuItem RsttoAppMenuItem;

struct _RsttoAppMenuItem
{
    GtkImageMenuItem parent;
    RsttoAppMenuItemPriv *priv;
};

typedef struct _RsttoAppMenuItemClass RsttoAppMenuItemClass;

struct _RsttoAppMenuItemClass
{
    GtkImageMenuItemClass  parent_class;
};

GType       rstto_app_menu_item_get_type (void);
GtkWidget  *rstto_app_menu_item_new (GAppInfo *app_info, GFile *file);

G_END_DECLS

#endif /* __RISTRETTO_APP_MENU_ITEM_H__ */

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

#ifndef __RISTRETTO_PREFERENCES_DIALOG_H__
#define __RISTRETTO_PREFERENCES_DIALOG_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RSTTO_TYPE_PREFERENCES_DIALOG rstto_preferences_dialog_get_type()

#define RSTTO_PREFERENCES_DIALOG(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_PREFERENCES_DIALOG, \
                RsttoPreferencesDialog))

#define RSTTO_PREFERENCES_DIALOG_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_PREFERENCES_DIALOG, \
                RsttoPreferencesDialogClass))

typedef struct _RsttoPreferencesDialog RsttoPreferencesDialog;

typedef struct _RsttoPreferencesDialogPriv RsttoPreferencesDialogPriv;

struct _RsttoPreferencesDialog
{
    GtkDialog parent;
    RsttoPreferencesDialogPriv *priv;
};

typedef struct _RsttoPreferencesDialogClass RsttoPreferencesDialogClass;

struct _RsttoPreferencesDialogClass
{
    GtkDialogClass parent_class;
};

GType
rstto_preferences_dialog_get_type (void);

GtkWidget *
rstto_preferences_dialog_new (GtkWindow *parent);

G_END_DECLS

#endif /* __RISTRETTO_PREFERENCES_DIALOG_H__ */

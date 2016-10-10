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

#ifndef __RISTRETTO_PRIVACY_DIALOG_H__
#define __RISTRETTO_PRIVACY_DIALOG_H__

#include <libxfce4ui/libxfce4ui.h>

G_BEGIN_DECLS

#define RSTTO_TYPE_PRIVACY_DIALOG rstto_privacy_dialog_get_type()

#define RSTTO_PRIVACY_DIALOG(obj)( \
        G_TYPE_CHECK_INSTANCE_CAST ((obj), \
                RSTTO_TYPE_PRIVACY_DIALOG, \
                RsttoPrivacyDialog))

#define RSTTO_IS_PRIVACY_DIALOG(obj)( \
        G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                RSTTO_TYPE_PRIVACY_DIALOG))

#define RSTTO_PRIVACY_DIALOG_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_CAST ((klass), \
                RSTTO_TYPE_PRIVACY_DIALOG, \
                RsttoPrivacyDialogClass))

#define RSTTO_IS_PRIVACY_DIALOG_CLASS(klass)( \
        G_TYPE_CHECK_CLASS_TYPE ((klass), \
                RSTTO_TYPE_PRIVACY_DIALOG()))

typedef struct _RsttoPrivacyDialog RsttoPrivacyDialog;

typedef struct _RsttoPrivacyDialogPriv RsttoPrivacyDialogPriv;

struct _RsttoPrivacyDialog
{
    XfceTitledDialog parent;
    RsttoPrivacyDialogPriv *priv;
};

typedef struct _RsttoPrivacyDialogClass RsttoPrivacyDialogClass;

struct _RsttoPrivacyDialogClass
{
    XfceTitledDialogClass  parent_class;
};

GType
rstto_privacy_dialog_get_type();

GtkWidget *
rstto_privacy_dialog_new (
        GtkWindow *parent,
        GtkRecentManager *recent_manager
        );

G_END_DECLS

#endif /* __RISTRETTO_PRIVACY_DIALOG_H__ */

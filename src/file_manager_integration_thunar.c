/*
 *  Copyright (c) 2025 The Xfce Development Team
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

#include "file_manager_integration_thunar.h"

#include <libxfce4util/libxfce4util.h>
#include <xfconf/xfconf.h>

typedef struct _RsttoFileManagerIntegrationThunarPrivate RsttoFileManagerIntegrationThunarPrivate;

static void
rstto_file_manager_integration_thunar_file_manager_integration_interface_init (RsttoFileManagerIntegrationInterface *iface);

static RsttoSortType
rstto_file_manager_integration_thunar_get_sort_type_from_string (const gchar *column);

static RsttoSortOrder
rstto_file_manager_integration_thunar_get_sort_order_from_string (const gchar *order);

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings_per_dir (RsttoFileManagerIntegrationThunar *self,
                                                                 GFile *dir,
                                                                 RsttoSortType *sort_type,
                                                                 RsttoSortOrder *sort_order);

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings_global (RsttoFileManagerIntegrationThunar *self,
                                                                GFile *dir,
                                                                RsttoSortType *sort_type,
                                                                RsttoSortOrder *sort_order);

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings (RsttoFileManagerIntegration *file_manager_integration,
                                                         GFile *dir,
                                                         RsttoSortType *sort_type,
                                                         RsttoSortOrder *sort_order);

struct _RsttoFileManagerIntegrationThunarPrivate
{
    XfconfChannel *channel;
};

G_DEFINE_TYPE_WITH_CODE (RsttoFileManagerIntegrationThunar, rstto_file_manager_integration_thunar, G_TYPE_OBJECT,
                         G_ADD_PRIVATE (RsttoFileManagerIntegrationThunar)
                         G_IMPLEMENT_INTERFACE (RSTTO_TYPE_FILE_MANAGER_INTEGRATION,
                                                rstto_file_manager_integration_thunar_file_manager_integration_interface_init))

static void
rstto_file_manager_integration_thunar_class_init (RsttoFileManagerIntegrationThunarClass *klass)
{
}

static void
rstto_file_manager_integration_thunar_init (RsttoFileManagerIntegrationThunar *self)
{
    RsttoFileManagerIntegrationThunarPrivate *priv = rstto_file_manager_integration_thunar_get_instance_private (self);

    if (!rstto_settings_get_xfconf_disabled ())
        priv->channel = xfconf_channel_get ("thunar");
}

static void
rstto_file_manager_integration_thunar_file_manager_integration_interface_init (RsttoFileManagerIntegrationInterface *iface)
{
    iface->get_sort_settings = rstto_file_manager_integration_thunar_get_sort_settings;
}

static RsttoSortType
rstto_file_manager_integration_thunar_get_sort_type_from_string (const gchar *column)
{
    if (0 == g_strcmp0 (column, "THUNAR_COLUMN_DATE_MODIFIED"))
        return SORT_TYPE_DATE;
    else if (0 == g_strcmp0 (column, "THUNAR_COLUMN_SIZE"))
        return SORT_TYPE_SIZE;
    else if (0 == g_strcmp0 (column, "THUNAR_COLUMN_TYPE"))
        return SORT_TYPE_TYPE;
    else
        return SORT_TYPE_NAME;
}

static RsttoSortOrder
rstto_file_manager_integration_thunar_get_sort_order_from_string (const gchar *order)
{
    if (0 == g_strcmp0 (order, "GTK_SORT_DESCENDING"))
        return SORT_ORDER_DESC;
    else
        return SORT_ORDER_ASC;
}

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings_per_dir (RsttoFileManagerIntegrationThunar *self,
                                                                 GFile *dir,
                                                                 RsttoSortType *sort_type,
                                                                 RsttoSortOrder *sort_order)
{
    GFileInfo *file_info;
    const gchar *thunar_sort_column;
    const gchar *thunar_sort_order;
    GError *error = NULL;

    file_info = g_file_query_info (dir,
                                   "metadata::thunar-sort-column,metadata::thunar-sort-order",
                                   G_FILE_QUERY_INFO_NONE,
                                   NULL,
                                   &error);

    if (NULL != error)
    {
        g_clear_object (&file_info);
        g_clear_error (&error);
        return FALSE;
    }

    thunar_sort_column = g_file_info_get_attribute_string (file_info, "metadata::thunar-sort-column");
    thunar_sort_order = g_file_info_get_attribute_string (file_info, "metadata::thunar-sort-order");

    if (thunar_sort_column == NULL || thunar_sort_order == NULL)
    {
        g_clear_object (&file_info);
        return rstto_file_manager_integration_thunar_get_sort_settings_global (self, dir, sort_type, sort_order);
    }

    *sort_type = rstto_file_manager_integration_thunar_get_sort_type_from_string (thunar_sort_column);
    *sort_order = rstto_file_manager_integration_thunar_get_sort_order_from_string (thunar_sort_order);

    g_clear_object (&file_info);

    return TRUE;
}

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings_global (RsttoFileManagerIntegrationThunar *self,
                                                                GFile *dir,
                                                                RsttoSortType *sort_type,
                                                                RsttoSortOrder *sort_order)
{
    RsttoFileManagerIntegrationThunarPrivate *priv = rstto_file_manager_integration_thunar_get_instance_private (self);
    gchar *thunar_last_sort_column;
    gchar *thunar_last_sort_order;

    thunar_last_sort_column = xfconf_channel_get_string (priv->channel, "/last-sort-column", NULL);
    thunar_last_sort_order = xfconf_channel_get_string (priv->channel, "/last-sort-order", NULL);

    if (thunar_last_sort_column == NULL || thunar_last_sort_order == NULL)
    {
        g_free (thunar_last_sort_column);
        g_free (thunar_last_sort_order);
        return FALSE;
    }

    *sort_type = rstto_file_manager_integration_thunar_get_sort_type_from_string (thunar_last_sort_column);
    *sort_order = rstto_file_manager_integration_thunar_get_sort_order_from_string (thunar_last_sort_order);

    g_free (thunar_last_sort_column);
    g_free (thunar_last_sort_order);

    return TRUE;
}

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings (RsttoFileManagerIntegration *file_manager_integration,
                                                         GFile *dir,
                                                         RsttoSortType *sort_type,
                                                         RsttoSortOrder *sort_order)
{
    RsttoFileManagerIntegrationThunar *self = RSTTO_FILE_MANAGER_INTEGRATION_THUNAR (file_manager_integration);
    RsttoFileManagerIntegrationThunarPrivate *priv = rstto_file_manager_integration_thunar_get_instance_private (self);
    gboolean settings_per_dir;

    if (rstto_settings_get_xfconf_disabled ())
        return FALSE;

    settings_per_dir = xfconf_channel_get_bool (priv->channel,
                                                "/misc-directory-specific-settings",
                                                FALSE);
    if (settings_per_dir)
        return rstto_file_manager_integration_thunar_get_sort_settings_per_dir (self, dir, sort_type, sort_order);
    else
        return rstto_file_manager_integration_thunar_get_sort_settings_global (self, dir, sort_type, sort_order);
}

RsttoFileManagerIntegration *
rstto_file_manager_integration_thunar_new (void)
{
    return g_object_new (RSTTO_TYPE_FILE_MANAGER_INTEGRATION_THUNAR, NULL);
}

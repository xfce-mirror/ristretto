/*
 *  Copyright (c) Dmitry Petrachkov 2025 <dmitry-petrachkov@outlook.com>
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

enum
{
    PROP_0,
    PROP_DIRECTORY,
    PROP_SORT_TYPE,
    PROP_SORT_ORDER,
};

static void
rstto_file_manager_integration_thunar_finalize (GObject *object);

static void
rstto_file_manager_integration_thunar_get_property (GObject *object,
                                                    guint prop_id,
                                                    GValue *value,
                                                    GParamSpec *pspec);

static void
rstto_file_manager_integration_thunar_set_property (GObject *object,
                                                    guint prop_id,
                                                    const GValue *value,
                                                    GParamSpec *pspec);

static void
rstto_file_manager_integration_thunar_file_manager_integration_interface_init (RsttoFileManagerIntegrationInterface *iface);

static void
rstto_file_manager_integration_thunar_xfconf_property_changed (RsttoFileManagerIntegrationThunar *self,
                                                               const gchar *prop_name,
                                                               const GValue *value);

static void
rstto_file_manager_integration_thunar_set_directory (RsttoFileManagerIntegrationThunar *self,
                                                     GFile *directory);

static RsttoSortType
rstto_file_manager_integration_thunar_get_sort_type_from_string (const gchar *column);

static RsttoSortOrder
rstto_file_manager_integration_thunar_get_sort_order_from_string (const gchar *order);

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings_per_directory (RsttoFileManagerIntegrationThunar *self);

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings_global (RsttoFileManagerIntegrationThunar *self);

static gboolean
rstto_file_manager_integration_thunar_fetch_sort_settings (RsttoFileManagerIntegrationThunar *self);

struct _RsttoFileManagerIntegrationThunar
{
    GObject parent;

    GFile *directory;
    RsttoSortType sort_type;
    RsttoSortOrder sort_order;
    XfconfChannel *channel;
};

G_DEFINE_TYPE_WITH_CODE (RsttoFileManagerIntegrationThunar, rstto_file_manager_integration_thunar, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (RSTTO_TYPE_FILE_MANAGER_INTEGRATION,
                                                rstto_file_manager_integration_thunar_file_manager_integration_interface_init))

static void
rstto_file_manager_integration_thunar_class_init (RsttoFileManagerIntegrationThunarClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->finalize = rstto_file_manager_integration_thunar_finalize;

    object_class->get_property = rstto_file_manager_integration_thunar_get_property;
    object_class->set_property = rstto_file_manager_integration_thunar_set_property;

    g_object_class_override_property (object_class, PROP_DIRECTORY, "directory");
    g_object_class_override_property (object_class, PROP_SORT_TYPE, "sort-type");
    g_object_class_override_property (object_class, PROP_SORT_ORDER, "sort-order");
}

static void
rstto_file_manager_integration_thunar_init (RsttoFileManagerIntegrationThunar *self)
{
    self->sort_type = -1;
    self->sort_order = -1;

    if (!rstto_settings_get_xfconf_disabled ())
    {
        self->channel = xfconf_channel_get ("thunar");
        g_signal_connect_swapped (self->channel, "property-changed",
                                  G_CALLBACK (rstto_file_manager_integration_thunar_xfconf_property_changed),
                                  self);
    }
}

static void
rstto_file_manager_integration_thunar_finalize (GObject *object)
{
    RsttoFileManagerIntegrationThunar *self = RSTTO_FILE_MANAGER_INTEGRATION_THUNAR (object);

    g_clear_object (&self->directory);

    if (self->channel != NULL)
        g_signal_handlers_disconnect_by_data (self->channel, self);
}

static void
rstto_file_manager_integration_thunar_get_property (GObject *object,
                                                    guint prop_id,
                                                    GValue *value,
                                                    GParamSpec *pspec)
{
    RsttoFileManagerIntegrationThunar *self = RSTTO_FILE_MANAGER_INTEGRATION_THUNAR (object);

    switch (prop_id)
    {
        case PROP_DIRECTORY:
            g_value_set_object (value, self->directory);
            break;

        case PROP_SORT_TYPE:
            g_value_set_int (value, self->sort_type);
            break;

        case PROP_SORT_ORDER:
            g_value_set_int (value, self->sort_order);
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
rstto_file_manager_integration_thunar_set_property (GObject *object,
                                                    guint prop_id,
                                                    const GValue *value,
                                                    GParamSpec *pspec)
{
    RsttoFileManagerIntegrationThunar *self = RSTTO_FILE_MANAGER_INTEGRATION_THUNAR (object);

    switch (prop_id)
    {
        case PROP_DIRECTORY:
            rstto_file_manager_integration_thunar_set_directory (self, g_value_get_object (value));
            break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
rstto_file_manager_integration_thunar_file_manager_integration_interface_init (RsttoFileManagerIntegrationInterface *iface)
{
}

static void
rstto_file_manager_integration_thunar_xfconf_property_changed (RsttoFileManagerIntegrationThunar *self,
                                                               const gchar *prop_name,
                                                               const GValue *value)
{
    const char *monitored_props[] = {
        "/misc-directory-specific-settings",
        "/last-sort-column",
        "/last-sort-order",
    };

    for (guint i = 0; i < G_N_ELEMENTS (monitored_props); ++i)
    {
        if (g_strcmp0 (prop_name, monitored_props[i]) == 0)
        {
            rstto_file_manager_integration_thunar_fetch_sort_settings (self);
            g_object_notify (G_OBJECT (self), "sort-type");
            g_object_notify (G_OBJECT (self), "sort-order");
            break;
        }
    }
}

static void
rstto_file_manager_integration_thunar_set_directory (RsttoFileManagerIntegrationThunar *self,
                                                     GFile *directory)
{
    g_clear_object (&self->directory);
    self->sort_type = -1;
    self->sort_order = -1;

    if (directory == NULL)
        return;

    self->directory = g_object_ref (directory);
    rstto_file_manager_integration_thunar_fetch_sort_settings (self);
}

static RsttoSortType
rstto_file_manager_integration_thunar_get_sort_type_from_string (const gchar *column)
{
    if (g_strcmp0 (column, "THUNAR_COLUMN_DATE_MODIFIED") == 0)
    {
        return SORT_TYPE_DATE;
    }
    else if (g_strcmp0 (column, "THUNAR_COLUMN_SIZE") == 0)
    {
        return SORT_TYPE_SIZE;
    }
    else if (g_strcmp0 (column, "THUNAR_COLUMN_TYPE") == 0)
    {
        return SORT_TYPE_TYPE;
    }
    else if (g_strcmp0 (column, "THUNAR_COLUMN_NAME") == 0)
    {
        return SORT_TYPE_NAME;
    }
    else
    {
        g_warning ("Unknown Thunar column");
        return SORT_TYPE_NAME;
    }
}

static RsttoSortOrder
rstto_file_manager_integration_thunar_get_sort_order_from_string (const gchar *order)
{
    if (g_strcmp0 (order, "GTK_SORT_DESCENDING") == 0)
    {
        return SORT_ORDER_DESC;
    }
    else if (g_strcmp0 (order, "GTK_SORT_ASCENDING") == 0)
    {
        return SORT_ORDER_ASC;
    }
    else
    {
        g_warning ("Unknown Thunar sort order");
        return SORT_ORDER_ASC;
    }
}

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings_per_directory (RsttoFileManagerIntegrationThunar *self)
{
    GError *error = NULL;
    GFileInfo *file_info = g_file_query_info (self->directory,
                                              "metadata::thunar-sort-column,metadata::thunar-sort-order",
                                              G_FILE_QUERY_INFO_NONE,
                                              NULL,
                                              &error);

    if (error != NULL)
    {
        g_clear_object (&file_info);
        g_clear_error (&error);
        return rstto_file_manager_integration_thunar_get_sort_settings_global (self);
    }

    const gchar *thunar_sort_column = g_file_info_get_attribute_string (file_info, "metadata::thunar-sort-column");
    const gchar *thunar_sort_order = g_file_info_get_attribute_string (file_info, "metadata::thunar-sort-order");

    if (thunar_sort_column == NULL || thunar_sort_order == NULL)
    {
        g_clear_object (&file_info);
        return rstto_file_manager_integration_thunar_get_sort_settings_global (self);
    }

    self->sort_type = rstto_file_manager_integration_thunar_get_sort_type_from_string (thunar_sort_column);
    self->sort_order = rstto_file_manager_integration_thunar_get_sort_order_from_string (thunar_sort_order);

    g_clear_object (&file_info);

    return TRUE;
}

static gboolean
rstto_file_manager_integration_thunar_get_sort_settings_global (RsttoFileManagerIntegrationThunar *self)
{
    gchar *thunar_last_sort_column = xfconf_channel_get_string (self->channel, "/last-sort-column", NULL);
    gchar *thunar_last_sort_order = xfconf_channel_get_string (self->channel, "/last-sort-order", NULL);

    if (thunar_last_sort_column == NULL || thunar_last_sort_order == NULL)
    {
        g_free (thunar_last_sort_column);
        g_free (thunar_last_sort_order);
        return FALSE;
    }

    self->sort_type = rstto_file_manager_integration_thunar_get_sort_type_from_string (thunar_last_sort_column);
    self->sort_order = rstto_file_manager_integration_thunar_get_sort_order_from_string (thunar_last_sort_order);

    g_free (thunar_last_sort_column);
    g_free (thunar_last_sort_order);

    return TRUE;
}

static gboolean
rstto_file_manager_integration_thunar_fetch_sort_settings (RsttoFileManagerIntegrationThunar *self)
{
    gboolean status = FALSE;

    if (!rstto_settings_get_xfconf_disabled ())
    {
        if (xfconf_channel_get_bool (self->channel, "/misc-directory-specific-settings", FALSE))
        {
            if (self->directory != NULL)
            {
                status = rstto_file_manager_integration_thunar_get_sort_settings_per_directory (self);
            }
            else
            {
                status = rstto_file_manager_integration_thunar_get_sort_settings_global (self);
            }
        }
        else
        {
            status = rstto_file_manager_integration_thunar_get_sort_settings_global (self);
        }
    }

    return status;
}

RsttoFileManagerIntegration *
rstto_file_manager_integration_thunar_new (void)
{
    return g_object_new (RSTTO_TYPE_FILE_MANAGER_INTEGRATION_THUNAR, NULL);
}

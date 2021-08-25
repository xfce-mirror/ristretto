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

#include "util.h"



static void
rstto_util_source_remove_all (gpointer  data,
                              GObject  *object)
{
    while (g_source_remove_by_user_data (object));
}



/* an often sufficient way to automate memory management of sources, without having
 * to store a source id and use an ad hoc handler */
gpointer
rstto_util_source_autoremove (gpointer object)
{
    g_return_val_if_fail (G_IS_OBJECT (object), object);

    if (! rstto_object_get_data (object, "source-autoremove"))
    {
        g_object_weak_ref (object, rstto_util_source_remove_all, NULL);
        rstto_object_set_data (object, "source-autoremove", GINT_TO_POINTER (TRUE));
    }

    return object;
}

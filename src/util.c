/*
 *  Copyright (c) Stephan Arts 2011 <stephan@xfce.org>
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

#include <config.h>
#include <glib.h>
#include <locale.h>
#include <exo/exo.h>

#include "util.h"

gboolean
rstto_launch_help (void)
{
    gchar *locale = NULL;
    gchar *offset;
    gchar *docpath = NULL;
    gchar *cur_dir = g_get_current_dir();
  
    /* Find localized documentation path on disk */
#ifdef ENABLE_NLS
#ifdef HAVE_LOCALE_H
    locale = g_strdup (setlocale (LC_MESSAGES, ""));
    if (locale != NULL)
    {
        offset = g_strrstr (locale, ".");
        if (offset != NULL)
        {
            *offset = '\0';
        }
    }
    else
    {
        locale = g_strdup ("C");
    }
#else
    locale = g_strdup ("C");
#endif

    docpath = g_strdup_printf (DOCDIR"/html/%s/index.html", locale);
    if (!g_file_test (docpath, G_FILE_TEST_EXISTS))
    {
        offset = g_strrstr (locale, "_");
        if (offset == NULL)
        {
            g_free (docpath);
            docpath = g_strdup (DOCDIR"/html/C/index.html");
        }
        else
        {
            *offset = '\0';
            g_free (docpath);
            docpath = g_strdup_printf (DOCDIR"/html/%s/index.html", locale);
            if (!g_file_test (docpath, G_FILE_TEST_EXISTS))
            {
                g_free (docpath);
                docpath = g_strdup (DOCDIR"/html/C/index.html");
            }
        }
    }

    g_free (locale);
#else
    docpath = g_strdup (DOCDIR"/html/C/index.html");
#endif

    /* Revert to online documentation if not available on disk */
    if (g_file_test (docpath, G_FILE_TEST_EXISTS))
    {
        gchar *tmp = docpath;
        docpath = g_strdup_printf ("file://%s", docpath);
        g_free (tmp);
    }
    else
    {
        g_free (docpath);
        docpath = g_strdup("http://docs.xfce.org/help.php?package=ristretto");
    }

    if (FALSE == exo_execute_preferred_application (
            "WebBrowser",
            docpath,
            cur_dir,
            NULL,
            NULL))
    {

    }

    g_free (docpath);
    g_free (cur_dir);

    return TRUE;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RSTTO_PROFILE_H
#define RSTTO_PROFILE_H

#include <glib.h>
#include <stdarg.h>

/* Optional wall-clock spans, including time blocked in libraries. No file names
 * or pixel data are recorded. Normal builds optimize these calls away. */
static inline gint64
rstto_profile_now (void)
{
#ifdef RSTTO_ENABLE_PROFILING
    static gsize enabled;
    if (g_once_init_enter (&enabled))
    {
        const gchar *value = g_getenv ("RISTRETTO_PROFILE");
        g_once_init_leave (&enabled, g_strcmp0 (value, "1") == 0 ? 2 : 1);
    }
    if (enabled == 2)
        return g_get_monotonic_time ();
#endif
    return 0;
}

static inline void rstto_profile_end (gint64 start, gconstpointer object, guint64 id,
                                     const gchar *event, const gchar *format, ...) G_GNUC_PRINTF (5, 6);

static inline void
rstto_profile_end (gint64 start, gconstpointer object, guint64 id,
                  const gchar *event, const gchar *format, ...)
{
#ifdef RSTTO_ENABLE_PROFILING
    if (start != 0)
    {
        gint64 end = g_get_monotonic_time ();
        gchar detail[512];
        va_list args;
        va_start (args, format);
        g_vsnprintf (detail, sizeof detail, format, args);
        va_end (args);
        /* One write per completed span; never log inside a pixel/chunk loop. */
        g_printerr ("RSTTO_PROFILE start_us=%" G_GINT64_FORMAT " duration_us=%" G_GINT64_FORMAT
                    " thread=%p object=%p id=%" G_GUINT64_FORMAT " event=%s %s\n",
                    start, end - start, (void *) g_thread_self (), (void *) object, id, event, detail);
    }
#endif
}

#endif

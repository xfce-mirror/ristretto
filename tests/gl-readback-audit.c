/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Optional LD_PRELOAD benchmark diagnostic, never linked into the app. GTK 3
 * uses libepoxy's dispatch pointer for its software readback fallback. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <epoxy/gl.h>
#include <epoxy/glx.h>
#include <gdk/gdk.h>
#include <stdio.h>

static guint64 draws, readbacks;
static PFNGLREADPIXELSPROC real_read;


/**
 * audit_read:
 *
 * Count a GL pixel readback before forwarding it to the real driver entry
 * point.
 */
static void GLAPIENTRY
audit_read (GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels)
{
    readbacks++;
    real_read (x, y, width, height, format, type, pixels);
}


/**
 * gdk_cairo_draw_from_gl:
 *
 * Interpose GDK texture presentation and temporarily instrument its libepoxy
 * readback path.
 */
void
gdk_cairo_draw_from_gl (cairo_t *cr, GdkWindow *window, int source, int type, int scale, int x, int y, int width, int height)
{
    static void (*draw) (cairo_t *, GdkWindow *, int, int, int, int, int, int, int);
    if (draw == NULL)
        draw = dlsym (RTLD_NEXT, "gdk_cairo_draw_from_gl");
    if (real_read == NULL)
        real_read = (PFNGLREADPIXELSPROC) glXGetProcAddressARB ((const GLubyte *) "glReadPixels");
    PFNGLREADPIXELSPROC saved = epoxy_glReadPixels;
    epoxy_glReadPixels = audit_read;
    draws++;
    draw (cr, window, source, type, scale, x, y, width, height);
    epoxy_glReadPixels = saved;
}


/**
 * audit_report:
 *
 * Print the final presentation and readback counts when the audit library is
 * unloaded.
 */
static void __attribute__ ((destructor))
audit_report (void)
{
    if (draws)
        fprintf (stderr, "RSTTO_GL_AUDIT draws=%" G_GUINT64_FORMAT " readbacks=%" G_GUINT64_FORMAT "\n", draws, readbacks);
}

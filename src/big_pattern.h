#ifndef __RISTRETTO_BIG_PATTERN_H__
#define __RISTRETTO_BIG_PATTERN_H__

#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>

G_BEGIN_DECLS

#define RSTTO_TYPE_BIG_PATTERN (rstto_big_pattern_get_type ())
G_DECLARE_DERIVABLE_TYPE (RsttoBigPattern, rstto_big_pattern, RSTTO, BIG_PATTERN, GObject)

typedef struct _RsttoBigPatternClass RsttoBigPatternClass;
typedef struct _RsttoBigPattern RsttoBigPattern;

/*
 * This class replaces cairo_pattern_t for large images. It slices a large image
 * into tiles, and displays those tiles separately because different parts of
 * the OS have different image size limits, limits may be small for processing
 * the image as one piece
 */
struct _RsttoBigPatternClass
{
    GObjectClass parent;
};

RsttoBigPattern *
rstto_big_pattern_new_from_pixbuf (GdkPixbuf *pixbuf);

void
rstto_big_pattern_set_device_scale (RsttoBigPattern *self,
                                    gdouble xscale,
                                    gdouble yscale);

GdkPixbuf *
rstto_big_pattern_get_pixbuf (RsttoBigPattern *self);

void
rstto_big_pattern_cairo_paint (RsttoBigPattern *self,
                               cairo_t *cr,
                               gdouble xscale,
                               gdouble yscale,
                               cairo_filter_t filter);

G_END_DECLS

#endif /* __RISTRETTO_BIG_PATTERN_H__ */

/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __RISTRETTO_IMAGE_CACHE_H__
#define __RISTRETTO_IMAGE_CACHE_H__

#include "image_render.h"
#include <cairo.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define RSTTO_PRELOAD_WORKERS_DEFAULT 2
#define RSTTO_PRELOAD_WORKERS_MAX 32

#define RSTTO_TYPE_IMAGE_CACHE (rstto_image_cache_get_type ())
G_DECLARE_FINAL_TYPE (RsttoImageCache, rstto_image_cache, RSTTO, IMAGE_CACHE, GObject)

typedef struct
{
    gboolean limit_quality;
    gint width, height;
    gboolean ephemeral;
} RsttoDecodeOptions;

/* Borrowed for the duration of the synchronous "ready" signal. The consumer
 * must replace its old surface before returning, and reference these objects
 * if needed. The cache keeps the displayed image pinned until the next ready
 * signal, clear(), or stop(). */
typedef struct
{
    cairo_surface_t *surface;
    GdkPixbufAnimation *animation;
    GError *error;
    gint width, height;
    gint decoded_width, decoded_height;
    gdouble quality_scale;
    gboolean has_alpha;
    cairo_surface_t *rendered;
    RsttoRenderKey render_key;
} RsttoImageData;

typedef struct
{
    gboolean enabled;
    guint max_images;
    guint64 max_bytes, max_image_bytes;
} RsttoCacheLimits;

typedef struct
{
    guint entries, jobs;
    guint64 resident_bytes, reserved_bytes, decodes, hits;
    guint64 renders, render_hits;
    guint reserved_images, speculative_images;
    guint active_tasks, peak_tasks;
} RsttoCacheStats;

RsttoImageCache *
rstto_image_cache_new (void);
void
rstto_image_cache_set_limits (RsttoImageCache *cache, const RsttoCacheLimits *limits);
void
rstto_image_cache_set_preload_workers (RsttoImageCache *cache, guint workers);
void
rstto_image_cache_set_excluded_mime_types (RsttoImageCache *cache, const gchar *const *types);
void
rstto_image_cache_request (RsttoImageCache *cache, GFile *file, const gchar *mime_type, const RsttoDecodeOptions *options);
/* Candidates are in priority order, borrowed during this call. MIME detection
 * uses the same generic fallback as foreground loading. */
void
rstto_image_cache_set_schedule (RsttoImageCache *cache, GFile *pending, GPtrArray *files);

void
rstto_image_cache_set_candidates (RsttoImageCache *cache, GPtrArray *files);
void
rstto_image_cache_invalidate (RsttoImageCache *cache, GFile *file);
void
rstto_image_cache_cancel_request (RsttoImageCache *cache);
void
rstto_image_cache_clear (RsttoImageCache *cache);
void
rstto_image_cache_stop (RsttoImageCache *cache);
gboolean
rstto_image_cache_is_busy (RsttoImageCache *cache);
void
rstto_image_cache_get_stats (RsttoImageCache *cache, RsttoCacheStats *stats);
/* Pure navigation policy, also used by the headless tests. */
GArray *
rstto_image_cache_neighbor_positions (guint length, guint current, gint direction, gboolean initial, guint count, guint capacity, gboolean wrap);

G_END_DECLS
#endif

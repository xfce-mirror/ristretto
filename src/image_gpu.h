/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RSTTO_IMAGE_GPU_H
#define RSTTO_IMAGE_GPU_H
#include "image_render.h"
#include "image_shader_cache.h"

#include <gdk/gdk.h>

/* Private optional backend. Policy and object ownership stay on the main
 * context. Each worker exclusively owns a shared GL context, for whole images. */
#define RSTTO_TYPE_GPU_RENDERER (rstto_gpu_renderer_get_type ())
#define RSTTO_GPU_RENDERER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), RSTTO_TYPE_GPU_RENDERER, RsttoGpuRenderer))
#define RSTTO_IS_GPU_RENDERER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RSTTO_TYPE_GPU_RENDERER))

typedef struct RsttoGpuRenderer RsttoGpuRenderer;
typedef struct
{
    GObjectClass parent_class;
} RsttoGpuRendererClass;

/**
 * rstto_gpu_renderer_get_type:
 *
 * Return the registered type of the private GPU renderer.
 */
GType
rstto_gpu_renderer_get_type (void);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (RsttoGpuRenderer, g_object_unref)
G_DEFINE_AUTOPTR_CLEANUP_FUNC (RsttoGpuRendererClass, g_type_class_unref)

typedef struct RsttoGpuImage RsttoGpuImage;
typedef struct RsttoGpuRequest RsttoGpuRequest;
/* Explicit application-owned GPU allocations, independent of the RAM budget. */
#define RSTTO_GPU_MAX_BYTES ((guint64) 1024 * 1024 * 1024)
#define RSTTO_GPU_MAX_SOURCE_BYTES ((guint64) 256 * 1024 * 1024)
#define RSTTO_GPU_MAX_SOURCES 2
#define RSTTO_GPU_HOST_SCRATCH ((guint64) 65536)
typedef enum
{
    RSTTO_GPU_NEAREST,
    RSTTO_GPU_DIRECT,
    RSTTO_GPU_UP,
    RSTTO_GPU_DOWN
} RsttoGpuFilter;
typedef struct
{
    guint64 retained, transient, peak_bytes, uploads, submissions, completed, discarded;
    guint64 shader_bytes, programs, warmups, warmup_uploads, warmup_submissions;
    guint active, peak_active, textures, retiring;
    guint64 binary_hits, binary_misses, binary_writes;
    guint64 source_bytes, reserved, source_hits, source_misses;
    guint sources, peak_sources;
} RsttoGpuStats;

gboolean
rstto_gpu_shader_cache_enabled (void);
guint
rstto_gpu_worker_count (void);
RsttoGpuRenderer *
rstto_gpu_renderer_new (GdkWindow *window, guint workers, GError **error);
gboolean
rstto_gpu_dimensions_fit (guint limit, gint width, gint height, const RsttoRenderKey *key);
RsttoGpuFilter
rstto_gpu_filter (const RsttoRenderKey *key);
void
rstto_gpu_render_key (RsttoGpuRenderer *self, gint width, gint height, RsttoRenderKey *key);
gboolean
rstto_gpu_renderer_available (RsttoGpuRenderer *self);
/* Cache scheduler owns this explicit host reservation and a task permit. */
#define RSTTO_GPU_WARMUP_BYTES ((guint64) 65536)
gboolean
rstto_gpu_renderer_needs_warmup (RsttoGpuRenderer *self);
void
rstto_gpu_warmup_async (RsttoGpuRenderer *self, gboolean persistent, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data);
gboolean
rstto_gpu_warmup_finish (RsttoGpuRenderer *self, GAsyncResult *result, GError **error);
void
rstto_gpu_renderer_get_stats (RsttoGpuRenderer *self, RsttoGpuStats *stats);
/* Immutable decoded-entry serials, never addresses. Main-context policy. A
 * lease pins a hit through queued work; misses reserve upload staging instead. */
void
rstto_gpu_source_select (RsttoGpuRenderer *self, guint64 serial);
void
rstto_gpu_source_invalidate (RsttoGpuRenderer *self, guint64 serial);
RsttoGpuRequest *
rstto_gpu_request_new (RsttoGpuRenderer *self, const RsttoRenderPixels *pixels, const RsttoRenderKey *key, guint64 serial, guint64 *host_bytes);
void
rstto_gpu_request_free (RsttoGpuRequest *request);
gboolean
rstto_gpu_request_available (RsttoGpuRequest *request);
/* Transfers the admitted request, including its pinned source and reservation. */
void
rstto_gpu_render_request_async (RsttoGpuRequest *request, GCancellable *cancel, gboolean (*useful) (gpointer), gpointer useful_data, const gint *demand, GAsyncReadyCallback callback, gpointer data);
/* A single permit is held until finish, including upload, GPU fences and
 * cleanup. The source stays pinned by the caller until the callback. */
void
rstto_gpu_render_async (RsttoGpuRenderer *self, const RsttoRenderPixels *source, const RsttoRenderKey *key, GCancellable *cancel, gboolean (*useful) (gpointer), gpointer useful_data, const gint *demand, GAsyncReadyCallback callback, gpointer data);
RsttoGpuImage *
rstto_gpu_render_finish (RsttoGpuRenderer *self, GAsyncResult *result, GError **error);
RsttoGpuImage *
rstto_gpu_image_ref (RsttoGpuImage *image);
void
rstto_gpu_image_unref (RsttoGpuImage *image);
gboolean
rstto_gpu_image_draw (RsttoGpuImage *image, cairo_t *cr, GdkWindow *window, gint scale);

#ifdef RSTTO_GPU_TEST
/* Deterministic controls are absent from production binaries. */
void
rstto_gpu_test_programs (guint *compiles, guint *loads, guint *accepted);
void
rstto_gpu_test_reject_binary (void);
void
rstto_gpu_test_budget (RsttoGpuRenderer *self, guint64 bytes, guint64 source_bytes);
void
rstto_gpu_test_limit (RsttoGpuRenderer *self, guint limit);
void
rstto_gpu_test_hold (RsttoGpuRenderer *self, gboolean hold);
void
rstto_gpu_test_fail (RsttoGpuRenderer *self, guint stage);
void
rstto_gpu_test_reference (RsttoGpuRenderer *self, gboolean reference);
cairo_surface_t *
rstto_gpu_test_download (RsttoGpuImage *image);
#endif
#if defined(RSTTO_VIEWER_TEST) && defined(HAVE_PLACEBO)
void
rstto_gpu_test_disable (RsttoGpuRenderer *self);
#endif
#endif

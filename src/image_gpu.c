/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "image_gpu.h"
#include "image_shader_cache.h"
#include "profile.h"
#include <math.h>
#include <float.h>
#include <string.h>

#ifdef HAVE_PLACEBO
#include <epoxy/gl.h>
#ifdef RSTTO_GPU_TEST
#include <epoxy/glx.h>
#endif
#include <libplacebo/opengl.h>
#include <libplacebo/dispatch.h>
#include <libplacebo/shaders/sampling.h>
#include <libplacebo/shaders/custom.h>
#endif

typedef struct _GpuWorker GpuWorker;
typedef struct _GpuSource GpuSource;
struct _RsttoGpuRenderer
{
    GObject parent;
    GMainContext *main_context;
    GMutex lock;
    RsttoGpuStats stats;
    guint count, limit;
    gint failed;
    guint64 generation;
    GpuWorker *workers;
    GpuSource *source; /* one selected source; leases may keep one retired source */
    guint64 selected_source, max_bytes, max_source_bytes;
#ifdef RSTTO_GPU_TEST
    gint hold, fail, reference;
#endif
};

G_DEFINE_TYPE (RsttoGpuRenderer, rstto_gpu_renderer, G_TYPE_OBJECT)

gboolean
rstto_gpu_shader_cache_enabled (void)
{
    return g_strcmp0 (g_getenv ("RISTRETTO_SHADER_CACHE"), "0") != 0;
}

gboolean
rstto_gpu_requested (void)
{
    return g_strcmp0 (g_getenv ("RISTRETTO_RENDERER"), "placebo") == 0;
}

guint
rstto_gpu_worker_count (void)
{
    const gchar *value = g_getenv ("RISTRETTO_GL_WORKERS");
    gchar *end = NULL;
    guint64 count = value != NULL ? g_ascii_strtoull (value, &end, 10) : 2;
    return value == NULL || (end != value && *end == '\0' && count >= 1 && count <= 5) ? count : 2;
}

gboolean
rstto_gpu_dimensions_fit (guint limit, gint width, gint height, const RsttoRenderKey *key)
{
    guint64 bytes;
    gdouble sx = key->x_scale * key->pixel_scale / key->source_scale;
    gdouble sy = key->y_scale * key->pixel_scale / key->source_scale;
    return limit > 0 && width > 0 && height > 0 && (guint) width <= limit && (guint) height <= limit
           && (guint64) width <= G_MAXSIZE / 4 / height
           && rstto_image_render_weight (key, &bytes) && bytes <= G_MAXSIZE
           && (guint) key->width <= limit && (guint) key->height <= limit
           && isfinite (key->x_scale) && isfinite (key->y_scale)
           && isfinite (key->source_scale) && key->source_scale > 0
           && isfinite (key->pixel_scale) && key->pixel_scale > 0
           && key->x_scale > 0 && key->y_scale > 0
           && isfinite (sx) && isfinite (sy) && sx >= FLT_MIN && sy >= FLT_MIN
           && sx <= FLT_MAX && sy <= FLT_MAX
           && key->width / sx <= FLT_MAX && key->height / sy <= FLT_MAX;
}

RsttoGpuFilter
rstto_gpu_filter (const RsttoRenderKey *key)
{
    if (!key->smoothing)
        return RSTTO_GPU_NEAREST;
    gdouble x = key->x_scale * key->pixel_scale / key->source_scale;
    gdouble y = key->y_scale * key->pixel_scale / key->source_scale;
    gdouble tx = ((gdouble) key->x - key->h_adjust) * key->pixel_scale;
    gdouble ty = ((gdouble) key->y - key->v_adjust) * key->pixel_scale;
    if (fabs (x - 1) < 1e-9 && fabs (y - 1) < 1e-9
        && fabs (tx - round (tx)) < 1e-9 && fabs (ty - round (ty)) < 1e-9)
        return RSTTO_GPU_DIRECT;
    return MIN (x, y) < 1 - 1e-9 ? RSTTO_GPU_DOWN : RSTTO_GPU_UP;
}

void
rstto_gpu_render_key (RsttoGpuRenderer *self, gint width, gint height, RsttoRenderKey *key)
{
    key->backend = key->filter = 0;
    key->context_generation = 0;
    if (self != NULL && !g_atomic_int_get (&self->failed) && rstto_gpu_dimensions_fit (self->limit, width, height, key))
    {
        key->backend = 2; /* Runtime EWA filter policy; incompatible with v1 derivatives. */
        key->filter = rstto_gpu_filter (key);
        key->context_generation = self->generation;
    }
}

void
rstto_gpu_renderer_get_stats (RsttoGpuRenderer *self, RsttoGpuStats *stats)
{
    *stats = (RsttoGpuStats) { 0 };
    if (self != NULL)
    {
        g_mutex_lock (&self->lock);
        *stats = self->stats;
        g_mutex_unlock (&self->lock);
    }
}

#ifdef HAVE_PLACEBO
static const gchar ewa_source[] =
        "if (kernel.w < 0.0) { color = textureLod(source, sample_uv, 0.0); } else {\n"
        "vec2 size = vec2(textureSize(source, 0));\n"
        "vec2 position = sample_uv * size - vec2(0.5);\n"
        "vec2 fraction = fract(position), base = floor(position) + vec2(0.5);\n"
        "int bound = int(kernel.z);\n"
        "vec4 sum = vec4(0.0), lo_sum = vec4(0.0), hi_sum = vec4(0.0);\n"
        "vec4 lo_weight = vec4(0.0), hi_weight = vec4(0.0);\n"
        "float total = 0.0;\n"
        "for (int y = 1 - bound; y <= bound; ++y) {\n"
        "  for (int x = 1 - bound; x <= bound; ++x) {\n"
        "    vec2 offset = vec2(x, y);\n"
        "    float distance = length(offset - fraction);\n"
        "    if (distance >= kernel.x) continue;\n"
        "    float weight = textureLod(weights, vec2((distance / kernel.x * 255.0 + 0.5) / 256.0, 0.5), 0.0).r;\n"
        "    vec4 value = textureLod(source, (base + offset) / size, 0.0);\n"
        "    sum += weight * value; total += weight;\n"
        "    if (distance <= kernel.y) {\n"
        "      vec4 low = vec4(1.0) - value;\n"
        "      vec4 lw = low + vec4(0.10), hw = value + vec4(0.10);\n"
        "      lw *= lw; hw *= hw; lw *= lw; hw *= hw; lw *= lw; hw *= hw;\n"
        "      lw *= lw; hw *= hw; lw *= lw; hw *= hw;\n"
        "      lw *= weight; hw *= weight;\n"
        "      lo_sum += lw * low; hi_sum += hw * value;\n"
        "      lo_weight += lw; hi_weight += hw;\n"
        "    }\n"
        "  }\n"
        "}\n"
        "color = sum / total;\n"
        "vec4 low = vec4(1.0) - lo_sum / lo_weight, high = hi_sum / hi_weight;\n"
        "vec4 limited = clamp(color, low, high);\n"
        "limited = mix(limited, (low + high) * 0.5, greaterThan(low, high));\n"
        "color = mix(color, limited, kernel.w);\n"
        "}\n";
static const gchar alpha_source[] = "color.a = clamp(color.a, 0.0, 1.0); color.rgb = clamp(color.rgb, vec3(0.0), vec3(color.a));";
static const gchar orientation_source[] = "color = textureLod(scaled, (source_uv - bounds.xy) / (bounds.zw - bounds.xy), 0.0);\n"
                "if (any(lessThan(source_uv, vec2(0.0))) || any(greaterThanEqual(source_uv, vec2(1.0)))) color = vec4(0.0);";

typedef enum { GPU_RENDER, GPU_DELETE, GPU_READ, GPU_STOP, GPU_WARMUP, GPU_SOURCE_DELETE } WorkType;
typedef struct
{
    WorkType type;
    GTask *task;
    RsttoRenderPixels pixels;
    RsttoRenderKey key;
    gboolean (*useful) (gpointer);
    gpointer useful_data;
    const gint *demand;
    GpuWorker *worker;
    RsttoGpuImage *image;
    GpuSource *delete_source;
    RsttoGpuRequest *request;
    GLuint source, queries[3];
    pl_tex input, intermediate, output;
    GLsync fence;
    gboolean ok, submitted;
    guint64 bytes;
    guint new_programs;
    guint warm_step;
    gboolean persistent;
    RsttoShaderCache *disk_cache;
    pl_cache binary_cache;
    guint32 warm_pixels[64];
    gint64 warm_start;
    gint64 start, upload_start, scale_start;
#ifdef RSTTO_GPU_TEST
    GMutex mutex;
    GCond cond;
    gboolean done;
    cairo_surface_t *download;
#endif
} GpuWork;

struct _GpuWorker
{
    RsttoGpuRenderer *owner;
    guint index;
    GdkGLContext *context;
    GAsyncQueue *queue;
    GThread *thread;
    gboolean busy, ready; /* main context only */
    pl_log log;
    pl_opengl gl;
    pl_dispatch dispatch;
    pl_fmt format;
    pl_shader_obj lut;
    pl_tex weights;
    GHashTable *programs;
    gchar *fingerprint;
};

struct _RsttoGpuImage
{
    gint refs;
    GpuWorker *worker;
    pl_tex texture;
    GLuint id;
    GLsync presented; /* written on the GTK thread, consumed after final unref */
    guint64 bytes;
    gint width, height;
};

/* The renderer owns one reference; immutable leases own any others. GL
 * destruction is queued to the owning context only after the last use drains.
 * Sources do not own the renderer (avoids a resident-source reference cycle). */
struct _GpuSource
{
    gint refs;
    GpuWorker *worker;
    guint64 serial, bytes;
    RsttoRenderPixels pixels; /* dimensions/format only; data is never dereferenced */
    GLuint texture;
    pl_tex input;
};

struct _RsttoGpuRequest
{
    RsttoGpuRenderer *owner;
    GpuSource *source;
    GpuWorker *worker;
    RsttoRenderPixels pixels;
    RsttoRenderKey key;
    guint64 serial, reservation;
};

static guint64
gpu_bytes (RsttoGpuRenderer *self)
{
    return self->stats.retained + self->stats.transient + self->stats.source_bytes
        + self->stats.reserved + self->stats.shader_bytes;
}

static gboolean
reserve_gpu (RsttoGpuRenderer *self, guint64 bytes)
{
    guint64 used = gpu_bytes (self);
    if (used > self->max_bytes || bytes > self->max_bytes - used)
        return FALSE;
    self->stats.reserved += bytes;
    self->stats.peak_bytes = MAX (self->stats.peak_bytes, used + bytes);
    return TRUE;
}

static void
source_unref (GpuSource *source)
{
    if (source == NULL || !g_atomic_int_dec_and_test (&source->refs)) return;
    GpuWork *work = g_new0 (GpuWork, 1);
    work->type = GPU_SOURCE_DELETE;
    work->delete_source = source;
    g_async_queue_push (source->worker->queue, work);
}

void
rstto_gpu_source_select (RsttoGpuRenderer *self, guint64 serial)
{
    if (self == NULL) return;
    g_mutex_lock (&self->lock);
    self->selected_source = serial;
    GpuSource *old = NULL;
    if (self->source != NULL && self->source->serial != serial)
        old = g_steal_pointer (&self->source);
    g_mutex_unlock (&self->lock);
    source_unref (old);
}

void
rstto_gpu_source_invalidate (RsttoGpuRenderer *self, guint64 serial)
{
    if (self == NULL) return;
    g_mutex_lock (&self->lock);
    if (self->selected_source == serial) self->selected_source = 0;
    GpuSource *old = self->source != NULL && self->source->serial == serial
        ? g_steal_pointer (&self->source) : NULL;
    g_mutex_unlock (&self->lock);
    source_unref (old);
}

RsttoGpuRequest *
rstto_gpu_request_new (RsttoGpuRenderer *self, const RsttoRenderPixels *pixels,
                       const RsttoRenderKey *key, guint64 serial, guint64 *host_bytes)
{
    if (self == NULL || g_atomic_int_get (&self->failed)
        || key->backend != 2 || key->context_generation != self->generation
        || !rstto_gpu_dimensions_fit (self->limit, pixels->width, pixels->height, key)
        || (pixels->format != CAIRO_FORMAT_ARGB32 && pixels->format != CAIRO_FORMAT_RGB24)
        || pixels->stride < (guint64) pixels->width * 4 || pixels->stride % 4 != 0
        || (guint64) pixels->stride > (G_MAXUINT64 - RSTTO_GPU_HOST_SCRATCH) / pixels->height)
        return NULL;
    guint64 source_bytes = (guint64) pixels->width * pixels->height * 4;
    guint64 output_bytes;
    if (!rstto_image_render_weight (key, &output_bytes) || output_bytes > (G_MAXUINT64 - source_bytes) / 2)
        return NULL;
    g_mutex_lock (&self->lock);
    GpuSource *source = self->source;
    gboolean hit = source != NULL && serial != 0 && source->serial == serial
        && source->pixels.width == pixels->width && source->pixels.height == pixels->height
        && source->pixels.format == pixels->format;
    guint64 reservation = output_bytes * 2 + (hit ? 0 : source_bytes);
    if (!reserve_gpu (self, reservation))
    {
        g_mutex_unlock (&self->lock);
        return NULL;
    }
    if (hit) g_atomic_int_inc (&source->refs);
    g_mutex_unlock (&self->lock);
    RsttoGpuRequest *request = g_new0 (RsttoGpuRequest, 1);
    request->owner = g_object_ref (self);
    request->source = hit ? source : NULL;
    request->pixels = *pixels;
    request->key = *key;
    request->serial = serial;
    request->reservation = reservation;
    *host_bytes = RSTTO_GPU_HOST_SCRATCH + (hit ? 0 : (guint64) pixels->stride * pixels->height);
    return request;
}

void
rstto_gpu_request_free (RsttoGpuRequest *request)
{
    if (request == NULL) return;
    g_mutex_lock (&request->owner->lock);
    request->owner->stats.reserved -= request->reservation;
    g_mutex_unlock (&request->owner->lock);
    source_unref (request->source);
    g_object_unref (request->owner);
    g_free (request);
}

gboolean
rstto_gpu_request_available (RsttoGpuRequest *request)
{
    return request->source != NULL ? !request->source->worker->busy
        : rstto_gpu_renderer_available (request->owner);
}

#ifdef RSTTO_GPU_TEST
static gint test_compiles, test_binary_loads, test_binary_accepts, test_reject_binary;
static void GLAPIENTRY test_compile (GLuint shader) {
    g_atomic_int_inc (&test_compiles); glCompileShader (shader);
}
static void GLAPIENTRY test_binary (GLuint program, GLenum format, const void *binary, GLsizei length) {
    g_atomic_int_inc (&test_binary_loads);
    if (g_atomic_int_compare_and_exchange (&test_reject_binary, 1, 0)) return;
    glProgramBinary (program, format, binary, length);
    GLint linked = 0; glGetProgramiv (program, GL_LINK_STATUS, &linked);
    if (linked) g_atomic_int_inc (&test_binary_accepts);
}
static pl_voidfunc_t test_get_proc (const char *name) {
    if (g_str_equal (name, "glCompileShader")) return (pl_voidfunc_t) test_compile;
    if (g_str_equal (name, "glProgramBinary")) return (pl_voidfunc_t) test_binary;
    return (pl_voidfunc_t) glXGetProcAddressARB ((const GLubyte *) name);
}
void rstto_gpu_test_programs (guint *compiles, guint *loads, guint *accepted) {
    *compiles = g_atomic_int_get (&test_compiles); *loads = g_atomic_int_get (&test_binary_loads); *accepted = g_atomic_int_get (&test_binary_accepts);
}
void rstto_gpu_test_reject_binary (void) { g_atomic_int_set (&test_reject_binary, 1); }
#endif

static gchar *
shader_fingerprint (void)
{
    GLint count = 0, profile = 0;
    glGetIntegerv (GL_NUM_PROGRAM_BINARY_FORMATS, &count);
    if (count <= 0 || count > 64) return NULL;
    GLint formats[64];
    glGetIntegerv (GL_PROGRAM_BINARY_FORMATS, formats);
    glGetIntegerv (GL_CONTEXT_PROFILE_MASK, &profile);
    GChecksum *sum = g_checksum_new (G_CHECKSUM_SHA256);
    const gchar *parts[] = { "ristretto-shader-envelope-1/filter-2/RGBA8/R32F/no-compute",
        ewa_source, alpha_source, orientation_source, pl_version (),
        (const gchar *) glGetString (GL_VENDOR), (const gchar *) glGetString (GL_RENDERER),
        (const gchar *) glGetString (GL_VERSION), (const gchar *) glGetString (GL_SHADING_LANGUAGE_VERSION) };
    for (guint i = 0; i < G_N_ELEMENTS (parts); i++)
    {
        const gchar *value = parts[i] != NULL ? parts[i] : "";
        g_checksum_update (sum, (const guchar *) value, strlen (value) + 1);
    }
    guint32 number = GUINT32_TO_LE (profile);
    g_checksum_update (sum, (guchar *) &number, sizeof number);
    for (gint i = 0; i < count; i++)
    {
        number = GUINT32_TO_LE (formats[i]);
        g_checksum_update (sum, (guchar *) &number, sizeof number);
    }
    gchar *result = g_strdup (g_checksum_get_string (sum));
    g_checksum_free (sum);
    return result;
}

static pl_cache_obj
binary_load (void *data, uint64_t key)
{
    pl_cache_obj object = { .key = key, .free = g_free };
    object.data = rstto_shader_cache_load (data, key, &object.size);
    return object;
}

static void
binary_save (void *data, pl_cache_obj object)
{
    rstto_shader_cache_save (data, object.key, object.data, object.size);
}

static void
gpu_log (void *priv, enum pl_log_level level, const char *message)
{
    /* Library failures are recoverable CPU fallbacks, not fatal GTK warnings. */
    if (level <= PL_LOG_ERR)
        g_printerr ("Ristretto OpenGL: %s\n", message);
}

static void
unref_on_main (RsttoGpuRenderer *self, GObject *object);

#ifdef RSTTO_VIEWER_TEST
void rstto_test_worker_checkpoint (gint phase, gboolean demand, GCancellable *cancel, gboolean entering);
#endif

static void
program_used (void *data, const struct pl_dispatch_info *info)
{
    GpuWorker *worker = data;
    if (!g_hash_table_contains (worker->programs, &info->signature))
    {
        guint64 *signature = g_new (guint64, 1);
        *signature = info->signature;
        g_hash_table_add (worker->programs, signature);
        g_mutex_lock (&worker->owner->lock);
        worker->owner->stats.programs++;
        rstto_profile_end (rstto_profile_now (), worker->owner, worker->index, "gpu-program",
            "signature=%" G_GUINT64_FORMAT " description=%s", (guint64) info->signature, info->shader->description);
        g_mutex_unlock (&worker->owner->lock);
    }
}

static gboolean
useful (GpuWork *work)
{
    return !g_cancellable_is_cancelled (g_task_get_cancellable (work->task))
           && (work->useful == NULL || work->useful (work->useful_data));
}

/* Same transform as the CPU path, including source device scale. Only these
 * tiny private Cairo objects are used off-thread; source pixels are immutable. */
static void
texture_coordinates (const RsttoRenderKey *key, const RsttoRenderPixels *pixels,
                     float uv[4][2], float *sample_w, float *sample_h)
{
    cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create (surface);
    cairo_scale (cr, key->pixel_scale, key->pixel_scale);
    rstto_image_render_transform (cr, key);
    cairo_scale (cr, 1 / key->source_scale, 1 / key->source_scale);
    cairo_matrix_t matrix;
    cairo_get_matrix (cr, &matrix);
    cairo_matrix_invert (&matrix);
    cairo_destroy (cr);
    cairo_surface_destroy (surface);
    gdouble min_x = G_MAXDOUBLE, min_y = G_MAXDOUBLE, max_x = -G_MAXDOUBLE, max_y = -G_MAXDOUBLE;
    for (guint i = 0; i < 4; i++)
    {
        gdouble x = i & 1 ? key->width : 0;
        /* GL row zero is at the bottom; source row zero is at the top. */
        gdouble y = i & 2 ? 0 : key->height;
        cairo_matrix_transform_point (&matrix, &x, &y);
        min_x = MIN (min_x, x); max_x = MAX (max_x, x);
        min_y = MIN (min_y, y); max_y = MAX (max_y, y);
        uv[i][0] = x / pixels->width;
        uv[i][1] = y / pixels->height;
    }
    *sample_w = max_x - min_x;
    *sample_h = max_y - min_y;
}

/* Fixed GLSL, with runtime loop bounds rather than one expanded program per
 * downscale radius. The 256-entry LUT and weighted antiring envelope follow
 * libplacebo 7.360's polar sampler (LGPL-2.1-or-later), including the first
 * zero crossing and power-32 envelope. Filter generation stays in libplacebo. */
static gboolean
sample_ewa (GpuWorker *worker, pl_shader sh, const struct pl_sample_src *src, guint policy)
{
    pl_gpu gpu = worker->gl->gpu;
    float rx = src->new_w / fabsf (src->rect.x1 - src->rect.x0);
    float ry = src->new_h / fabsf (src->rect.y1 - src->rect.y0);
    struct pl_filter_config config = policy == RSTTO_GPU_UP ? pl_filter_ewa_lanczossharp : pl_filter_ewa_lanczos;
    gboolean nearest = policy == RSTTO_GPU_NEAREST || policy == RSTTO_GPU_DIRECT;
    config.blur = (config.blur != 0 ? config.blur : 1.0f) * (nearest ? 1.0f : MAX (1.0f, 1.0f / MIN (rx, ry)));
    config.antiring = 0.8f;
    pl_filter filter = pl_filter_generate (worker->log, pl_filter_params (
        .config = config, .lut_entries = 256, .cutoff = 1e-3f));
    if (filter == NULL)
        return FALSE;
    float kernel[4] = { filter->radius, filter->radius_zero, ceilf (filter->radius), nearest ? -1.0f : 0.8f };
    gboolean valid = isfinite (kernel[2]) && kernel[2] < G_MAXINT / 2;
    if (valid && worker->weights == NULL)
    {
        pl_fmt format = pl_find_fmt (gpu, PL_FMT_FLOAT, 1, 32, 32, PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_LINEAR);
        RsttoGpuRenderer *self = worker->owner;
        g_mutex_lock (&self->lock);
        gboolean admitted = format != NULL && reserve_gpu (self, 256 * sizeof (float));
        g_mutex_unlock (&self->lock);
        if (admitted)
        {
            worker->weights = pl_tex_create (gpu, pl_tex_params (
                .w = 256, .h = 1, .format = format, .sampleable = TRUE, .host_writable = TRUE));
            g_mutex_lock (&self->lock);
            self->stats.reserved -= 256 * sizeof (float);
            if (worker->weights != NULL) self->stats.shader_bytes += 256 * sizeof (float);
            g_mutex_unlock (&self->lock);
        }
    }
    valid = valid && worker->weights != NULL && pl_tex_upload (gpu, pl_tex_transfer_params (
        .tex = worker->weights, .ptr = filter->weights));
    pl_filter_free (&filter); /* host LUT is covered by the active reservation */
    if (!valid)
        return FALSE;
    float uv[4][2] = {
        { src->rect.x0 / src->tex->params.w, src->rect.y0 / src->tex->params.h },
        { src->rect.x1 / src->tex->params.w, src->rect.y0 / src->tex->params.h },
        { src->rect.x0 / src->tex->params.w, src->rect.y1 / src->tex->params.h },
        { src->rect.x1 / src->tex->params.w, src->rect.y1 / src->tex->params.h }
    };
    struct pl_shader_desc descriptors[] = {
        { .desc = { .name = "source", .type = PL_DESC_SAMPLED_TEX },
          .binding = { .object = src->tex, .sample_mode = PL_TEX_SAMPLE_NEAREST, .address_mode = PL_TEX_ADDRESS_CLAMP } },
        { .desc = { .name = "weights", .type = PL_DESC_SAMPLED_TEX },
          .binding = { .object = worker->weights, .sample_mode = PL_TEX_SAMPLE_LINEAR, .address_mode = PL_TEX_ADDRESS_CLAMP } }
    };
    struct pl_shader_var var = { .var = pl_var_vec4 ("kernel"), .data = kernel, .dynamic = TRUE };
    struct pl_shader_va attr = { .attr = { .name = "sample_uv", .fmt = pl_find_vertex_fmt (gpu, PL_FMT_FLOAT, 2) },
                                .data = { uv[0], uv[1], uv[2], uv[3] } };
    return pl_shader_custom (sh, &(struct pl_custom_shader) {
        .description = "Ristretto runtime EWA", .input = PL_SHADER_SIG_NONE, .output = PL_SHADER_SIG_COLOR,
        .descriptors = descriptors, .num_descriptors = G_N_ELEMENTS (descriptors),
        .variables = &var, .num_variables = 1, .vertex_attribs = &attr, .num_vertex_attribs = 1,
        .body = ewa_source
    });
}

/* Scale once into an unrotated viewport, then orient/crop with nearest
 * texture sampling. Both textures are explicit and separately accounted. This
 * uses the ordinary texture-input API, without rewriting generated GLSL. */
static gboolean
dispatch_image (GpuWork *work)
{
    GpuWorker *worker = work->worker;
    pl_gpu gpu = worker->gl->gpu;
    float uv[4][2], sampled_w, sampled_h;
    texture_coordinates (&work->key, &work->pixels, uv, &sampled_w, &sampled_h);
    float bounds[4] = { INFINITY, INFINITY, -INFINITY, -INFINITY };
    for (guint i = 0; i < 4; i++)
    {
        bounds[0] = MIN (bounds[0], uv[i][0]); bounds[1] = MIN (bounds[1], uv[i][1]);
        bounds[2] = MAX (bounds[2], uv[i][0]); bounds[3] = MAX (bounds[3], uv[i][1]);
    }
    gboolean rotated = work->key.orientation >= 5 && work->key.orientation <= 8;
    struct pl_sample_src src = {
        .tex = work->input,
        .rect = { bounds[0] * work->pixels.width, bounds[1] * work->pixels.height,
                  bounds[2] * work->pixels.width, bounds[3] * work->pixels.height },
        .new_w = rotated ? work->key.height : work->key.width,
        .new_h = rotated ? work->key.width : work->key.height, .components = 4
    };
    work->intermediate = pl_tex_create (gpu, pl_tex_params (
        .w = src.new_w, .h = src.new_h, .format = worker->format, .sampleable = TRUE, .renderable = TRUE, .blit_dst = TRUE));
    if (work->intermediate == NULL) return FALSE;
    /* Restrict expensive filtering to visible image pixels. Keep the same
     * full-viewport affine map, so clipping cannot change the sample phase. */
    if (!isfinite (bounds[0]) || !isfinite (bounds[1]) || !isfinite (bounds[2]) || !isfinite (bounds[3])
        || bounds[2] <= bounds[0] || bounds[3] <= bounds[1])
        return FALSE;
    pl_rect2d rect = {
        (gint) CLAMP (floor (-bounds[0] / (bounds[2] - bounds[0]) * src.new_w), 0, src.new_w),
        (gint) CLAMP (floor (-bounds[1] / (bounds[3] - bounds[1]) * src.new_h), 0, src.new_h),
        (gint) CLAMP (ceil ((1 - bounds[0]) / (bounds[2] - bounds[0]) * src.new_w), 0, src.new_w),
        (gint) CLAMP (ceil ((1 - bounds[1]) / (bounds[3] - bounds[1]) * src.new_h), 0, src.new_h)
    };
    const float transparent[4] = { 0 };
    pl_tex_clear (gpu, work->intermediate, transparent);
    if (rect.x1 <= rect.x0 || rect.y1 <= rect.y0)
    {
        pl_tex_clear (gpu, work->output, transparent);
        return TRUE;
    }
    float span_x = src.rect.x1 - src.rect.x0, span_y = src.rect.y1 - src.rect.y0;
    src.rect.x1 = src.rect.x0 + span_x * rect.x1 / src.new_w;
    src.rect.y1 = src.rect.y0 + span_y * rect.y1 / src.new_h;
    src.rect.x0 += span_x * rect.x0 / src.new_w;
    src.rect.y0 += span_y * rect.y0 / src.new_h;
    src.new_w = rect.x1 - rect.x0;
    src.new_h = rect.y1 - rect.y0;
    pl_dispatch_reset_frame (worker->dispatch);
    pl_shader sample = pl_dispatch_begin (worker->dispatch);
    gboolean ok;
#ifdef RSTTO_GPU_TEST
    if (g_atomic_int_get (&worker->owner->reference))
    {
        if (work->key.filter == RSTTO_GPU_NEAREST || work->key.filter == RSTTO_GPU_DIRECT)
            ok = pl_shader_sample_nearest (sample, &src);
        else
        {
            struct pl_sample_filter_params params = {
                .filter = work->key.filter == RSTTO_GPU_UP ? pl_filter_ewa_lanczossharp : pl_filter_ewa_lanczos,
                .antiring = 0.8f, .no_compute = TRUE, .lut = &worker->lut
            };
            ok = pl_shader_sample_polar (sample, &src, &params);
        }
    }
    else
#endif
        ok = sample_ewa (worker, sample, &src, work->key.filter);
    if (ok)
        ok = pl_shader_custom (sample, &(struct pl_custom_shader) {
            .input = PL_SHADER_SIG_COLOR, .output = PL_SHADER_SIG_COLOR,
            .body = alpha_source
        });
    if (ok)
        ok = pl_dispatch_finish (worker->dispatch, pl_dispatch_params (.shader = &sample, .target = work->intermediate, .rect = rect));
    if (sample != NULL) pl_dispatch_abort (worker->dispatch, &sample);
    if (!ok) return FALSE;
    struct pl_shader_desc descriptor = {
        .desc = { .name = "scaled", .type = PL_DESC_SAMPLED_TEX },
        .binding = { .object = work->intermediate, .sample_mode = PL_TEX_SAMPLE_NEAREST,
                     .address_mode = PL_TEX_ADDRESS_CLAMP }
    };
    struct pl_shader_var var = { .var = pl_var_vec4 ("bounds"), .data = bounds, .dynamic = TRUE };
    struct pl_shader_va attr = { .attr = { .name = "source_uv", .fmt = pl_find_vertex_fmt (gpu, PL_FMT_FLOAT, 2) },
                                .data = { uv[0], uv[1], uv[2], uv[3] } };
    pl_shader sh = pl_dispatch_begin (worker->dispatch);
    ok = pl_shader_custom (sh, &(struct pl_custom_shader) {
        .body = orientation_source,
        .description = "Ristretto orientation and crop", .input = PL_SHADER_SIG_NONE, .output = PL_SHADER_SIG_COLOR,
        .descriptors = &descriptor, .num_descriptors = 1, .variables = &var, .num_variables = 1,
        .vertex_attribs = &attr, .num_vertex_attribs = 1
    });
    if (ok)
        ok = pl_dispatch_finish (worker->dispatch, pl_dispatch_params (.shader = &sh, .target = work->output));
    if (sh != NULL) pl_dispatch_abort (worker->dispatch, &sh);
    return ok;
}

static void
submit (GpuWork *work)
{
    GpuWorker *worker = work->worker;
    RsttoGpuRenderer *self = worker->owner;
    pl_gpu gpu = worker->gl->gpu;
#ifdef RSTTO_VIEWER_TEST
    rstto_test_worker_checkpoint (9, work->demand != NULL && g_atomic_int_get (work->demand),
                                 g_task_get_cancellable (work->task), TRUE);
#endif
    if (g_atomic_int_get (&self->failed) || !useful (work))
        return;
#ifdef RSTTO_GPU_TEST
    if (g_atomic_int_get (&self->fail) == 1)
        return;
#endif
    if (!rstto_gpu_dimensions_fit (MIN (self->limit, gpu->limits.max_tex_2d_dim),
                                    work->pixels.width, work->pixels.height, &work->key))
        return;
    if (work->type == GPU_WARMUP && work->warm_step == 0 && work->persistent && worker->fingerprint != NULL)
    {
        work->disk_cache = rstto_shader_cache_new (worker->fingerprint, NULL, g_task_get_cancellable (work->task));
        work->binary_cache = pl_cache_create (pl_cache_params (
            .max_object_size = RSTTO_SHADER_OBJECT_BYTES, .max_total_size = RSTTO_SHADER_PAYLOAD_BYTES,
            .get = binary_load, .set = binary_save, .priv = work->disk_cache));
        pl_gpu_set_cache (gpu, work->binary_cache);
    }
    work->start = work->upload_start = rstto_profile_now ();
    g_mutex_lock (&self->lock);
    work->bytes = work->request != NULL ? work->request->reservation
        : (guint64) work->pixels.width * work->pixels.height * 4 + (guint64) work->key.width * work->key.height * 8;
    if (work->request == NULL && !reserve_gpu (self, work->bytes))
    {
        work->bytes = 0;
        g_mutex_unlock (&self->lock);
        return;
    }
    self->stats.reserved -= work->bytes;
    self->stats.transient += work->bytes;
    if (work->request != NULL) work->request->reservation = 0;
    g_mutex_unlock (&self->lock);
    if (work->start != 0)
    {
        glGenQueries (3, work->queries);
        glQueryCounter (work->queries[0], GL_TIMESTAMP);
    }
    gboolean reused = work->request != NULL && work->request->source != NULL;
    gboolean uploaded = reused;
    if (reused)
        work->input = work->request->source->input;
    else
    {
        for (guint n = 0; n < 16 && glGetError () != GL_NO_ERROR; n++) { }
        glGenTextures (1, &work->source);
        glBindTexture (GL_TEXTURE_2D, work->source);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (work->pixels.format == CAIRO_FORMAT_RGB24)
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_ONE);
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei (GL_UNPACK_ROW_LENGTH, work->pixels.stride / 4);
        /* Native Cairo ARGB32/RGB24 words, without an additional pixel copy.
         * Reserve source-sized host staging conservatively in the cache manager. */
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8, work->pixels.width, work->pixels.height,
                      0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, work->pixels.data);
        glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
        uploaded = glGetError () == GL_NO_ERROR;
#ifdef RSTTO_GPU_TEST
        uploaded &= g_atomic_int_get (&self->fail) != 2;
#endif
        g_mutex_lock (&self->lock);
        if (work->type == GPU_WARMUP) self->stats.warmup_uploads++;
        else self->stats.uploads++;
        g_mutex_unlock (&self->lock);
        rstto_profile_end (work->upload_start, work, worker->index,
                           work->type == GPU_WARMUP ? "gpu-warmup-upload" : "gpu-upload", "width=%d height=%d success=%d",
                           work->pixels.width, work->pixels.height, uploaded);
    }
    if (work->type != GPU_WARMUP)
    {
        g_mutex_lock (&self->lock);
        self->stats.source_hits += reused;
        self->stats.source_misses += !reused;
        g_mutex_unlock (&self->lock);
        rstto_profile_end (rstto_profile_now (), work, worker->index, "gpu-source",
            "serial=%" G_GUINT64_FORMAT " reused=%d", work->request->serial, reused);
    }
    if (work->queries[0])
        glQueryCounter (work->queries[1], GL_TIMESTAMP);
    if (uploaded && useful (work))
    {
        if (!reused)
            work->input = pl_opengl_wrap (gpu, pl_opengl_wrap_params (
                .texture = work->source, .width = work->pixels.width, .height = work->pixels.height, .iformat = GL_RGBA8));
        work->output = pl_tex_create (gpu, pl_tex_params (
            .w = work->key.width, .h = work->key.height, .format = worker->format,
            .sampleable = TRUE, .renderable = TRUE, .blit_dst = TRUE, .host_readable = TRUE));
        if (work->input != NULL && work->output != NULL)
        {
            work->scale_start = rstto_profile_now ();
            guint programs = g_hash_table_size (worker->programs);
            work->ok = dispatch_image (work);
            work->new_programs = g_hash_table_size (worker->programs) - programs;
#ifdef RSTTO_GPU_TEST
            work->ok &= g_atomic_int_get (&self->fail) != 3;
#endif
            rstto_profile_end (work->scale_start, work, worker->index,
                work->type == GPU_WARMUP ? "gpu-warmup-submit" : "gpu-submit",
                "filter=%u success=%d new_programs=%u", work->key.filter, work->ok, work->new_programs);
            g_mutex_lock (&self->lock);
            if (work->type == GPU_WARMUP) self->stats.warmup_submissions++;
            else self->stats.submissions++;
            g_mutex_unlock (&self->lock);
        }
    }
    if (work->queries[0])
        glQueryCounter (work->queries[2], GL_TIMESTAMP);
    /* Even failed/cancelled submissions drain before releasing accounting. */
    work->fence = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush ();
    if (work->fence == NULL)
    {
        /* Error path only: no partial image or optimistic release after a
         * failed fence allocation. This is never called on the GTK thread. */
        glFinish ();
        work->ok = FALSE;
    }
}

static gboolean
fence_ready (GpuWorker *worker, GLsync fence)
{
    if (fence == NULL)
        return TRUE;
    GLenum status = glClientWaitSync (fence, 0, 0);
    if (status == GL_WAIT_FAILED)
    {
        g_atomic_int_set (&worker->owner->failed, TRUE);
        glFinish (); /* exceptional cleanup, never on the UI thread */
        return TRUE;
    }
    return status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED;
}

static gboolean
complete (GpuWork *work)
{
    GpuWorker *worker = work->worker;
    RsttoGpuRenderer *self = worker->owner;
    RsttoGpuImage *image = NULL;
    gboolean keep = !g_atomic_int_get (&self->failed) && work->ok && useful (work);
    if (keep && work->type != GPU_WARMUP)
    {
        image = g_new0 (RsttoGpuImage, 1);
        image->refs = 1;
        image->worker = worker;
        image->texture = g_steal_pointer (&work->output);
        image->id = pl_opengl_unwrap (worker->gl->gpu, image->texture, NULL, NULL, NULL);
        image->width = work->key.width; image->height = work->key.height;
        image->bytes = (guint64) image->width * image->height * 4;
        g_object_ref (self); /* released on the main context after texture deletion */
    }
    double upload_ms = 0, scale_ms = 0;
    if (work->queries[0])
    {
        GLuint64 times[3];
        for (guint n = 0; n < 3; n++)
            glGetQueryObjectui64v (work->queries[n], GL_QUERY_RESULT, &times[n]);
        upload_ms = (times[1] - times[0]) / 1e6;
        scale_ms = (times[2] - times[1]) / 1e6;
        glDeleteQueries (3, work->queries);
    }
    if (work->fence)
        glDeleteSync (work->fence);
    gboolean reused = work->request != NULL && work->request->source != NULL;
    /* Only the latest selected immutable source may become resident. A second
     * slot allows its predecessor's pinned leases/deletions to drain. */
    g_mutex_lock (&self->lock);
    if (keep && work->request != NULL && !reused && work->input != NULL
        && work->request->serial != 0 && self->selected_source == work->request->serial
        && self->source == NULL && self->stats.sources < RSTTO_GPU_MAX_SOURCES
        && (guint64) work->pixels.width * work->pixels.height * 4 <= self->max_source_bytes)
    {
        GpuSource *source = g_new0 (GpuSource, 1);
        source->refs = 1; source->worker = worker; source->serial = work->request->serial;
        source->pixels = work->pixels; source->pixels.data = NULL;
        source->texture = work->source; work->source = 0;
        source->input = g_steal_pointer (&work->input);
        source->bytes = (guint64) work->pixels.width * work->pixels.height * 4;
        self->source = source;
        self->stats.sources++;
        self->stats.peak_sources = MAX (self->stats.peak_sources, self->stats.sources);
        self->stats.source_bytes += source->bytes;
        self->stats.transient -= source->bytes;
        work->bytes -= source->bytes;
    }
    g_mutex_unlock (&self->lock);
    if (!reused) pl_tex_destroy (worker->gl->gpu, &work->input);
    else work->input = NULL; /* borrowed from the pinned lease */
    pl_tex_destroy (worker->gl->gpu, &work->intermediate);
    pl_tex_destroy (worker->gl->gpu, &work->output);
    if (work->source)
        glDeleteTextures (1, &work->source);
    g_mutex_lock (&self->lock);
    self->stats.transient -= work->bytes;
    self->stats.retained += image != NULL ? image->bytes : 0;
    self->stats.textures += image != NULL;
    self->stats.completed += image != NULL;
    self->stats.discarded += image == NULL && work->type != GPU_WARMUP;
    guint active = self->stats.active;
    guint64 accounted = gpu_bytes (self), source_bytes = self->stats.source_bytes, reserved = self->stats.reserved;
    guint sources = self->stats.sources;
    g_mutex_unlock (&self->lock);
    rstto_profile_end (work->start, work, worker->index,
        work->type == GPU_WARMUP ? "gpu-warmup-stage" : "gpu-render",
        "upload_gpu_ms=%.3f scale_gpu_ms=%.3f width=%d height=%d filter=%u active=%u discarded=%d"
        " gpu_bytes=%" G_GUINT64_FORMAT " source_bytes=%" G_GUINT64_FORMAT " sources=%u reserved=%" G_GUINT64_FORMAT,
        upload_ms, scale_ms, work->key.width, work->key.height, work->key.filter, active, image == NULL,
        accounted, source_bytes, sources, reserved);
#ifdef RSTTO_VIEWER_TEST
    rstto_test_worker_checkpoint (9, work->demand != NULL && g_atomic_int_get (work->demand),
                                 g_task_get_cancellable (work->task), FALSE);
#endif
    if (work->type == GPU_WARMUP)
    {
        if (keep && ++work->warm_step < 3)
        {
            work->key.filter = work->warm_step == 1 ? RSTTO_GPU_UP : RSTTO_GPU_DOWN;
            work->key.x_scale = work->key.y_scale = work->warm_step == 1 ? 1.5 : 0.5;
            work->key.render_width = work->key.render_height = work->warm_step == 1 ? 12 : 4;
            work->submitted = work->ok = FALSE;
            work->bytes = 0;
            work->fence = NULL;
            work->source = 0;
            memset (work->queries, 0, sizeof work->queries);
            return TRUE;
        }
        if (!keep && !g_cancellable_is_cancelled (g_task_get_cancellable (work->task)))
            g_atomic_int_set (&self->failed, TRUE);
        pl_gpu_set_cache (worker->gl->gpu, NULL);
        pl_cache_destroy (&work->binary_cache);
        if (work->disk_cache != NULL)
        {
            guint64 hits, misses, writes;
            rstto_shader_cache_stats (work->disk_cache, &hits, &misses, &writes);
            g_mutex_lock (&self->lock);
            self->stats.binary_hits += hits; self->stats.binary_misses += misses; self->stats.binary_writes += writes;
            g_mutex_unlock (&self->lock);
            rstto_shader_cache_free (work->disk_cache);
        }
        rstto_profile_end (work->warm_start, work, worker->index, "gpu-warmup", "success=%d", keep);
        g_task_return_boolean (work->task, keep);
    }
    else
    if (image != NULL)
        g_task_return_pointer (work->task, image, (GDestroyNotify) rstto_gpu_image_unref);
    else
        g_task_return_new_error (work->task, G_IO_ERROR, G_IO_ERROR_FAILED, "OpenGL scaling failed or was cancelled");
    unref_on_main (self, G_OBJECT (work->task));
    g_free (work);
    return FALSE;
}

static gboolean
release_owner (gpointer data)
{
    g_object_unref (data);
    return G_SOURCE_REMOVE;
}

static void
unref_on_main (RsttoGpuRenderer *self, GObject *object)
{
    GSource *source = g_idle_source_new ();
    g_source_set_callback (source, release_owner, object, NULL);
    g_source_attach (source, self->main_context);
    g_source_unref (source);
}

static void
delete_image (RsttoGpuImage *image)
{
    GpuWorker *worker = image->worker;
    RsttoGpuRenderer *self = worker->owner;
    if (image->presented)
        glDeleteSync (image->presented);
    pl_tex_destroy (worker->gl->gpu, &image->texture);
    g_mutex_lock (&self->lock);
    self->stats.retained -= image->bytes;
    self->stats.textures--;
    self->stats.retiring--;
    g_mutex_unlock (&self->lock);
    g_free (image);
    /* Never finalize a pool on one of its own worker threads. */
    unref_on_main (self, G_OBJECT (self));
}

static gpointer
gpu_thread (gpointer data)
{
    GpuWorker *worker = data;
    gdk_gl_context_make_current (worker->context);
    GpuWork *active = NULL;
    GQueue retiring = G_QUEUE_INIT;
    for (;;)
    {
        gboolean held = FALSE;
#ifdef RSTTO_GPU_TEST
        held = g_atomic_int_get (&worker->owner->hold);
#endif
        /* Do not keep allocating replacements while the consumer context is
         * stalled. One queued image retains its permit/reservation; retired
         * textures must drain before this worker uploads another source. */
        if (active != NULL && !active->submitted
            && (g_queue_is_empty (&retiring) || !useful (active)))
        {
            active->submitted = TRUE;
            submit (active);
        }
        if (active != NULL && active->submitted && fence_ready (worker, active->fence) && !held)
        {
            if (!complete (active))
                active = NULL;
        }
        for (GList *lp = retiring.head, *next; lp != NULL; lp = next)
        {
            RsttoGpuImage *image = lp->data;
            next = lp->next;
            if (!held && fence_ready (worker, image->presented))
            {
                g_queue_delete_link (&retiring, lp);
                delete_image (image);
            }
        }
        GpuWork *work = active != NULL || !g_queue_is_empty (&retiring)
            ? g_async_queue_timeout_pop (worker->queue, 1000) : g_async_queue_pop (worker->queue);
        if (work == NULL)
            continue;
        if (work->type == GPU_STOP)
        {
            g_assert (active == NULL && g_queue_is_empty (&retiring));
            g_free (work);
            break;
        }
        if (work->type == GPU_SOURCE_DELETE)
        {
            GpuSource *source = work->delete_source;
            pl_tex_destroy (worker->gl->gpu, &source->input);
            glDeleteTextures (1, &source->texture);
            g_mutex_lock (&worker->owner->lock);
            worker->owner->stats.source_bytes -= source->bytes;
            worker->owner->stats.sources--;
            g_mutex_unlock (&worker->owner->lock);
            g_free (source); g_free (work);
        }
        else if (work->type == GPU_DELETE)
        {
            g_queue_push_tail (&retiring, work->image);
            g_free (work);
        }
#ifdef RSTTO_GPU_TEST
        else if (work->type == GPU_READ)
        {
            RsttoGpuImage *image = work->image;
            cairo_surface_t *surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, image->width, image->height);
            guint8 *pixels = cairo_image_surface_get_data (surface);
            gint stride = cairo_image_surface_get_stride (surface);
            glBindTexture (GL_TEXTURE_2D, image->id);
            glGetTexImage (GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, pixels);
            guint8 *row = g_malloc (stride);
            for (gint y = 0; y < image->height / 2; y++)
            {
                guint8 *a = pixels + y * stride, *b = pixels + (image->height - y - 1) * stride;
                memcpy (row, a, stride); memcpy (a, b, stride); memcpy (b, row, stride);
            }
            g_free (row);
            cairo_surface_mark_dirty (surface);
            g_mutex_lock (&work->mutex);
            work->download = surface;
            work->done = TRUE;
            g_cond_signal (&work->cond);
            g_mutex_unlock (&work->mutex);
        }
#endif
        else
        {
            g_assert (active == NULL);
            active = work;
        }
    }
    pl_shader_obj_destroy (&worker->lut);
    if (worker->weights != NULL)
    {
        g_mutex_lock (&worker->owner->lock);
        worker->owner->stats.shader_bytes -= 256 * sizeof (float);
        g_mutex_unlock (&worker->owner->lock);
    }
    pl_tex_destroy (worker->gl->gpu, &worker->weights);
    pl_dispatch_destroy (&worker->dispatch);
    g_hash_table_unref (worker->programs);
    g_free (worker->fingerprint);
    pl_opengl_destroy (&worker->gl);
    pl_log_destroy (&worker->log);
    gdk_gl_context_clear_current ();
    return NULL;
}

RsttoGpuRenderer *
rstto_gpu_renderer_new (GdkWindow *window, guint workers, GError **error)
{
    RsttoGpuRenderer *self = g_object_new (RSTTO_TYPE_GPU_RENDERER, NULL);
    GdkGLContext *previous = gdk_gl_context_get_current ();
    if (previous != NULL)
        g_object_ref (previous);
    self->workers = g_new0 (GpuWorker, CLAMP (workers, 1, 5));
    for (guint i = 0; i < CLAMP (workers, 1, 5); i++)
    {
        GpuWorker *worker = &self->workers[i];
        worker->owner = self;
        worker->index = i;
        worker->context = gdk_window_create_gl_context (window, error);
        if (worker->context == NULL)
            break;
        gdk_gl_context_set_required_version (worker->context, 3, 3);
        gdk_gl_context_set_use_es (worker->context, FALSE);
        if (!gdk_gl_context_realize (worker->context, error))
        {
            g_clear_object (&worker->context);
            break;
        }
        gdk_gl_context_make_current (worker->context);
        worker->log = pl_log_create (PL_API_VER, pl_log_params (.log_cb = gpu_log, .log_level = PL_LOG_ERR));
        worker->gl = pl_opengl_create (worker->log, pl_opengl_params (.no_compute = TRUE,
#ifdef RSTTO_GPU_TEST
            .get_proc_addr = test_get_proc,
#endif
        ));
        if (worker->gl != NULL)
            worker->format = pl_find_fmt (worker->gl->gpu, PL_FMT_UNORM, 4, 8, 8,
                                          PL_FMT_CAP_SAMPLEABLE | PL_FMT_CAP_RENDERABLE);
        if (worker->gl == NULL || worker->format == NULL || worker->format->texel_size != 4)
        {
            pl_opengl_destroy (&worker->gl);
            pl_log_destroy (&worker->log);
            gdk_gl_context_clear_current ();
            g_clear_object (&worker->context);
            break;
        }
        self->limit = i == 0 ? worker->gl->gpu->limits.max_tex_2d_dim
                            : MIN (self->limit, worker->gl->gpu->limits.max_tex_2d_dim);
        worker->dispatch = pl_dispatch_create (worker->log, worker->gl->gpu);
        if (worker->dispatch == NULL || pl_find_vertex_fmt (worker->gl->gpu, PL_FMT_FLOAT, 2) == NULL)
        {
            pl_dispatch_destroy (&worker->dispatch);
            pl_opengl_destroy (&worker->gl);
            pl_log_destroy (&worker->log);
            gdk_gl_context_clear_current ();
            g_clear_object (&worker->context);
            break;
        }
        worker->fingerprint = shader_fingerprint ();
        worker->programs = g_hash_table_new_full (g_int64_hash, g_int64_equal, g_free, NULL);
        pl_dispatch_callback (worker->dispatch, worker, program_used);
        worker->queue = g_async_queue_new ();
        gdk_gl_context_clear_current ();
        worker->thread = g_thread_new ("ristretto-gl", gpu_thread, worker);
        self->count++;
    }
    if (previous != NULL)
    {
        gdk_gl_context_make_current (previous);
        g_object_unref (previous);
    }
    if (self->count == 0)
    {
        if (error != NULL && *error == NULL)
            g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Hardware OpenGL scaling is unavailable");
        g_object_unref (self);
        return NULL;
    }
    if (error != NULL)
        g_clear_error (error); /* a smaller successfully initialized pool is usable */
    rstto_profile_end (rstto_profile_now (), self, self->generation, "gpu-context",
                       "workers=%u max_texture=%u", self->count, self->limit);
    return self;
}

gboolean
rstto_gpu_renderer_needs_warmup (RsttoGpuRenderer *self)
{
    if (self != NULL && !g_atomic_int_get (&self->failed))
        for (guint i = 0; i < self->count; i++)
            if (!self->workers[i].ready && !self->workers[i].busy)
                return TRUE;
    return FALSE;
}

void
rstto_gpu_warmup_async (RsttoGpuRenderer *self, gboolean persistent, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data)
{
    g_return_if_fail (rstto_gpu_renderer_needs_warmup (self));
    GpuWorker *worker = NULL;
    for (guint i = 0; i < self->count; i++)
        if (!self->workers[i].ready && !self->workers[i].busy) { worker = &self->workers[i]; break; }
    worker->busy = TRUE;
    GpuWork *work = g_new0 (GpuWork, 1);
    work->type = GPU_WARMUP;
    work->persistent = persistent && rstto_gpu_shader_cache_enabled ();
    work->worker = worker;
    work->warm_start = rstto_profile_now ();
    for (guint i = 0; i < G_N_ELEMENTS (work->warm_pixels); i++) work->warm_pixels[i] = 0xff406080;
    work->pixels = (RsttoRenderPixels) { (guchar *) work->warm_pixels, 8, 8, 32, CAIRO_FORMAT_ARGB32 };
    work->key = (RsttoRenderKey) { .width = 8, .height = 8, .render_width = 8, .render_height = 8,
        .pixel_scale = 1, .source_scale = 1, .orientation = 1, .x_scale = 1, .y_scale = 1,
        .filter = RSTTO_GPU_NEAREST };
    work->task = g_task_new (self, cancel, callback, data);
    g_task_set_return_on_cancel (work->task, FALSE);
    g_task_set_check_cancellable (work->task, FALSE);
    g_task_set_task_data (work->task, worker, NULL);
    g_mutex_lock (&self->lock);
    self->stats.active++;
    self->stats.peak_active = MAX (self->stats.peak_active, self->stats.active);
    g_mutex_unlock (&self->lock);
    g_async_queue_push (worker->queue, work);
}

gboolean
rstto_gpu_warmup_finish (RsttoGpuRenderer *self, GAsyncResult *result, GError **error)
{
    GpuWorker *worker = g_task_get_task_data (G_TASK (result));
    worker->ready = g_task_propagate_boolean (G_TASK (result), error);
    worker->busy = FALSE;
    g_mutex_lock (&self->lock);
    self->stats.active--;
    self->stats.warmups += worker->ready;
    g_mutex_unlock (&self->lock);
    return worker->ready;
}

gboolean
rstto_gpu_renderer_available (RsttoGpuRenderer *self)
{
    if (self != NULL)
        for (guint i = 0; i < self->count; i++)
            if (!self->workers[i].busy && (self->workers[i].ready || g_atomic_int_get (&self->failed)))
                return TRUE;
    return FALSE;
}

void
rstto_gpu_render_request_async (RsttoGpuRequest *request, GCancellable *cancel,
                                gboolean (*keep) (gpointer), gpointer useful_data, const gint *demand,
                                GAsyncReadyCallback callback, gpointer data)
{
    g_return_if_fail (rstto_gpu_request_available (request));
    RsttoGpuRenderer *self = request->owner;
    GpuWorker *worker = request->source != NULL ? request->source->worker : NULL;
    if (worker == NULL)
        for (guint i = 0; i < self->count; i++)
            if (!self->workers[i].busy && (self->workers[i].ready || g_atomic_int_get (&self->failed)))
            { worker = &self->workers[i]; break; }
    worker->busy = TRUE;
    request->worker = worker;
    GpuWork *work = g_new0 (GpuWork, 1);
    work->worker = worker; work->pixels = request->pixels; work->key = request->key;
    work->request = request;
    work->useful = keep; work->useful_data = useful_data; work->demand = demand;
    work->task = g_task_new (self, cancel, callback, data);
    g_task_set_return_on_cancel (work->task, FALSE);
    g_task_set_check_cancellable (work->task, FALSE);
    g_task_set_task_data (work->task, request, (GDestroyNotify) rstto_gpu_request_free);
    g_mutex_lock (&self->lock);
    self->stats.active++;
    self->stats.peak_active = MAX (self->stats.peak_active, self->stats.active);
    g_mutex_unlock (&self->lock);
    g_async_queue_push (worker->queue, work);
}

/* Test/standalone callers use anonymous, nonresident sources. The cache uses
 * explicit admission above so it can reserve RAM before any worker starts. */
void
rstto_gpu_render_async (RsttoGpuRenderer *self, const RsttoRenderPixels *source, const RsttoRenderKey *key,
                       GCancellable *cancel, gboolean (*keep) (gpointer), gpointer useful_data, const gint *demand,
                       GAsyncReadyCallback callback, gpointer data)
{
    guint64 staging;
    RsttoGpuRequest *request = rstto_gpu_request_new (self, source, key, 0, &staging);
    if (request != NULL)
        rstto_gpu_render_request_async (request, cancel, keep, useful_data, demand, callback, data);
    else
    {
        GTask *task = g_task_new (self, cancel, callback, data);
        g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "GPU request cannot be admitted");
        g_object_unref (task);
    }
}

RsttoGpuImage *
rstto_gpu_render_finish (RsttoGpuRenderer *self, GAsyncResult *result, GError **error)
{
    RsttoGpuRequest *request = g_task_get_task_data (G_TASK (result));
    if (request != NULL)
    {
        request->worker->busy = FALSE;
        g_mutex_lock (&self->lock);
        self->stats.active--;
        g_mutex_unlock (&self->lock);
    }
    return g_task_propagate_pointer (G_TASK (result), error);
}

RsttoGpuImage *
rstto_gpu_image_ref (RsttoGpuImage *image)
{
    g_atomic_int_inc (&image->refs);
    return image;
}

void
rstto_gpu_image_unref (RsttoGpuImage *image)
{
    if (image == NULL || !g_atomic_int_dec_and_test (&image->refs))
        return;
    RsttoGpuRenderer *self = image->worker->owner;
    g_mutex_lock (&self->lock);
    self->stats.retiring++;
    g_mutex_unlock (&self->lock);
    GpuWork *work = g_new0 (GpuWork, 1);
    work->type = GPU_DELETE;
    work->image = image;
    g_async_queue_push (image->worker->queue, work);
}

gboolean
rstto_gpu_image_draw (RsttoGpuImage *image, cairo_t *cr, GdkWindow *window, gint scale)
{
    if (g_atomic_int_get (&image->worker->owner->failed)) return FALSE;
    gint64 start = rstto_profile_now ();
    cairo_save (cr);
    gdk_cairo_draw_from_gl (cr, window, image->id, GL_TEXTURE, scale, 0, 0, image->width, image->height);
    cairo_restore (cr);
    /* GTK changes the current context. Fence the actual consumer context, not
     * a worker context; repeated draws are ordered in this same paint context. */
    if (image->presented != NULL)
        glDeleteSync (image->presented);
    image->presented = glFenceSync (GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush ();
    if (image->presented == NULL) g_atomic_int_set (&image->worker->owner->failed, TRUE);
    rstto_profile_end (start, image, 0, "gpu-paint", "width=%d height=%d", image->width, image->height);
    return image->presented != NULL;
}

#ifdef RSTTO_VIEWER_TEST
void rstto_gpu_test_disable (RsttoGpuRenderer *self) { g_atomic_int_set (&self->failed, TRUE); }
#endif

#ifdef RSTTO_GPU_TEST
void rstto_gpu_test_budget (RsttoGpuRenderer *self, guint64 bytes, guint64 source_bytes) {
    g_mutex_lock (&self->lock); self->max_bytes = bytes; self->max_source_bytes = source_bytes; g_mutex_unlock (&self->lock);
}
void rstto_gpu_test_limit (RsttoGpuRenderer *self, guint limit) { self->limit = limit; }
void rstto_gpu_test_hold (RsttoGpuRenderer *self, gboolean hold) { g_atomic_int_set (&self->hold, hold); }
void rstto_gpu_test_reference (RsttoGpuRenderer *self, gboolean reference) { g_atomic_int_set (&self->reference, reference); }
void rstto_gpu_test_fail (RsttoGpuRenderer *self, guint stage) { g_atomic_int_set (&self->fail, stage); }
cairo_surface_t *
rstto_gpu_test_download (RsttoGpuImage *image)
{
    GpuWork *work = g_new0 (GpuWork, 1);
    work->type = GPU_READ; work->image = image;
    g_mutex_init (&work->mutex); g_cond_init (&work->cond);
    g_mutex_lock (&work->mutex);
    g_async_queue_push (image->worker->queue, work);
    while (!work->done)
        g_cond_wait (&work->cond, &work->mutex);
    g_mutex_unlock (&work->mutex);
    cairo_surface_t *surface = work->download;
    g_mutex_clear (&work->mutex); g_cond_clear (&work->cond); g_free (work);
    return surface;
}
#endif
#else
void rstto_gpu_source_select (RsttoGpuRenderer *self, guint64 serial) { }
void rstto_gpu_source_invalidate (RsttoGpuRenderer *self, guint64 serial) { }
RsttoGpuRequest *rstto_gpu_request_new (RsttoGpuRenderer *self, const RsttoRenderPixels *pixels,
    const RsttoRenderKey *key, guint64 serial, guint64 *host_bytes) { return NULL; }
void rstto_gpu_request_free (RsttoGpuRequest *request) { }
gboolean rstto_gpu_request_available (RsttoGpuRequest *request) { return FALSE; }
void rstto_gpu_render_request_async (RsttoGpuRequest *request, GCancellable *cancel,
    gboolean (*useful) (gpointer), gpointer useful_data, const gint *demand,
    GAsyncReadyCallback callback, gpointer data) { g_assert_not_reached (); }
RsttoGpuRenderer *rstto_gpu_renderer_new (GdkWindow *window, guint workers, GError **error)
{
    g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Built without libplacebo OpenGL support");
    return NULL;
}
gboolean rstto_gpu_renderer_needs_warmup (RsttoGpuRenderer *self) { return FALSE; }
void rstto_gpu_warmup_async (RsttoGpuRenderer *self, gboolean persistent, GCancellable *cancel, GAsyncReadyCallback callback, gpointer data) { g_assert_not_reached (); }
gboolean rstto_gpu_warmup_finish (RsttoGpuRenderer *self, GAsyncResult *result, GError **error) { return FALSE; }
gboolean rstto_gpu_renderer_available (RsttoGpuRenderer *self) { return FALSE; }
void rstto_gpu_render_async (RsttoGpuRenderer *self, const RsttoRenderPixels *source, const RsttoRenderKey *key,
                            GCancellable *cancel, gboolean (*keep) (gpointer), gpointer useful_data, const gint *demand,
                            GAsyncReadyCallback callback, gpointer data) { g_assert_not_reached (); }
RsttoGpuImage *rstto_gpu_render_finish (RsttoGpuRenderer *self, GAsyncResult *result, GError **error) { return NULL; }
RsttoGpuImage *rstto_gpu_image_ref (RsttoGpuImage *image) { return image; }
void rstto_gpu_image_unref (RsttoGpuImage *image) { }
gboolean rstto_gpu_image_draw (RsttoGpuImage *image, cairo_t *cr, GdkWindow *window, gint scale) { return FALSE; }
#endif

static void
rstto_gpu_renderer_finalize (GObject *object)
{
    RsttoGpuRenderer *self = RSTTO_GPU_RENDERER (object);
#ifdef HAVE_PLACEBO
    rstto_gpu_source_select (self, 0);
    for (guint i = 0; i < self->count; i++)
    {
        GpuWork *work = g_new0 (GpuWork, 1);
        work->type = GPU_STOP;
        g_async_queue_push (self->workers[i].queue, work);
    }
    for (guint i = 0; i < self->count; i++)
    {
        g_thread_join (self->workers[i].thread);
        g_async_queue_unref (self->workers[i].queue);
        g_object_unref (self->workers[i].context);
    }
    g_free (self->workers);
#endif
    g_main_context_unref (self->main_context);
    g_mutex_clear (&self->lock);
    G_OBJECT_CLASS (rstto_gpu_renderer_parent_class)->finalize (object);
}

static void rstto_gpu_renderer_class_init (RsttoGpuRendererClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = rstto_gpu_renderer_finalize;
}

static void rstto_gpu_renderer_init (RsttoGpuRenderer *self)
{
    static guint64 generation;
    self->generation = ++generation; /* construction is on the GTK thread */
    self->max_bytes = RSTTO_GPU_MAX_BYTES;
    self->max_source_bytes = RSTTO_GPU_MAX_SOURCE_BYTES;
    self->main_context = g_main_context_ref_thread_default ();
    g_mutex_init (&self->lock);
}

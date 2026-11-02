/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "image_cache.h"

#include <gio/gfiledescriptorbased.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

#define MIB ((guint64) 1024 * 1024)
#define PROBE_LIMIT MIB
#define PROBE_BUFFER_SIZE 4096
#define READ_BUFFER_SIZE (1024 * 1024)
#define MAX_IMAGE_SIZE 32767
#define PRESSURE_COOLDOWN (30 * G_USEC_PER_SEC)

typedef struct
{
    guint64 device, inode, size;
    gint64 modified, changed;
    guint32 modified_nsec, changed_nsec;
} FileIdentity;

typedef struct
{
    gchar *uri;
    FileIdentity identity;
    RsttoDecodeOptions options;
} CacheKey;

typedef struct _Entry
{
    gint refs;
    RsttoImageCache *owner; /* owner outlives every entry */
    CacheKey *key;
    RsttoImageData image;
    guint64 bytes;
    GList *link;
    gboolean demanded;
} Entry;

typedef enum
{
    IDENTIFY,
    PROBE,
    DECODE,
    REOPEN_DECODE,
    WAIT_PROBE,
    WAIT_UNCACHED
} JobPhase;

typedef struct
{
    RsttoImageCache *owner;
    GFile *file;
    gchar *uri, *mime_type;
    RsttoDecodeOptions options;
    GCancellable *cancel;
    GFileInputStream *stream;
    FileIdentity identity;
    gboolean identified, changed, running, cacheable, rejected;
    gint demand; /* atomic: promotion is the only worker-visible mutation */
    guint64 generation, reservation;
    gboolean reserves_slot, foreground_slot;
    JobPhase phase;
    gint width, height, decoded_width, decoded_height;
    RsttoImageData image;
} DecodeJob;

struct _RsttoImageCache
{
    GObject parent;
    GHashTable *entries; /* key owned by value; value destroy releases both */
    GHashTable *requests; /* borrowed DecodeJob keys/values, owned by jobs */
    GQueue lru, jobs, pending;
    GHashTable *targets; /* URI -> priority, bounded by candidate capacity */
    Entry *displayed;
    GFile *selected;
    gchar *mime_type;
    gchar **excluded_mime_types;
    RsttoDecodeOptions options;
    RsttoCacheLimits limits;
    guint64 generation, resident_bytes, decodes, hits;
    guint resident_slots;
    gboolean busy, stopped, pumping, speculation_allowed, foreground_pending, pressure_blocked;
    gint64 pressure_until;
    GMemoryMonitor *memory_monitor;
};

G_DEFINE_TYPE (RsttoImageCache, rstto_image_cache, G_TYPE_OBJECT)

enum
{
    READY,
    IDENTITY_CHANGED,
    N_SIGNALS
};
static guint signals[N_SIGNALS];
static void
pump (RsttoImageCache *self);
static void
launch (DecodeJob *job);
static void
evict_all (RsttoImageCache *self);

static void
image_clear (RsttoImageData *image)
{
    g_clear_pointer (&image->surface, cairo_surface_destroy);
    g_clear_object (&image->animation);
    g_clear_error (&image->error);
}

static gboolean
options_equal (const RsttoDecodeOptions *a, const RsttoDecodeOptions *b)
{
    return a->limit_quality == b->limit_quality
           && a->width == b->width && a->height == b->height && a->ephemeral == b->ephemeral;
}

static gboolean
identity_equal (const FileIdentity *a, const FileIdentity *b)
{
    return a->device == b->device && a->inode == b->inode && a->size == b->size
           && a->modified == b->modified && a->changed == b->changed
           && a->modified_nsec == b->modified_nsec && a->changed_nsec == b->changed_nsec;
}

static gboolean
identity_from_stat (const struct stat *st, FileIdentity *identity)
{
    if (!S_ISREG (st->st_mode) || st->st_size < 0)
        return FALSE;
    identity->device = st->st_dev;
    identity->inode = st->st_ino;
    identity->size = st->st_size;
    identity->modified = st->st_mtime;
    identity->changed = st->st_ctime;
#if defined(__APPLE__)
    identity->modified_nsec = st->st_mtimespec.tv_nsec;
    identity->changed_nsec = st->st_ctimespec.tv_nsec;
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)
    identity->modified_nsec = st->st_mtim.tv_nsec;
    identity->changed_nsec = st->st_ctim.tv_nsec;
#else
    /* Do not silently weaken cache identity on an unsupported stat layout. */
    return FALSE;
#endif
    return TRUE;
}

static gboolean
path_identity (GFile *file, FileIdentity *identity)
{
    gchar *path = g_file_get_path (file);
    struct stat st;
    gboolean valid = path != NULL && g_stat (path, &st) == 0
                     && identity_from_stat (&st, identity);
    g_free (path);
    return valid;
}

static gboolean
stream_identity (DecodeJob *job, FileIdentity *identity)
{
    struct stat st;
    return G_IS_FILE_DESCRIPTOR_BASED (job->stream)
           && fstat (g_file_descriptor_based_get_fd (G_FILE_DESCRIPTOR_BASED (job->stream)), &st) == 0
           && identity_from_stat (&st, identity);
}

static guint
key_hash (gconstpointer data)
{
    const CacheKey *key = data;
    return g_str_hash (key->uri) ^ (guint) key->identity.inode
           ^ (guint) key->identity.modified ^ key->identity.modified_nsec
           ^ (guint) key->options.width ^ ((guint) key->options.height << 16);
}

static gboolean
key_equal (gconstpointer a, gconstpointer b)
{
    const CacheKey *ka = a, *kb = b;
    return g_str_equal (ka->uri, kb->uri)
           && identity_equal (&ka->identity, &kb->identity)
           && options_equal (&ka->options, &kb->options);
}

/* Identity is established by the worker, before cache lookup and again before
 * delivery. While that is in progress, coalesce by URI and immutable options. */
static guint
request_hash (gconstpointer data)
{
    const DecodeJob *job = data;
    return g_str_hash (job->uri) ^ (guint) job->options.width
           ^ ((guint) job->options.height << 16);
}

static gboolean
request_equal (gconstpointer a, gconstpointer b)
{
    const DecodeJob *ja = a, *jb = b;
    return g_str_equal (ja->uri, jb->uri) && options_equal (&ja->options, &jb->options);
}

static Entry *
entry_ref (Entry *entry)
{
    entry->refs++;
    return entry;
}

static void
entry_unref (gpointer data)
{
    Entry *entry = data;
    if (--entry->refs != 0)
        return;
    g_assert (entry->link == NULL);
    entry->owner->resident_bytes -= entry->bytes;
    entry->owner->resident_slots--;
    image_clear (&entry->image);
    if (entry->key != NULL)
    {
        g_free (entry->key->uri);
        g_free (entry->key);
    }
    g_free (entry);
}

static void
remove_entry (RsttoImageCache *self, Entry *entry)
{
    g_queue_delete_link (&self->lru, entry->link);
    entry->link = NULL;
    g_hash_table_remove (self->entries, entry->key);
}

static void
touch_entry (RsttoImageCache *self, Entry *entry)
{
    entry->demanded = TRUE;
    if (entry->link != NULL)
    {
        g_queue_unlink (&self->lru, entry->link);
        g_queue_push_head_link (&self->lru, entry->link);
    }
}

static guint64
reserved_bytes (RsttoImageCache *self)
{
    guint64 bytes = 0;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        bytes += ((DecodeJob *) lp->data)->reservation;
    return bytes;
}

static guint
reserved_slots (RsttoImageCache *self)
{
    guint count = 0;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        count += ((DecodeJob *) lp->data)->reserves_slot;
    return count;
}

static gboolean
enabled (RsttoImageCache *self)
{
    return self->limits.enabled && self->limits.max_images > 0
           && self->limits.max_bytes > 0 && self->limits.max_image_bytes > 0;
}

static gboolean
fits (RsttoImageCache *self, guint64 bytes, guint slots)
{
    guint64 used = self->resident_bytes + reserved_bytes (self);
    return enabled (self) && used <= self->limits.max_bytes
           && bytes <= self->limits.max_bytes - used
           && (guint64) self->resident_slots + reserved_slots (self) + slots <= self->limits.max_images;
}

static gboolean
make_room (RsttoImageCache *self, guint64 bytes, guint slots, guint priority)
{
    GList *lp = self->lru.tail;
    while (!fits (self, bytes, slots) && lp != NULL)
    {
        Entry *entry = lp->data;
        lp = lp->prev;
        guint rank = GPOINTER_TO_UINT (g_hash_table_lookup (self->targets, entry->key->uri));
        if (entry != self->displayed && (priority == 0 || rank == 0 || rank > priority))
            remove_entry (self, entry);
    }
    return fits (self, bytes, slots);
}

static void
evict_all (RsttoImageCache *self)
{
    while (!g_queue_is_empty (&self->lru))
        remove_entry (self, self->lru.tail->data);
}

static gboolean
rgba_bytes (gint width, gint height, guint64 *bytes)
{
    if (width <= 0 || height <= 0 || (guint64) width > G_MAXUINT64 / 4 / (guint64) height)
        return FALSE;
    *bytes = (guint64) width * height * 4;
    return TRUE;
}

static void
target_size (DecodeJob *job, gint width, gint height)
{
    gint size;
    job->width = width;
    job->height = height;
    job->decoded_width = width;
    job->decoded_height = height;
    if ((job->options.limit_quality
         && (width > job->options.width || height > job->options.height))
        || width >= MAX_IMAGE_SIZE || height >= MAX_IMAGE_SIZE)
    {
        if (height < width)
        {
            size = MIN (width, job->options.limit_quality ? job->options.width : MAX_IMAGE_SIZE - 1);
            job->decoded_width = MAX (1, size);
            job->decoded_height = MAX (1, (gint) ((gdouble) size * height / width));
        }
        else
        {
            size = MIN (height, job->options.limit_quality ? job->options.height : MAX_IMAGE_SIZE - 1);
            job->decoded_height = MAX (1, size);
            job->decoded_width = MAX (1, (gint) ((gdouble) size * width / height));
        }
    }
}

static void
probe_size (GdkPixbufLoader *loader, gint width, gint height, gpointer data)
{
    DecodeJob *job = data;
    if (width > 0 && height > 0)
        target_size (job, width, height);
    /* Same size-only mechanism used by gdk_pixbuf_get_file_info(). */
    gdk_pixbuf_loader_set_size (loader, 0, 0);
}

static void
decode_size (GdkPixbufLoader *loader, gint width, gint height, gpointer data)
{
    DecodeJob *job = data;
    if (width <= 0 || height <= 0)
    {
        job->rejected = TRUE;
        gdk_pixbuf_loader_set_size (loader, 0, 0);
        return;
    }
    /* A changed header must not turn an admitted decode into a larger one. */
    if (job->cacheable && (width != job->width || height != job->height))
    {
        job->changed = TRUE;
        job->rejected = TRUE;
        gdk_pixbuf_loader_set_size (loader, 0, 0);
        return;
    }
    target_size (job, width, height);
    gdk_pixbuf_loader_set_size (loader, job->decoded_width, job->decoded_height);
}

static GdkPixbufLoader *
new_loader (DecodeJob *job)
{
    GdkPixbufLoader *loader = NULL;
    if (job->mime_type != NULL)
        loader = gdk_pixbuf_loader_new_with_mime_type (job->mime_type, NULL);
    return loader != NULL ? loader : gdk_pixbuf_loader_new ();
}

/* Cairo's native-endian premultiplied ARGB32/RGB24 representation. Conversion
 * takes place in the worker and allocates one surface, with no GDK/GTK calls. */
static cairo_surface_t *
surface_from_pixbuf (GdkPixbuf *pixbuf)
{
    gint width = gdk_pixbuf_get_width (pixbuf), height = gdk_pixbuf_get_height (pixbuf);
    gint channels = gdk_pixbuf_get_n_channels (pixbuf);
    gboolean alpha = gdk_pixbuf_get_has_alpha (pixbuf);
    cairo_surface_t *surface = cairo_image_surface_create (alpha ? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24,
                                                           width, height);
    if (cairo_surface_status (surface) != CAIRO_STATUS_SUCCESS)
        return surface;
    for (gint y = 0; y < height; y++)
    {
        const guchar *src = gdk_pixbuf_get_pixels (pixbuf) + (gsize) y * gdk_pixbuf_get_rowstride (pixbuf);
        guint32 *dst = (guint32 *) (cairo_image_surface_get_data (surface)
                                    + (gsize) y * cairo_image_surface_get_stride (surface));
        for (gint x = 0; x < width; x++, src += channels)
        {
            guint a = alpha ? src[3] : 255;
            guint r = src[0] * a + 128, g = src[1] * a + 128, b = src[2] * a + 128;
            dst[x] = (a << 24) | (((r + (r >> 8)) >> 8) << 16)
                     | (((g + (g >> 8)) >> 8) << 8) | ((b + (b >> 8)) >> 8);
        }
    }
    cairo_surface_mark_dirty (surface);
    return surface;
}

#ifdef RSTTO_CACHE_TEST
static void (*worker_hook) (DecodeJob *job);
#endif

static void
worker (GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    DecodeJob *job = data;
    GdkPixbufLoader *loader = NULL;
    GdkPixbuf *pixbuf;
    GdkPixbufAnimation *animation;
    FileIdentity fd_identity, current;
    guchar *buffer = NULL;
    gsize buffer_size;
    gsize total = 0;
    gssize count;
    gboolean closed = FALSE;

#ifdef RSTTO_CACHE_TEST
    if (worker_hook != NULL)
        worker_hook (job);
#endif
    if (g_cancellable_is_cancelled (cancel))
        goto done;
    if (job->phase == IDENTIFY)
    {
        if (!g_atomic_int_get (&job->demand) && !path_identity (job->file, &current))
            goto done;
        job->stream = g_file_read (job->file, cancel, &job->image.error);
        if (job->stream != NULL && g_file_is_native (job->file))
            job->identified = stream_identity (job, &job->identity)
                              && path_identity (job->file, &current)
                              && identity_equal (&job->identity, &current);
        goto done;
    }

    if (job->phase == REOPEN_DECODE)
    {
        g_clear_object (&job->stream);
        g_clear_error (&job->image.error);
        job->stream = g_file_read (job->file, cancel, &job->image.error);
        if (job->stream == NULL)
            goto done;
        job->identified = stream_identity (job, &job->identity)
                          && path_identity (job->file, &current)
                          && identity_equal (&job->identity, &current);
    }

    buffer_size = job->phase == PROBE || !g_atomic_int_get (&job->demand)
                      ? PROBE_BUFFER_SIZE
                      : READ_BUFFER_SIZE;
    buffer = g_malloc (buffer_size);
    loader = new_loader (job);
    if (job->phase == PROBE)
    {
        g_signal_connect (loader, "size-prepared", G_CALLBACK (probe_size), job);
        while (total < PROBE_LIMIT && !g_cancellable_is_cancelled (cancel))
        {
            count = g_input_stream_read (G_INPUT_STREAM (job->stream), buffer, buffer_size, cancel, NULL);
            if (count <= 0)
                break;
            total += count;
            if (!gdk_pixbuf_loader_write (loader, buffer, count, NULL) || job->width > 0)
                break;
        }
        gdk_pixbuf_loader_close (loader, NULL);
        closed = TRUE;
        if (!g_seekable_seek (G_SEEKABLE (job->stream), 0, G_SEEK_SET, cancel, NULL))
            job->identified = FALSE;
        goto done;
    }

    g_signal_connect (loader, "size-prepared", G_CALLBACK (decode_size), job);
    while (!g_cancellable_is_cancelled (cancel))
    {
        count = g_input_stream_read (G_INPUT_STREAM (job->stream), buffer, buffer_size,
                                     cancel, &job->image.error);
        if (count <= 0)
            break;
        if (!gdk_pixbuf_loader_write (loader, buffer, count, &job->image.error) || job->rejected)
            break;
        animation = gdk_pixbuf_loader_get_animation (loader);
        if (!g_atomic_int_get (&job->demand) && animation != NULL
            && !gdk_pixbuf_animation_is_static_image (animation))
        {
            job->rejected = TRUE;
            break;
        }
    }
    gdk_pixbuf_loader_close (loader, job->image.error == NULL && !job->rejected ? &job->image.error : NULL);
    closed = TRUE;
    if (g_cancellable_is_cancelled (cancel) || job->rejected)
        goto done;

    pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
    animation = gdk_pixbuf_loader_get_animation (loader);
    if (pixbuf != NULL
        && (job->image.error == NULL
            || (g_atomic_int_get (&job->demand)
                && g_error_matches (job->image.error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_CORRUPT_IMAGE))))
    {
        guint64 actual, expected;
        job->image.decoded_width = gdk_pixbuf_get_width (pixbuf);
        job->image.decoded_height = gdk_pixbuf_get_height (pixbuf);
        if (job->cacheable
            && (!rgba_bytes (job->image.decoded_width, job->image.decoded_height, &actual)
                || !rgba_bytes (job->decoded_width, job->decoded_height, &expected)
                || actual > expected))
        {
            job->rejected = TRUE;
            goto done;
        }
        job->image.surface = surface_from_pixbuf (pixbuf);
        if (cairo_surface_status (job->image.surface) != CAIRO_STATUS_SUCCESS)
        {
            g_clear_pointer (&job->image.surface, cairo_surface_destroy);
            g_clear_error (&job->image.error);
            g_set_error_literal (&job->image.error, G_IO_ERROR, G_IO_ERROR_NO_SPACE, "Unable to allocate image surface");
        }
        job->image.width = job->width;
        job->image.height = job->height;
        job->image.quality_scale = job->width > 0 ? (gdouble) job->image.decoded_width / job->width : 1.0;
        job->image.has_alpha = gdk_pixbuf_get_has_alpha (pixbuf);
        if (animation != NULL && !gdk_pixbuf_animation_is_static_image (animation))
            job->image.animation = g_object_ref (animation);
    }
    else if (job->image.error == NULL)
        g_set_error_literal (&job->image.error, GDK_PIXBUF_ERROR, GDK_PIXBUF_ERROR_FAILED, "Image contains no pixels");

    if (job->identified)
    {
        job->changed = !stream_identity (job, &fd_identity) || !path_identity (job->file, &current)
                       || !identity_equal (&job->identity, &fd_identity)
                       || !identity_equal (&job->identity, &current);
    }
done:
    if (loader != NULL)
    {
        if (!closed)
            gdk_pixbuf_loader_close (loader, NULL);
        g_object_unref (loader);
    }
    g_free (buffer);
    /* Do not return-on-cancel: completion must mean decoder cleanup is done. */
    g_task_return_boolean (task, TRUE);
}

static void
job_free (DecodeJob *job)
{
    if (g_hash_table_lookup (job->owner->requests, job) == job)
        g_hash_table_remove (job->owner->requests, job);
    g_clear_object (&job->file);
    g_clear_object (&job->stream);
    g_clear_object (&job->cancel);
    image_clear (&job->image);
    g_free (job->uri);
    g_free (job->mime_type);
    g_free (job);
}

static gboolean
is_current (RsttoImageCache *self, DecodeJob *job)
{
    return !self->stopped && g_atomic_int_get (&job->demand)
           && job->generation == self->generation && !g_cancellable_is_cancelled (job->cancel);
}

static void
publish (RsttoImageCache *self, Entry *entry)
{
    Entry *old = self->displayed;
    entry_ref (entry); /* synchronous signal handlers may clear the cache */
    self->displayed = entry_ref (entry);
    touch_entry (self, entry);
    self->busy = FALSE;
    self->foreground_pending = FALSE;
    /* Old display memory remains charged throughout synchronous delivery. */
    g_signal_emit (self, signals[READY], 0, &entry->image);
    if (old != NULL)
        entry_unref (old);
    entry_unref (entry);
}

static Entry *
entry_from_job (RsttoImageCache *self, DecodeJob *job)
{
    Entry *entry = g_new0 (Entry, 1);
    entry->refs = 1;
    entry->owner = self;
    entry->image = job->image;
    memset (&job->image, 0, sizeof job->image);
    if (entry->image.surface != NULL)
        entry->bytes = (guint64) cairo_image_surface_get_stride (entry->image.surface)
                       * cairo_image_surface_get_height (entry->image.surface);
    self->resident_bytes += entry->bytes;
    self->resident_slots++;
    return entry;
}

static void
cancel_other_jobs (RsttoImageCache *self, DecodeJob *except)
{
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        if (lp->data != except)
            g_cancellable_cancel (((DecodeJob *) lp->data)->cancel);
}

static void
uncached (DecodeJob *job)
{
    RsttoImageCache *self = job->owner;
    job->cacheable = FALSE;
    job->reservation = 0;
    job->reserves_slot = FALSE;
    job->running = FALSE;
    self->speculation_allowed = FALSE;
    evict_all (self);
    cancel_other_jobs (self, job);
    job->phase = WAIT_UNCACHED;
}

static void
finish_job (DecodeJob *job)
{
    RsttoImageCache *self = job->owner;
    g_queue_remove (&self->jobs, job);
    job_free (job);
    pump (self);
}

static void
wait_or_uncached (DecodeJob *job)
{
    RsttoImageCache *self = job->owner;
    if (enabled (self) && job->identified && !job->options.ephemeral && self->jobs.length > 1)
    {
        /* A cancelled decoder still owns its reservation. Retry admission only
         * after cleanup instead of unnecessarily making this image uncached. */
        cancel_other_jobs (self, job);
        job->running = FALSE;
        job->phase = WAIT_PROBE;
    }
    else
        uncached (job);
    pump (self);
}

static void
job_done (GObject *source, GAsyncResult *result, gpointer data)
{
    RsttoImageCache *self = RSTTO_IMAGE_CACHE (source);
    DecodeJob *job = data;
    gboolean current;
    guint priority;
    Entry *entry;
    guint64 source_bytes, output_bytes, reservation;
    CacheKey key = { job->uri, job->identity, job->options };

    g_task_propagate_boolean (G_TASK (result), NULL);
    /* The callback still owns this job. Signal handlers may stop or clear the
     * cache, but must not free a job whose completion is being delivered. */
    if (g_cancellable_is_cancelled (job->cancel) || self->stopped)
    {
        finish_job (job);
        return;
    }
    current = is_current (self, job);
    priority = current ? 0 : GPOINTER_TO_UINT (g_hash_table_lookup (self->targets, job->uri));
    if (job->phase == IDENTIFY && job->stream != NULL)
    {
        if (enabled (self) && job->identified && !job->options.ephemeral)
        {
            /* A fresh descriptor/path identity precedes every lookup. */
            entry = g_hash_table_lookup (self->entries, &key);
            if (entry != NULL)
            {
                if (current)
                {
                    self->hits++;
                    self->speculation_allowed = TRUE;
                    publish (self, entry);
                }
                finish_job (job);
                return;
            }
            gboolean stale = self->displayed != NULL && self->displayed->key != NULL
                             && g_str_equal (self->displayed->key->uri, job->uri)
                             && !identity_equal (&self->displayed->key->identity, &job->identity);
            for (GList *lp = self->lru.head, *next; lp != NULL; lp = next)
            {
                Entry *old = lp->data;
                next = lp->next;
                if (g_str_equal (old->key->uri, job->uri)
                    && !identity_equal (&old->key->identity, &job->identity))
                {
                    stale = TRUE;
                    remove_entry (self, old);
                }
            }
            if (stale)
            {
                /* Metadata may have missed a monitor notification; an old MIME
                 * hint must not force a replaced file through the wrong loader. */
                g_clear_pointer (&job->mime_type, g_free);
                g_signal_emit (self, signals[IDENTITY_CHANGED], 0, job->file);
            }
            if (g_cancellable_is_cancelled (job->cancel) || self->stopped)
            {
                finish_job (job);
                return;
            }
            if (make_room (self, PROBE_BUFFER_SIZE, 1, priority))
            {
                job->reservation = PROBE_BUFFER_SIZE;
                job->reserves_slot = TRUE;
                job->phase = PROBE;
                launch (job);
                return;
            }
        }
        if (current)
            wait_or_uncached (job);
        else
            finish_job (job);
        return;
    }
    if (job->phase == PROBE)
    {
        job->reservation = 0;
        if (job->identified && rgba_bytes (job->width, job->height, &source_bytes)
            && rgba_bytes (job->decoded_width, job->decoded_height, &output_bytes)
            && source_bytes <= self->limits.max_image_bytes
            && output_bytes <= self->limits.max_image_bytes
            && output_bytes <= (G_MAXUINT64 - source_bytes - READ_BUFFER_SIZE) / 3)
        {
            reservation = source_bytes + 3 * output_bytes + READ_BUFFER_SIZE;
            if (make_room (self, reservation, 0, priority))
            {
                job->cacheable = TRUE;
                job->reservation = reservation;
                job->reserves_slot = TRUE;
                job->phase = DECODE;
                self->decodes++;
                launch (job);
                return;
            }
        }
        if (current)
            wait_or_uncached (job);
        else
            finish_job (job);
        return;
    }

    /* All decoder allocations are gone before replacing the reservation. */
    job->reservation = 0;
    job->reserves_slot = FALSE;
    if (job->changed || job->rejected)
    {
        image_clear (&job->image);
        if (current)
        {
            if (job->changed)
            {
                g_set_error_literal (&job->image.error, G_IO_ERROR, G_IO_ERROR_BUSY,
                                     "Image changed while it was being loaded; refresh to retry");
                g_signal_emit (self, signals[IDENTITY_CHANGED], 0, job->file);
            }
            else
            {
                /* An animation preload can be promoted just as it is rejected. */
                job->rejected = FALSE;
                uncached (job);
                pump (self);
                return;
            }
        }
        else
        {
            finish_job (job);
            return;
        }
    }
    entry = entry_from_job (self, job);
    if (job->cacheable && !job->changed && entry->image.surface != NULL
        && entry->image.animation == NULL && entry->image.error == NULL
        && enabled (self) && entry->bytes <= self->limits.max_image_bytes
        && make_room (self, 0, 0, priority))
    {
        entry->key = g_new (CacheKey, 1);
        *entry->key = key;
        entry->key->uri = g_strdup (job->uri);
        /* No duplicate can own this link: requests are coalesced while live. */
        Entry *duplicate = g_hash_table_lookup (self->entries, entry->key);
        if (duplicate != NULL)
            remove_entry (self, duplicate);
        g_hash_table_insert (self->entries, entry->key, entry_ref (entry));
        g_queue_push_tail (&self->lru, entry);
        entry->link = self->lru.tail;
    }
    if (is_current (self, job))
    {
        self->speculation_allowed = entry->link != NULL;
        if (!self->speculation_allowed)
            evict_all (self);
        publish (self, entry);
    }
    entry_unref (entry);
    finish_job (job);
}

static void
launch (DecodeJob *job)
{
    GTask *task = g_task_new (job->owner, job->cancel, job_done, job);
    job->running = TRUE;
    g_task_set_return_on_cancel (task, FALSE);
    g_task_set_priority (task, g_atomic_int_get (&job->demand) ? G_PRIORITY_DEFAULT : G_PRIORITY_LOW);
    g_task_set_task_data (task, job, NULL);
    g_task_run_in_thread (task, worker);
    g_object_unref (task);
}

static DecodeJob *
new_job (RsttoImageCache *self, GFile *file, gboolean demand)
{
    DecodeJob *job = g_new0 (DecodeJob, 1);
    job->owner = self;
    job->file = g_object_ref (file);
    job->uri = g_file_get_uri (file);
    job->options = self->options;
    job->generation = self->generation;
    job->demand = demand;
    job->foreground_slot = demand;
    job->phase = IDENTIFY;
    job->cancel = g_cancellable_new ();
    if (demand && self->mime_type != NULL
        && !g_strv_contains ((const gchar *const *) self->excluded_mime_types, self->mime_type))
        job->mime_type = g_strdup (self->mime_type);
    g_queue_push_tail (&self->jobs, job);
    g_hash_table_replace (self->requests, job, job);
    return job;
}

static void
pump (RsttoImageCache *self)
{
    if (self->stopped || self->pumping)
        return;
    self->pumping = TRUE;
    for (GList *lp = self->jobs.head, *next; lp != NULL; lp = next)
    {
        DecodeJob *job = lp->data;
        next = lp->next;
        if (!job->running && g_cancellable_is_cancelled (job->cancel))
        {
            g_queue_delete_link (&self->jobs, lp);
            job_free (job);
        }
        else if (!job->running && self->jobs.length == 1)
        {
            if (job->phase == WAIT_PROBE
                && make_room (self, PROBE_BUFFER_SIZE, !job->reserves_slot, 0))
            {
                job->reservation = PROBE_BUFFER_SIZE;
                job->reserves_slot = TRUE;
                job->width = job->height = 0;
                job->phase = PROBE;
                launch (job);
                continue;
            }
            uncached (job);
            /* Reopening (including remote I/O) belongs in the worker. */
            job->phase = REOPEN_DECODE;
            self->decodes++;
            launch (job);
        }
    }
    if (self->foreground_pending && self->jobs.length < 2)
    {
        gboolean found = FALSE;
        for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        {
            DecodeJob *job = lp->data;
            found |= job->foreground_slot || is_current (self, job);
        }
        if (!found)
        {
            self->foreground_pending = FALSE;
            launch (new_job (self, self->selected, TRUE));
        }
    }
    if (!self->busy && self->jobs.length == 0 && self->speculation_allowed && !self->pressure_blocked
        && enabled (self) && g_get_monotonic_time () >= self->pressure_until)
    {
        GFile *file = g_queue_pop_head (&self->pending);
        if (file != NULL)
        {
            launch (new_job (self, file, FALSE));
            g_object_unref (file);
        }
    }
    self->pumping = FALSE;
}

static void
pressure (GMemoryMonitor *monitor, GMemoryMonitorWarningLevel level, gpointer data)
{
    RsttoImageCache *self = data;
    self->pressure_until = g_get_monotonic_time () + PRESSURE_COOLDOWN;
    self->pressure_blocked = TRUE;
    self->speculation_allowed = FALSE;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        if (!is_current (self, lp->data))
            g_cancellable_cancel (((DecodeJob *) lp->data)->cancel);
    evict_all (self);
}

static void
rstto_image_cache_finalize (GObject *object)
{
    RsttoImageCache *self = RSTTO_IMAGE_CACHE (object);
    g_assert (g_queue_is_empty (&self->jobs));
    rstto_image_cache_clear (self);
    g_hash_table_unref (self->entries);
    g_hash_table_unref (self->requests);
    g_hash_table_unref (self->targets);
    g_strfreev (self->excluded_mime_types);
    g_clear_object (&self->memory_monitor);
    G_OBJECT_CLASS (rstto_image_cache_parent_class)->finalize (object);
}

static void
rstto_image_cache_class_init (RsttoImageCacheClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = rstto_image_cache_finalize;
    signals[READY] = g_signal_new ("ready", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                   0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_POINTER);
    signals[IDENTITY_CHANGED] = g_signal_new ("identity-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_FILE);
}

static void
rstto_image_cache_init (RsttoImageCache *self)
{
    self->entries = g_hash_table_new_full (key_hash, key_equal, NULL, entry_unref);
    self->requests = g_hash_table_new (request_hash, request_equal);
    self->targets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_queue_init (&self->lru);
    g_queue_init (&self->jobs);
    g_queue_init (&self->pending);
    self->limits = (RsttoCacheLimits) { TRUE, 5, 256 * MIB, 128 * MIB };
    self->excluded_mime_types = g_new0 (gchar *, 1);
    self->memory_monitor = g_memory_monitor_dup_default ();
    g_signal_connect_object (self->memory_monitor, "low-memory-warning", G_CALLBACK (pressure), self, 0);
}

RsttoImageCache *
rstto_image_cache_new (void)
{
    return g_object_new (RSTTO_TYPE_IMAGE_CACHE, NULL);
}

void
rstto_image_cache_set_excluded_mime_types (RsttoImageCache *self, const gchar *const *types)
{
    g_strfreev (self->excluded_mime_types);
    self->excluded_mime_types = types != NULL ? g_strdupv ((gchar **) types) : g_new0 (gchar *, 1);
}

void
rstto_image_cache_set_limits (RsttoImageCache *self, const RsttoCacheLimits *limits)
{
    self->limits = *limits;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (!is_current (self, job))
            g_cancellable_cancel (job->cancel);
    }
    if (!enabled (self))
        evict_all (self);
    else
    {
        for (GList *lp = self->lru.head, *next; lp != NULL; lp = next)
        {
            Entry *entry = lp->data;
            guint64 full;
            next = lp->next;
            if (entry->bytes > limits->max_image_bytes
                || !rgba_bytes (entry->image.width, entry->image.height, &full)
                || full > limits->max_image_bytes)
                remove_entry (self, entry);
        }
        make_room (self, 0, 0, 0);
    }
    if (!fits (self, 0, 0))
        for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        {
            DecodeJob *job = lp->data;
            if (is_current (self, job) && job->cacheable)
            {
                self->foreground_pending = TRUE;
                g_cancellable_cancel (job->cancel);
            }
        }
    pump (self);
}

void
rstto_image_cache_request (RsttoImageCache *self, GFile *file, const gchar *mime_type, const RsttoDecodeOptions *options)
{
    if (self->stopped)
        return;
    self->generation++;
    if ((self->selected == NULL || !g_file_equal (self->selected, file))
        && g_get_monotonic_time () >= self->pressure_until)
        self->pressure_blocked = FALSE;
    if (!options_equal (&self->options, options))
        evict_all (self);
    self->options = *options;
    g_set_object (&self->selected, file);
    g_free (self->mime_type);
    self->mime_type = g_strdup (mime_type);
    self->busy = TRUE;
    self->foreground_pending = TRUE;
    g_queue_clear_full (&self->pending, g_object_unref);
    g_hash_table_remove_all (self->targets);
    gchar *uri = g_file_get_uri (file);
    DecodeJob lookup = { .uri = uri, .options = *options };
    DecodeJob *matching = g_hash_table_lookup (self->requests, &lookup);
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (!g_cancellable_is_cancelled (job->cancel)
            && job == matching)
        {
            g_atomic_int_set (&job->demand, TRUE);
            job->generation = self->generation;
            self->foreground_pending = FALSE;
        }
        else if (g_atomic_int_get (&job->demand) || !options_equal (&job->options, options))
            g_cancellable_cancel (job->cancel);
        /* set_candidates() cancels obsolete speculation once the new order is
         * known. Keep a still-useful preload running across demand cache hits. */
    }
    g_free (uri);
    pump (self);
}

void
rstto_image_cache_set_candidates (RsttoImageCache *self, GPtrArray *files)
{
    g_queue_clear_full (&self->pending, g_object_unref);
    g_hash_table_remove_all (self->targets);
    guint capacity = self->limits.max_images > 0 ? self->limits.max_images - 1 : 0;
    for (guint n = 0; n < files->len && self->targets != NULL
                      && g_hash_table_size (self->targets) < capacity;
         n++)
    {
        GFile *file = g_ptr_array_index (files, n);
        gchar *uri = g_file_get_uri (file);
        if ((self->selected != NULL && g_file_equal (self->selected, file))
            || g_hash_table_contains (self->targets, uri) || !g_file_is_native (file))
        {
            g_free (uri);
            continue;
        }
        g_hash_table_insert (self->targets, uri, GUINT_TO_POINTER (g_hash_table_size (self->targets) + 1));
        DecodeJob lookup = { .uri = uri, .options = self->options };
        DecodeJob *running = g_hash_table_lookup (self->requests, &lookup);
        if (running == NULL || g_cancellable_is_cancelled (running->cancel))
            g_queue_push_tail (&self->pending, g_object_ref (file));
    }
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (!is_current (self, job) && !g_hash_table_contains (self->targets, job->uri))
            g_cancellable_cancel (job->cancel);
    }
    pump (self);
}

void
rstto_image_cache_invalidate (RsttoImageCache *self, GFile *file)
{
    gchar *uri = g_file_get_uri (file);
    for (GList *lp = self->lru.head, *next; lp != NULL; lp = next)
    {
        Entry *entry = lp->data;
        next = lp->next;
        if (g_str_equal (entry->key->uri, uri))
            remove_entry (self, entry);
    }
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (g_file_equal (job->file, file))
        {
            if (is_current (self, job))
                self->foreground_pending = TRUE;
            g_cancellable_cancel (job->cancel);
        }
    }
    g_free (uri);
    pump (self);
}

void
rstto_image_cache_clear (RsttoImageCache *self)
{
    self->generation++;
    self->busy = self->foreground_pending = self->speculation_allowed = FALSE;
    cancel_other_jobs (self, NULL);
    g_queue_clear_full (&self->pending, g_object_unref);
    g_hash_table_remove_all (self->targets);
    evict_all (self);
    if (self->displayed != NULL)
    {
        entry_unref (self->displayed);
        self->displayed = NULL;
    }
    g_clear_object (&self->selected);
    g_clear_pointer (&self->mime_type, g_free);
    pump (self);
}

void
rstto_image_cache_stop (RsttoImageCache *self)
{
    self->stopped = TRUE;
    rstto_image_cache_clear (self);
    /* Waiting jobs have no GTask keeping the owner alive. */
    for (GList *lp = self->jobs.head, *next; lp != NULL; lp = next)
    {
        DecodeJob *job = lp->data;
        next = lp->next;
        if (!job->running)
        {
            g_queue_delete_link (&self->jobs, lp);
            job_free (job);
        }
    }
}

gboolean
rstto_image_cache_is_busy (RsttoImageCache *self)
{
    return self->busy;
}

void
rstto_image_cache_get_stats (RsttoImageCache *self, RsttoCacheStats *stats)
{
    *stats = (RsttoCacheStats) { self->resident_slots, self->jobs.length,
                                 self->resident_bytes, reserved_bytes (self), self->decodes, self->hits };
}

GArray *
rstto_image_cache_neighbor_positions (guint length, guint current, gint direction, gboolean initial, guint count, guint capacity, gboolean wrap)
{
    GArray *positions = g_array_new (FALSE, FALSE, sizeof (guint));
    if (length < 2 || current >= length || capacity < 2 || count == 0)
        return positions;
    guint wanted = MIN (length - 1, MIN (capacity - 1, initial ? 4 : count));
    direction = direction < 0 ? -1 : 1;
    guint attempts = initial ? 4 : MIN (count, length);
    for (guint n = 0; positions->len < wanted && n < attempts; n++)
    {
        gint64 offset = initial && n == 2 ? -1 : (gint64) n + 1 - (initial && n > 2);
        gint64 position = current + offset * direction;
        if (wrap)
            position = (position % length + length) % length;
        if (position < 0 || position >= length || position == current)
            continue;
        guint value = position;
        gboolean duplicate = FALSE;
        for (guint j = 0; j < positions->len; j++)
            duplicate |= g_array_index (positions, guint, j) == value;
        if (!duplicate)
            g_array_append_val (positions, value);
    }
    return positions;
}

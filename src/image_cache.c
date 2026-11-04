/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "image_cache.h"
#include "image_probe.h"
#include "profile.h"

#include <gio/gfiledescriptorbased.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

#define MIB ((guint64) 1024 * 1024)
#define PROBE_LIMIT RSTTO_PROBE_LIMIT
#define PROBE_BUFFER_SIZE RSTTO_PROBE_BUFFER_SIZE
#define PROBE_RESERVATION RSTTO_PROBE_RESERVATION
#define READ_BUFFER_SIZE (1024 * 1024)
#define MAX_IMAGE_SIZE 32767
#define SPECULATION_QUIET_US (300 * 1000)
#define DIRECTIONAL_BURST_STEPS 2
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

typedef struct Entry
{
    gint refs;
    RsttoImageCache *owner; /* owner outlives every entry */
    CacheKey *key;
    RsttoImageData image;
    guint64 bytes, source_serial;
    GList *link;
    gboolean demanded;
    gboolean gpu_rejected;
    RsttoRenderKey gpu_rejected_key;
} Entry;

typedef enum
{
    IDENTIFY,
    PROBE,
    PROBE_INFO,
    PROBE_VERIFY,
    DECODE,
    REOPEN_DECODE,
    WAIT_PROBE,
    WAIT_DECODE,
    WAIT_UNCACHED,
    RENDER,
    RENDER_VERIFY,
    GPU_RENDER
} JobPhase;

typedef struct
{
    RsttoImageCache *owner;
    GFile *file;
    gchar *uri, *mime_type;
    RsttoDecodeOptions options;
    GCancellable *cancel;
    GFileInputStream *stream;
    GBytes *probe_prefix;
    gchar **probe_raw_types;
    FileIdentity identity;
    gboolean identified, changed, running, cacheable, rejected;
    gint demand;
    gint interest; /* atomic: 1 useful, 0 obsolete, -1 committed to stopping */
    gboolean queued;
    guint render_tasks, render_next, render_peak;
    gboolean render_failed, gpu_capacity_fallback;
    gint64 render_start;
    RsttoRenderPixels render_source, render_output;
    guint64 generation, serial, reservation, required_bytes;
    guint64 deferral;
    gint64 profile_launch, profile_metadata;
#ifdef RSTTO_CACHE_TEST
    gint64 probe_start;
    gboolean test_fail_stripe;
#endif
    gboolean reserves_slot, foreground_slot;
    JobPhase phase;
    gint width, height, decoded_width, decoded_height;
    RsttoImageData image;
    Entry *render_entry;
    RsttoRenderKey render_key;
    RsttoRenderKey attempted_key;
    RsttoRenderOutcome preparation;
    RsttoRenderReason render_reason;
    RsttoGpuRenderer *render_gpu;
    RsttoGpuRequest *gpu_request;
} DecodeJob;

struct RsttoImageCache
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
    gchar **probe_raw_types;
    RsttoDecodeOptions options;
    RsttoCacheLimits limits;
    guint64 generation, resident_bytes, decodes, hits, renders, render_hits;
    guint64 display_revision;
    guint64 next_job_serial, navigation_revision, request_revision;
    gint64 last_navigation;
    gint navigation_direction;
    guint burst_steps;
#ifdef RSTTO_CACHE_TEST
    guint64 probes, probe_time_us;
    gint64 test_now;
    guint test_render_failure;
#endif
    guint resident_slots, preload_workers, active_tasks, peak_tasks;
    guint warmups, update_depth;
    guint64 warmup_bytes;
    GCancellable *warm_cancel;
    gint64 quiet_until;
    guint quiet_source, urgent_rank;
    gboolean view_pending;
    gchar *pending_target;
    gboolean busy, stopped, pumping, speculation_allowed, foreground_pending, pressure_blocked;
    gint64 pressure_until;
    GMemoryMonitor *memory_monitor;
    RsttoGpuRenderer *gpu;
};

G_DEFINE_TYPE (RsttoImageCache, rstto_image_cache, G_TYPE_OBJECT)

enum
{
    READY,
    IDENTITY_CHANGED,
    PREPARE_RENDER,
    N_SIGNALS
};
static guint signals[N_SIGNALS];
static void
pump (RsttoImageCache *self);


/**
 * schedule_now:
 *
 * Read the scheduler's monotonic clock, allowing deterministic time in cache
 * tests.
 */
static gint64
schedule_now (RsttoImageCache *self)
{
#ifdef RSTTO_CACHE_TEST
    if (self->test_now != 0)
        return self->test_now;
#endif
    return g_get_monotonic_time ();
}


/**
 * quiet_expired:
 *
 * Resume admission when the navigation quiet timer expires and remove the
 * timer source.
 */
static gboolean
quiet_expired (gpointer data)
{
    RsttoImageCache *self = data;
    self->quiet_source = 0;
    pump (self);
    return G_SOURCE_REMOVE;
}


/**
 * schedule_activity:
 *
 * Restart the quiet interval after a meaningful navigation or view change.
 */
static void
schedule_activity (RsttoImageCache *self)
{
    self->quiet_until = schedule_now (self) + SPECULATION_QUIET_US;
    g_clear_handle_id (&self->quiet_source, g_source_remove);
    if (!self->stopped)
        self->quiet_source = g_timeout_add_full (G_PRIORITY_DEFAULT, SPECULATION_QUIET_US / 1000,
                                                 quiet_expired, g_object_ref (self), g_object_unref);
}


/**
 * speculation_due:
 *
 * Check whether the pending selection or a ranked neighbour may start
 * speculative work.
 *
 * Deferral does not change usefulness or cancel a decoder already entered.
 */
static gboolean
speculation_due (RsttoImageCache *self, const gchar *uri)
{
    if (g_strcmp0 (self->pending_target, uri) == 0)
        return TRUE;
    guint rank = GPOINTER_TO_UINT (g_hash_table_lookup (self->targets, uri));
    return !self->view_pending && rank != 0
           && (rank <= self->urgent_rank || self->burst_steps >= DIRECTIONAL_BURST_STEPS
               || schedule_now (self) >= self->quiet_until);
}


/**
 * rstto_image_cache_navigation:
 *
 * Update directional burst tracking and the quiet deadline for a navigation
 * event.
 */
void
rstto_image_cache_navigation (RsttoImageCache *self, RsttoNavigationChange change, gint direction)
{
    gint64 now = schedule_now (self);
    direction = direction < 0 ? -1 : 1;
    self->burst_steps = change == RSTTO_NAV_STEP
                            ? (self->navigation_direction == direction && self->last_navigation != 0
                                       && now >= self->last_navigation && now - self->last_navigation <= SPECULATION_QUIET_US
                                   ? MIN (DIRECTIONAL_BURST_STEPS, self->burst_steps + 1)
                                   : 1)
                            : 0;
    self->navigation_direction = change == RSTTO_NAV_STEP ? direction : 0;
    self->last_navigation = now;
    self->navigation_revision++;
    schedule_activity (self);
    rstto_profile_end (rstto_profile_now (), self, self->navigation_revision, "schedule-input",
                       "kind=%d direction=%d burst=%u quiet_until=%" G_GINT64_FORMAT,
                       change, direction, self->burst_steps, self->quiet_until);
}

static void
launch (DecodeJob *job);
static void
evict_all (RsttoImageCache *self);
static void
dispatch_tasks (RsttoImageCache *self);


/**
 * image_clear:
 *
 * Release every owned image resource, including decoded pixels and rendered
 * derivatives.
 */
static void
image_clear (RsttoImageData *image)
{
    g_clear_pointer (&image->surface, cairo_surface_destroy);
    g_clear_pointer (&image->rendered, cairo_surface_destroy);
    g_clear_pointer (&image->gpu_image, rstto_gpu_image_unref);
    g_clear_object (&image->animation);
    g_clear_error (&image->error);
}


/**
 * options_equal:
 *
 * Compare decode options that affect pixel content, dimensions or cache
 * admission.
 */
static gboolean
options_equal (const RsttoDecodeOptions *a, const RsttoDecodeOptions *b)
{
    return a->limit_quality == b->limit_quality
           && a->width == b->width && a->height == b->height && a->ephemeral == b->ephemeral;
}


/**
 * identity_equal:
 *
 * Compare the complete file identity, including nanosecond modification and
 * change times.
 */
static gboolean
identity_equal (const FileIdentity *a, const FileIdentity *b)
{
    return a->device == b->device && a->inode == b->inode && a->size == b->size
           && a->modified == b->modified && a->changed == b->changed
           && a->modified_nsec == b->modified_nsec && a->changed_nsec == b->changed_nsec;
}


/**
 * identity_from_stat:
 *
 * Extract a trustworthy identity from regular-file metadata, rejecting
 * unsuitable files.
 */
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


/**
 * path_identity:
 *
 * Read the current native path identity for freshness checks; return FALSE
 * when unavailable.
 */
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


/**
 * stream_identity:
 *
 * Read the identity of the job's opened descriptor rather than trusting its
 * pathname.
 */
static gboolean
stream_identity (DecodeJob *job, FileIdentity *identity)
{
    struct stat st;
    return G_IS_FILE_DESCRIPTOR_BASED (job->stream)
           && fstat (g_file_descriptor_based_get_fd (G_FILE_DESCRIPTOR_BASED (job->stream)), &st) == 0
           && identity_from_stat (&st, identity);
}


/**
 * key_hash:
 *
 * Hash a completed entry's URI, selected identity fields and decode dimensions
 * for image lookup.
 */
static guint
key_hash (gconstpointer data)
{
    const CacheKey *key = data;
    return g_str_hash (key->uri) ^ (guint) key->identity.inode
           ^ (guint) key->identity.modified ^ key->identity.modified_nsec
           ^ (guint) key->options.width ^ ((guint) key->options.height << 16);
}


/**
 * key_equal:
 *
 * Require matching URI, file identity and decode options for completed-entry
 * reuse.
 */
static gboolean
key_equal (gconstpointer a, gconstpointer b)
{
    const CacheKey *ka = a, *kb = b;
    return g_str_equal (ka->uri, kb->uri)
           && identity_equal (&ka->identity, &kb->identity)
           && options_equal (&ka->options, &kb->options);
}


/**
 * request_hash:
 *
 * Hash a request's URI and decode dimensions before file identity is known.
 * The worker establishes identity before lookup and again before delivery;
 * coalescing compares all immutable options.
 */
static guint
request_hash (gconstpointer data)
{
    const DecodeJob *job = data;
    return g_str_hash (job->uri) ^ (guint) job->options.width
           ^ ((guint) job->options.height << 16);
}


/**
 * request_equal:
 *
 * Match the URI and immutable decode options of requests eligible to share one
 * job.
 */
static gboolean
request_equal (gconstpointer a, gconstpointer b)
{
    const DecodeJob *ja = a, *jb = b;
    return g_str_equal (ja->uri, jb->uri) && options_equal (&ja->options, &jb->options);
}


/**
 * entry_ref:
 *
 * Retain an entry without changing its LRU position or accounting weight.
 */
static Entry *
entry_ref (Entry *entry)
{
    entry->refs++;
    return entry;
}


/**
 * entry_unref:
 *
 * Release an entry reference and its accounted storage when its last owner
 * lets go.
 */
static void
entry_unref (gpointer data)
{
    Entry *entry = data;
    if (--entry->refs != 0)
        return;
    g_assert (entry->link == NULL);
    entry->owner->resident_bytes -= entry->bytes;
    entry->owner->resident_slots--;
    rstto_gpu_source_invalidate (entry->owner->gpu, entry->source_serial);
    image_clear (&entry->image);
    if (entry->key != NULL)
    {
        g_free (entry->key->uri);
        g_free (entry->key);
    }
    g_free (entry);
}


/**
 * remove_entry:
 *
 * Unlink an entry from the nonowning LRU queue before releasing the lookup
 * table's reference.
 */
static void
remove_entry (RsttoImageCache *self, Entry *entry)
{
    rstto_gpu_source_invalidate (self->gpu, entry->source_serial);
    g_queue_delete_link (&self->lru, entry->link);
    entry->link = NULL;
    g_hash_table_remove (self->entries, entry->key);
}


/**
 * touch_entry:
 *
 * Record foreground use and move the existing queue link to the demand end of
 * the LRU.
 */
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


/**
 * reserved_bytes:
 *
 * Sum live job and shader-warmup reservations, including work still draining
 * after cancellation.
 */
static guint64
reserved_bytes (RsttoImageCache *self)
{
    guint64 bytes = self->warmup_bytes;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        bytes += ((DecodeJob *) lp->data)->reservation;
    return bytes;
}


/**
 * reserved_slots:
 *
 * Count image reservations until their jobs have actually released their
 * resources.
 */
static guint
reserved_slots (RsttoImageCache *self)
{
    guint count = 0;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        count += ((DecodeJob *) lp->data)->reserves_slot;
    return count;
}


/**
 * enabled:
 *
 * Check whether probing support and nonzero configured limits permit caching.
 */
static gboolean
enabled (RsttoImageCache *self)
{
    return rstto_image_probe_available () && self->limits.enabled && self->limits.max_images > 0
           && self->limits.max_bytes > 0 && self->limits.max_image_bytes > 0;
}


/**
 * fits:
 *
 * Check retained storage plus current reservations against the byte and
 * image-slot limits.
 */
static gboolean
fits (RsttoImageCache *self, guint64 bytes, guint slots)
{
    guint64 used = self->resident_bytes + reserved_bytes (self);
    return enabled (self) && used <= self->limits.max_bytes
           && bytes <= self->limits.max_bytes - used
           && (guint64) self->resident_slots + reserved_slots (self) + slots <= self->limits.max_images;
}


/**
 * preload_capacity:
 *
 * Leave two slots for viewing history, even before both are populated, and
 * return the remaining speculative capacity.
 */
static guint
preload_capacity (guint max_images)
{
    return max_images > 2 ? max_images - 2 : 0;
}


/**
 * target_priority:
 *
 * Return the pending selection's or neighbour's scheduling rank without
 * refreshing recency.
 */
static guint
target_priority (RsttoImageCache *self, const gchar *uri)
{
    guint rank = GPOINTER_TO_UINT (g_hash_table_lookup (self->targets, uri));
    /* Missing targets are still speculative, never foreground priority zero. */
    return rank != 0 ? rank : G_MAXUINT;
}


/**
 * speculative_slots:
 *
 * Count retained speculative entries and reserved speculative jobs against
 * their shared capacity.
 */
static guint
speculative_slots (RsttoImageCache *self)
{
    guint count = 0;
    for (GList *lp = self->lru.head; lp != NULL; lp = lp->next)
        count += !((Entry *) lp->data)->demanded;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        count += !g_atomic_int_get (&job->demand)
                 && (job->reserves_slot || (job->render_entry != NULL && job->render_entry->link == NULL && !job->render_entry->demanded));
    }
    return count;
}


/**
 * previous_entry:
 *
 * Find the most recently demand-used entry other than the displayed image for
 * history protection.
 */
static Entry *
previous_entry (RsttoImageCache *self)
{
    /* Demand accesses alone move entries to the head. No extra owning pin is
     * needed; foreground eviction and invalidation can still remove history. */
    for (GList *lp = self->lru.head; lp != NULL; lp = lp->next)
    {
        Entry *entry = lp->data;
        if (entry != self->displayed && entry->demanded)
            return entry;
    }
    return NULL;
}


/**
 * can_evict:
 *
 * Check speculative eviction eligibility while protecting displayed and
 * previous demand history and higher-priority targets.
 */
static gboolean
can_evict (RsttoImageCache *self, Entry *entry, Entry *previous, guint priority)
{
    if (entry == self->displayed || entry->refs != 1)
        return FALSE; /* Removing the lookup must actually release its storage. */
    if (priority == 0)
        return TRUE;
    guint rank = GPOINTER_TO_UINT (g_hash_table_lookup (self->targets, entry->key->uri));
    return entry != previous && (rank == 0 || rank > priority);
}


/**
 * render_weight:
 *
 * Return the retained CPU bytes belonging to an entry's scaled derivative.
 */
static guint64
render_weight (Entry *entry)
{
    cairo_surface_t *surface = entry->image.rendered;
    return surface != NULL ? (guint64) cairo_image_surface_get_stride (surface)
                                 * cairo_image_surface_get_height (surface)
                           : 0;
}


/**
 * can_drop_render:
 *
 * Check whether an entry's rendered derivative is unpinned and can be
 * reclaimed.
 */
static gboolean
can_drop_render (RsttoImageCache *self, Entry *entry)
{
    return entry != self->displayed && entry->refs == 1 && entry->image.rendered != NULL;
}


/**
 * drop_render:
 *
 * Release an entry's CPU derivative and subtract its accounted storage.
 */
static void
drop_render (Entry *entry)
{
    guint64 bytes = render_weight (entry);
    entry->bytes -= bytes;
    entry->owner->resident_bytes -= bytes;
    g_clear_pointer (&entry->image.rendered, cairo_surface_destroy);
}

typedef struct
{
    guint64 resident, reserved, slots, speculative;
} Admission;


/**
 * admission_bytes_fit:
 *
 * Check a proposed byte reservation against simulated retained and reserved
 * storage without overflow.
 */
static gboolean
admission_bytes_fit (RsttoImageCache *self, const Admission *usage, guint64 bytes)
{
    return enabled (self) && usage->resident <= self->limits.max_bytes
           && usage->reserved <= self->limits.max_bytes - usage->resident
           && bytes <= self->limits.max_bytes - usage->resident - usage->reserved;
}


/**
 * admission_fits:
 *
 * Check byte, total-slot and speculative-slot limits for a simulated
 * admission.
 */
static gboolean
admission_fits (RsttoImageCache *self, const Admission *usage, guint64 bytes, guint slots, guint speculative)
{
    /* Subtraction checks avoid overflowing either the byte or slot sum. */
    return admission_bytes_fit (self, usage, bytes)
           && usage->slots <= self->limits.max_images
           && slots <= self->limits.max_images - usage->slots
           && usage->speculative <= preload_capacity (self->limits.max_images)
           && speculative <= preload_capacity (self->limits.max_images) - usage->speculative;
}


/**
 * plan_admission:
 *
 * Simulate eligible evictions before optionally committing them, leaving
 * retained images intact on failed admission.
 */
static gboolean
plan_admission (RsttoImageCache *self, guint64 bytes, guint slots, guint speculative, guint priority, DecodeJob *after_cleanup, gboolean apply)
{
    Admission usage = { self->resident_bytes, self->warmup_bytes, self->resident_slots, speculative_slots (self) };
    Entry *previous = previous_entry (self);
    guint removals = 0, derivatives = 0;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (after_cleanup != NULL && job != after_cleanup && job->running)
        {
            usage.speculative -= job->reserves_slot && !g_atomic_int_get (&job->demand);
            continue;
        }
        if (job->reservation > G_MAXUINT64 - usage.reserved)
            return FALSE;
        usage.reserved += job->reservation;
        usage.slots += job->reserves_slot;
    }
    /* Derivatives can relieve byte pressure, never slot pressure. Plan both
     * passes before touching any storage so failed admission is atomic. */
    for (GList *lp = self->lru.tail;
         !admission_bytes_fit (self, &usage, bytes) && lp != NULL; lp = lp->prev)
    {
        Entry *entry = lp->data;
        if (can_drop_render (self, entry) && can_evict (self, entry, previous, priority))
        {
            usage.resident -= render_weight (entry);
            derivatives++;
        }
    }
    guint discounted = derivatives;
    for (GList *lp = self->lru.tail;
         !admission_fits (self, &usage, bytes, slots, speculative) && lp != NULL; lp = lp->prev)
    {
        Entry *entry = lp->data;
        guint64 dropped = 0;
        if (discounted != 0 && can_drop_render (self, entry) && can_evict (self, entry, previous, priority))
        {
            dropped = render_weight (entry);
            discounted--;
        }
        if (can_evict (self, entry, previous, priority))
        {
            usage.resident -= entry->bytes - dropped;
            usage.slots--;
            usage.speculative -= !entry->demanded;
            removals++;
        }
    }
    if (!admission_fits (self, &usage, bytes, slots, speculative))
        return FALSE;
    if (apply)
    {
        for (GList *lp = self->lru.tail; derivatives != 0; lp = lp->prev)
            if (can_drop_render (self, lp->data) && can_evict (self, lp->data, previous, priority))
            {
                drop_render (lp->data);
                derivatives--;
            }
        for (GList *lp = self->lru.tail, *next; removals != 0; lp = next)
        {
            Entry *entry = lp->data;
            next = lp->prev;
            if (can_evict (self, entry, previous, priority))
            {
                remove_entry (self, entry);
                removals--;
            }
        }
    }
    return TRUE;
}


/**
 * make_room:
 *
 * Commit transactional admission by evicting only entries that the complete
 * plan allows.
 */
static gboolean
make_room (RsttoImageCache *self, guint64 bytes, guint slots, guint speculative, guint priority)
{
    return plan_admission (self, bytes, slots, speculative, priority, NULL, TRUE);
}


/**
 * admit_after_cleanup:
 *
 * Check whether draining jobs can make a reservation admissible without
 * evicting entries yet.
 */
static gboolean
admit_after_cleanup (DecodeJob *job, guint64 bytes, guint slots, guint speculative, guint priority)
{
    RsttoImageCache *self = job->owner;
    gboolean active = FALSE;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *other = lp->data;
        active |= other != job && other->running;
    }
    /* An optimistic lower bound: do not wait if even releasing every active
     * job cannot make room. Recheck actual accounting when a job completes. */
    return active && plan_admission (self, bytes, slots, speculative, priority, job, FALSE);
}


/**
 * trim_to_limits:
 *
 * Reclaim eligible CPU derivatives and lookup entries after live limits
 * change, allowing pinned storage to drain later.
 */
static void
trim_to_limits (RsttoImageCache *self)
{
    for (GList *lp = self->lru.tail; lp != NULL; lp = lp->prev)
    {
        Entry *entry = lp->data;
        Admission usage = { self->resident_bytes, reserved_bytes (self), 0, 0 };
        if (can_drop_render (self, entry)
            && (!admission_bytes_fit (self, &usage, 0) || entry->bytes > self->limits.max_image_bytes))
            drop_render (entry);
    }
    /* Forced limit reductions are not admissions: release what can be freed
     * even if pinned storage/cancelled workers temporarily prevent compliance. */
    for (GList *lp = self->lru.tail, *next; lp != NULL; lp = next)
    {
        Entry *entry = lp->data;
        next = lp->prev;
        if (entry != self->displayed
            && (!fits (self, 0, 0)
                || (!entry->demanded && speculative_slots (self) > preload_capacity (self->limits.max_images))))
            remove_entry (self, entry);
    }
}


/**
 * evict_all:
 *
 * Remove all lookup entries while allowing external pins to retain their
 * accounted storage.
 */
static void
evict_all (RsttoImageCache *self)
{
    while (!g_queue_is_empty (&self->lru))
        remove_entry (self, self->lru.tail->data);
}


/**
 * rgba_bytes:
 *
 * Calculate a positive RGBA pixel footprint with checked arithmetic.
 */
static gboolean
rgba_bytes (gint width, gint height, guint64 *bytes)
{
    if (width <= 0 || height <= 0 || (guint64) width > G_MAXUINT64 / 4 / (guint64) height)
        return FALSE;
    *bytes = (guint64) width * height * 4;
    return TRUE;
}


/**
 * target_size:
 *
 * Choose decoded dimensions from the job's immutable quality settings and the
 * loader dimension limit.
 */
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


/**
 * decode_size:
 *
 * Apply the admitted dimensions to GdkPixbuf and reject unexpected or
 * oversized speculative output.
 */
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

#ifdef RSTTO_CACHE_TEST
static gint pixbuf_loaders_created;
static void (*metadata_started_hook) (DecodeJob *job);
#endif


/**
 * new_loader:
 *
 * Create the MIME-selected GdkPixbuf loader, falling back to generic format
 * detection.
 */
static GdkPixbufLoader *
new_loader (DecodeJob *job)
{
    GdkPixbufLoader *loader = NULL;
#ifdef RSTTO_CACHE_TEST
    g_atomic_int_inc (&pixbuf_loaders_created);
#endif
    if (job->mime_type != NULL)
        loader = gdk_pixbuf_loader_new_with_mime_type (job->mime_type, NULL);
    return loader != NULL ? loader : gdk_pixbuf_loader_new ();
}


/**
 * job_useful:
 *
 * Check hard cancellation and atomically commit to stopping work that is no
 * longer wanted.
 *
 * A target can return until the worker commits to abandoning its result. Hard
 * cancellation remains irreversible and owns resources until cleanup.
 */
static gboolean
job_useful (gpointer data)
{
    DecodeJob *job = data;
    if (g_cancellable_is_cancelled (job->cancel))
        return FALSE;
    if (g_atomic_int_compare_and_exchange (&job->interest, 0, -1))
        return FALSE;
    return g_atomic_int_get (&job->interest) > 0;
}

#ifdef RSTTO_CACHE_TEST
static void (*worker_hook) (DecodeJob *job);
#endif

#ifdef RSTTO_VIEWER_TEST
void
rstto_test_worker_checkpoint (gint phase, gboolean demand, GCancellable *cancel, gboolean entering);
#endif


/**
 * worker:
 *
 * Run one identity, probe-read, decode or verification phase using job-owned
 * resources; report completion only after local cleanup.
 */
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
    gssize count;
    gboolean closed = FALSE;
    gint64 start = rstto_profile_now ();
    rstto_profile_end (job->profile_launch, job, job->serial, "worker-queue", "phase=%d", job->phase);

#ifdef RSTTO_CACHE_TEST
    if (worker_hook != NULL)
        worker_hook (job);
#endif
#ifdef RSTTO_VIEWER_TEST
    rstto_test_worker_checkpoint (job->phase, g_atomic_int_get (&job->demand), cancel, TRUE);
#endif
    if (!job_useful (job))
        goto done;
    if (job->phase == RENDER_VERIFY)
    {
        job->changed = !stream_identity (job, &fd_identity) || !path_identity (job->file, &current)
                       || !identity_equal (&job->identity, &fd_identity)
                       || !identity_equal (&job->identity, &current);
        goto done;
    }
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

    if (job->phase == PROBE)
    {
        job->probe_prefix = rstto_image_probe_read (G_INPUT_STREAM (job->stream), cancel);
        if (job->probe_prefix != NULL
            && rstto_image_probe_is_raw (job->file, job->probe_prefix,
                                         (const gchar *const *) job->probe_raw_types))
            g_clear_pointer (&job->probe_prefix, g_bytes_unref);
        if (!g_seekable_seek (G_SEEKABLE (job->stream), 0, G_SEEK_SET, cancel, NULL))
        {
            job->identified = FALSE;
            g_clear_pointer (&job->probe_prefix, g_bytes_unref);
        }
        goto done;
    }

    if (job->phase == PROBE_VERIFY || job->phase == DECODE)
    {
        job->changed = !stream_identity (job, &fd_identity) || !path_identity (job->file, &current)
                       || !identity_equal (&job->identity, &fd_identity)
                       || !identity_equal (&job->identity, &current);
        if (job->changed)
            job->identified = FALSE;
        if (job->changed || job->phase == PROBE_VERIFY)
            goto done;
    }

    buffer_size = !g_atomic_int_get (&job->demand) ? PROBE_BUFFER_SIZE : READ_BUFFER_SIZE;
    buffer = g_malloc (buffer_size);
    loader = new_loader (job);
    g_signal_connect (loader, "size-prepared", G_CALLBACK (decode_size), job);
    while (job_useful (job))
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
    if (!job_useful (job) || job->rejected)
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
#ifdef RSTTO_VIEWER_TEST
        rstto_test_worker_checkpoint (11, g_atomic_int_get (&job->demand), cancel, TRUE);
#endif
        gint64 conversion = rstto_profile_now ();
        job->image.surface = rstto_image_convert (pixbuf, job_useful, job);
        rstto_profile_end (conversion, job, job->serial, "pixel-conversion", "width=%d height=%d alpha=%d complete=%d",
                           job->image.decoded_width, job->image.decoded_height,
                           gdk_pixbuf_get_has_alpha (pixbuf), job->image.surface != NULL);
#ifdef RSTTO_VIEWER_TEST
        rstto_test_worker_checkpoint (11, g_atomic_int_get (&job->demand), cancel, FALSE);
#endif
        if (job->image.surface == NULL)
            goto done;
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
    rstto_profile_end (start, job, job->serial, "worker", "phase=%d demand=%d cancelled=%d",
                       job->phase, g_atomic_int_get (&job->demand), g_cancellable_is_cancelled (cancel));
#ifdef RSTTO_VIEWER_TEST
    rstto_test_worker_checkpoint (job->phase, g_atomic_int_get (&job->demand), cancel, FALSE);
#endif
    /* Do not return-on-cancel: completion must mean decoder cleanup is done. */
    g_task_return_boolean (task, TRUE);
}


/**
 * job_free:
 *
 * Release all resources held by a completed job, including source pins and GPU
 * admission leases.
 */
static void
job_free (DecodeJob *job)
{
    if (g_hash_table_lookup (job->owner->requests, job) == job)
        g_hash_table_remove (job->owner->requests, job);
    g_clear_object (&job->file);
    g_clear_object (&job->stream);
    g_clear_pointer (&job->probe_prefix, g_bytes_unref);
    g_strfreev (job->probe_raw_types);
    g_clear_object (&job->cancel);
    g_clear_pointer (&job->render_entry, entry_unref);
    g_clear_pointer (&job->gpu_request, rstto_gpu_request_free);
    g_clear_object (&job->render_gpu);
    image_clear (&job->image);
    g_free (job->uri);
    g_free (job->mime_type);
    g_free (job);
}


/**
 * is_current:
 *
 * Check that the cache is running and a noncancelled demand job belongs to its
 * current foreground generation.
 */
static gboolean
is_current (RsttoImageCache *self, DecodeJob *job)
{
    return !self->stopped && g_atomic_int_get (&job->demand)
           && job->generation == self->generation && !g_cancellable_is_cancelled (job->cancel);
}


/**
 * publish:
 *
 * Offer borrowed pixels while retaining both entries. Only an accepted
 * delivery transfers the display pin; clear/stop or a nested accepted delivery
 * invalidates this transaction. A proposed derivative remains job-owned and
 * reserved until the caller commits or releases it after delivery.
 */
static gboolean
publish (RsttoImageCache *self, Entry *entry, RsttoImageDelivery *delivery)
{
    Entry *old = self->displayed != NULL ? entry_ref (self->displayed) : NULL;
    guint64 revision = self->display_revision;
    gboolean accepted = FALSE;
    entry_ref (entry); /* synchronous signal handlers may clear the cache */
    self->busy = FALSE;
    self->foreground_pending = FALSE;
    g_signal_emit (self, signals[READY], 0, delivery, &accepted);
    accepted &= !self->stopped && revision == self->display_revision;
    if (accepted)
    {
        self->display_revision++;
        g_clear_pointer (&self->displayed, entry_unref);
        self->displayed = entry_ref (entry);
        touch_entry (self, entry);
        if (delivery->outcome == RSTTO_RENDER_DIRECT)
        {
            /* Source-only acceptance releases obsolete outputs only after the
             * viewer has dropped them. Rejected deliveries retain both. */
            drop_render (entry);
            g_clear_pointer (&entry->image.gpu_image, rstto_gpu_image_unref);
            entry->image.render_key = (RsttoRenderKey) { 0 };
        }
    }
    /* A rejected refresh must not select its source in place of the image
     * that remains displayed. Do not undo a reentrant clear or replacement. */
    if (!self->stopped && (accepted || revision == self->display_revision))
        rstto_gpu_source_select (self->gpu, self->displayed != NULL && self->displayed->link != NULL
                                                ? self->displayed->source_serial
                                                : 0);
    g_clear_pointer (&old, entry_unref);
    entry_unref (entry);
    return accepted;
}


/**
 * render_result:
 *
 * Record an explicit preparation outcome and log fallback transitions once
 * per job, without conflating refusal with a compatible cache hit.
 */
static RsttoRenderOutcome
render_result (DecodeJob *job, RsttoRenderOutcome outcome, RsttoRenderReason reason)
{
    static const gchar *reasons[] = { "none", "uncached", "native", "image-limit", "budget", "allocation", "render-failed" };
    if (outcome == RSTTO_RENDER_DIRECT && (job->preparation != outcome || job->render_reason != reason))
        rstto_profile_end (rstto_profile_now (), job, job->serial, "render-fallback",
                           "reason=%s width=%d height=%d", reasons[reason], job->attempted_key.width, job->attempted_key.height);
    job->preparation = outcome;
    job->render_reason = reason;
    return outcome;
}


/**
 * publish_job:
 *
 * Deliver preparation metadata separately from cached variant identity.
 * Direct fallback offers only the canonical source and never an old output.
 */
static gboolean
publish_job (DecodeJob *job, Entry *entry, const RsttoImageData *image)
{
    if (job->preparation == RSTTO_RENDER_ABORTED)
        return FALSE;
    RsttoImageDelivery delivery = {
        .image = *image, .outcome = job->preparation, .reason = job->render_reason, .attempted_key = job->attempted_key, .retry_view = enabled (job->owner) && entry->link != NULL && image->animation == NULL
    };
    if (delivery.outcome == RSTTO_RENDER_DIRECT)
    {
        delivery.image.rendered = NULL;
        delivery.image.gpu_image = NULL;
        delivery.image.render_key = (RsttoRenderKey) { 0 };
    }
    return publish (job->owner, entry, &delivery);
}


/**
 * render_key_for:
 *
 * Ask the viewer for an immutable render snapshot and account for a previously
 * rejected GPU variant.
 *
 * Called only on the main thread; the UI supplies a value snapshot.
 */
static gboolean
render_key_for (DecodeJob *job, Entry *entry, RsttoRenderKey *key)
{
    *key = (RsttoRenderKey) { 0 };
    if (entry->image.surface == NULL || entry->image.animation != NULL || entry->key == NULL)
        return FALSE;
    g_signal_emit (job->owner, signals[PREPARE_RENDER], 0, job->file, &entry->image, key);
    rstto_gpu_render_key (job->owner->gpu, entry->image.decoded_width, entry->image.decoded_height, key);
    if (job->gpu_capacity_fallback
        || (entry->gpu_rejected && rstto_image_render_key_equal (key, &entry->gpu_rejected_key)))
    {
        key->backend = key->filter = 0;
        key->context_generation = 0;
    }
    guint64 bytes;
    return rstto_image_render_weight (key, &bytes);
}


/**
 * prepare_render:
 *
 * Reuse a compatible derivative or admit asynchronous GPU/CPU preparation
 * while preserving source and reservation ownership.
 */
static RsttoRenderOutcome
prepare_render (DecodeJob *job, Entry *entry)
{
    RsttoImageCache *self = job->owner;
    RsttoRenderKey key;
    guint64 bytes;
    /* The prepare signal may synchronously clear/stop this cache. */
    if (job->render_entry == NULL)
        job->render_entry = entry_ref (entry);
    job->attempted_key = (RsttoRenderKey) { 0 };
    if (g_atomic_int_get (&job->interest) <= 0 || self->stopped || g_cancellable_is_cancelled (job->cancel))
        return render_result (job, RSTTO_RENDER_ABORTED, RSTTO_RENDER_REASON_NONE);
    if (!enabled (self) || entry->key == NULL || entry->link == NULL || entry->image.animation != NULL)
        return render_result (job, RSTTO_RENDER_DIRECT, RSTTO_RENDER_REASON_UNCACHED);
    gboolean valid = render_key_for (job, entry, &key);
    job->attempted_key = key;
    if (self->stopped || g_cancellable_is_cancelled (job->cancel))
        return render_result (job, RSTTO_RENDER_ABORTED, RSTTO_RENDER_REASON_NONE);
    if (!valid)
        return render_result (job, RSTTO_RENDER_DIRECT, RSTTO_RENDER_REASON_NATIVE);
    if (is_current (self, job))
        rstto_gpu_source_select (self->gpu, entry->source_serial);
    if ((entry->image.rendered != NULL || entry->image.gpu_image != NULL)
        && rstto_image_render_key_equal (&key, &entry->image.render_key))
    {
        self->render_hits++;
        rstto_profile_end (rstto_profile_now (), self, self->generation, "render-hit", "%s", "");
        return render_result (job, RSTTO_RENDER_READY, RSTTO_RENDER_REASON_NONE);
    }
    guint priority = is_current (self, job) ? 0 : target_priority (self, job->uri);
    if (key.backend != 0)
    {
        /* Acquire a pinned source lease and GPU reservation before RAM
         * admission. A hit cannot turn into an unreserved upload later. */
        guint64 staging;
        rstto_image_render_pixels (entry->image.surface, &job->render_source);
        g_clear_pointer (&job->gpu_request, rstto_gpu_request_free);
        job->gpu_request = rstto_gpu_request_new (self->gpu, &job->render_source,
                                                  &key, entry->source_serial, &staging);
        if (job->gpu_request != NULL && make_room (self, staging, 0, 0, priority))
        {
            job->render_key = key;
            job->reservation = staging;
            job->phase = GPU_RENDER;
            g_set_object (&job->render_gpu, self->gpu);
            cairo_surface_flush (entry->image.surface);
            rstto_image_render_pixels (entry->image.surface, &job->render_source);
            self->renders++;
            render_result (job, RSTTO_RENDER_STARTED, RSTTO_RENDER_REASON_NONE);
            launch (job);
            return RSTTO_RENDER_STARTED;
        }
        g_clear_pointer (&job->gpu_request, rstto_gpu_request_free);
        /* Capacity pressure applies only to this request. Verification must
         * compare the admitted CPU variant without poisoning future GPU hits. */
        job->gpu_capacity_fallback = TRUE;
        key.backend = key.filter = 0;
        key.context_generation = 0;
        job->attempted_key = key;
    }
    if (!rstto_image_render_weight (&key, &bytes) || bytes > G_MAXUINT64 / 3
        || entry->bytes - render_weight (entry) > self->limits.max_image_bytes
        || bytes > self->limits.max_image_bytes - (entry->bytes - render_weight (entry)))
        return render_result (job, RSTTO_RENDER_DIRECT, RSTTO_RENDER_REASON_IMAGE_LIMIT);
    /* The pinned source is already charged and shares this image's slot. */
    if (!make_room (self, 3 * bytes, 0, 0, priority))
        return render_result (job, RSTTO_RENDER_DIRECT, RSTTO_RENDER_REASON_BUDGET);
    job->cacheable = TRUE;
    job->render_key = key;
    job->reservation = 3 * bytes;
    job->phase = RENDER;
    cairo_surface_flush (entry->image.surface);
    self->renders++;
    render_result (job, RSTTO_RENDER_STARTED, RSTTO_RENDER_REASON_NONE);
    launch (job);
    pump (self);
    return RSTTO_RENDER_STARTED;
}


/**
 * entry_from_job:
 *
 * Transfer completed decoded pixels into a new accounted entry with one
 * reference and a unique source serial; lookup insertion belongs to the
 * caller.
 */
static Entry *
entry_from_job (RsttoImageCache *self, DecodeJob *job)
{
    static guint64 source_serial; /* main thread, distinct across caches */
    Entry *entry = g_new0 (Entry, 1);
    entry->source_serial = ++source_serial;
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


/**
 * cancel_job:
 *
 * Mark a job irreversibly cancelled and keep running resources reserved until
 * cleanup completes.
 */
static void
cancel_job (DecodeJob *job, const gchar *reason)
{
    if (!g_cancellable_is_cancelled (job->cancel))
    {
        rstto_profile_end (rstto_profile_now (), job, job->serial, "work-cancelled",
                           "phase=%d reason=%s", job->phase, reason);
        g_cancellable_cancel (job->cancel);
    }
}


/**
 * cancel_other_jobs:
 *
 * Hard-cancel every job except the explicitly preserved one; running jobs keep
 * their reservations until cleanup.
 */
static void
cancel_other_jobs (RsttoImageCache *self, DecodeJob *except)
{
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        if (lp->data != except)
            cancel_job (lp->data, G_STRFUNC);
}


/**
 * uncached:
 *
 * Start the normal foreground decode exception when the image cannot be safely
 * cached.
 */
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


/**
 * finish_job:
 *
 * Remove a fully drained job and release its image, render and reservation
 * ownership.
 */
static void
finish_job (DecodeJob *job)
{
    RsttoImageCache *self = job->owner;
    if (!self->stopped && !g_cancellable_is_cancelled (job->cancel)
        && g_atomic_int_get (&job->interest) < 0 && target_priority (self, job->uri) != G_MAXUINT)
    {
        gboolean pending = FALSE;
        for (GList *lp = self->pending.head; lp != NULL; lp = lp->next)
            pending |= g_file_equal (lp->data, job->file);
        if (!pending)
            g_queue_push_head (&self->pending, g_object_ref (job->file));
    }
    g_queue_remove (&self->jobs, job);
    job_free (job);
    pump (self);
}


/**
 * wait_or_uncached:
 *
 * Wait for reclaimable reservations to drain before choosing the uncached
 * foreground fallback.
 */
static void
wait_or_uncached (DecodeJob *job)
{
    RsttoImageCache *self = job->owner;
    if (enabled (self) && job->identified && !job->options.ephemeral && (self->jobs.length > 1 || self->warmups != 0))
    {
        /* A cancelled decoder still owns its reservation. Retry admission only
         * after cleanup instead of unnecessarily making this image uncached. */
        cancel_other_jobs (self, job);
        g_cancellable_cancel (self->warm_cancel);
        job->running = FALSE;
        job->phase = WAIT_PROBE;
    }
    else
        uncached (job);
    pump (self);
}


/**
 * probe_metadata_done:
 *
 * Finish the asynchronous metadata probe and advance to identity verification
 * without releasing the job early.
 */
static void
probe_metadata_done (GObject *source, GAsyncResult *result, gpointer data)
{
    DecodeJob *job = data;
    RsttoImageCache *self = job->owner;
    gint width, height;
    if (rstto_image_probe_finish (result, &width, &height))
        target_size (job, width, height);
    rstto_profile_end (job->profile_metadata, job, job->serial, "glycin-metadata", "width=%d height=%d", job->width, job->height);
    /* Keep the reservation/slot through the metadata callback and the final
     * descriptor/path check. Cancellation never completes a live job early. */
    if (self->stopped || g_cancellable_is_cancelled (job->cancel) || !job_useful (job))
        finish_job (job);
    else
    {
        job->phase = PROBE_VERIFY;
        launch (job);
    }
    g_object_unref (self);
}


/**
 * job_done:
 *
 * Process a completed worker phase on the main thread, rechecking freshness,
 * demand and admission before advancing or publishing.
 */
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
    if (g_atomic_int_get (&job->interest) < 0)
    {
        rstto_profile_end (rstto_profile_now (), job, job->serial, "work-discarded", "phase=%d reason=obsolete", job->phase);
        finish_job (job);
        return;
    }
    current = is_current (self, job);
    priority = current ? 0 : target_priority (self, job->uri);
    if (job->phase == RENDER_VERIFY)
    {
        entry = job->render_entry;
        if (job->changed)
        {
            if (entry->link != NULL)
                remove_entry (self, entry);
            g_clear_pointer (&job->image.rendered, cairo_surface_destroy);
            g_clear_pointer (&job->image.gpu_image, rstto_gpu_image_unref);
            job->reservation = 0;
            if (current)
            {
                g_set_error_literal (&job->image.error, G_IO_ERROR, G_IO_ERROR_BUSY,
                                     "Image changed while it was being rendered; refresh to retry");
                g_signal_emit (self, signals[IDENTITY_CHANGED], 0, job->file);
                Entry *error = entry_from_job (self, job);
                render_result (job, RSTTO_RENDER_DIRECT, RSTTO_RENDER_REASON_UNCACHED);
                publish_job (job, error, &error->image);
                entry_unref (error);
            }
        }
        else
        {
            RsttoRenderKey latest = { 0 };
            RsttoRenderCompatibility compatibility = render_key_for (job, entry, &latest)
                                                         ? rstto_image_render_compatibility (&latest, job->preparation == RSTTO_RENDER_DIRECT
                                                                                                          ? &job->attempted_key
                                                                                                          : &job->render_key)
                                                         : RSTTO_RENDER_INCOMPATIBLE;
            /* A verified full pan frame can advance the already displayed
             * image while input continues. The pinned entry is the same source
             * and identity; never relax background/cache-hit comparisons. */
            gboolean pan_frame = job->preparation == RSTTO_RENDER_STARTED
                                 && compatibility == RSTTO_RENDER_PAN_ONLY && current && self->displayed == entry
                                 && job->render_key.view_revision >= entry->image.render_key.view_revision;
            if (self->stopped || g_cancellable_is_cancelled (job->cancel))
            {
                finish_job (job);
                return;
            }
            if (compatibility != RSTTO_RENDER_EXACT && !pan_frame)
            {
                rstto_profile_end (rstto_profile_now (), job, job->serial, "render-obsolete",
                                   "revision=%" G_GUINT64_FORMAT " latest=%" G_GUINT64_FORMAT,
                                   job->render_key.view_revision, latest.view_revision);
                g_clear_pointer (&job->image.rendered, cairo_surface_destroy);
                g_clear_pointer (&job->image.gpu_image, rstto_gpu_image_unref);
                job->reservation = 0;
                if (prepare_render (job, entry) == RSTTO_RENDER_STARTED)
                    return;
            }
            if (job->preparation == RSTTO_RENDER_ABORTED)
            {
                finish_job (job);
                return;
            }
            /* Identical pixels after a reversal satisfy the newest revision.
             * A pan preview retains its actual submission snapshot instead. */
            if (compatibility == RSTTO_RENDER_EXACT)
                job->render_key.view_revision = latest.view_revision;
            if (pan_frame)
                rstto_profile_end (rstto_profile_now (), job, job->serial, "render-pan-frame",
                                   "revision=%" G_GUINT64_FORMAT " latest=%" G_GUINT64_FORMAT,
                                   job->render_key.view_revision, latest.view_revision);
            /* Cleanup has finished. Replace the full render reservation with
             * the proposed output's actual weight without yielding. The old
             * derivative stays in the entry until the viewer accepts delivery. */
            job->reservation = 0;
            gboolean completed_output = job->image.rendered != NULL || job->image.gpu_image != NULL;
            RsttoImageData proposed = entry->image;
            guint64 bytes = 0;
            gboolean replace = FALSE;
            if (job->image.gpu_image != NULL && enabled (self)
                && entry->bytes - render_weight (entry) <= self->limits.max_image_bytes && fits (self, 0, 0))
            {
                proposed.rendered = NULL;
                proposed.gpu_image = job->image.gpu_image;
                proposed.render_key = job->render_key;
                replace = TRUE;
            }
            if (job->image.rendered != NULL)
            {
                guint64 weight = (guint64) cairo_image_surface_get_stride (job->image.rendered)
                                 * cairo_image_surface_get_height (job->image.rendered);
                if (enabled (self) && weight <= self->limits.max_image_bytes
                    && entry->bytes - render_weight (entry) <= self->limits.max_image_bytes - weight
                    && fits (self, weight, 0))
                {
                    proposed.rendered = job->image.rendered;
                    proposed.gpu_image = NULL;
                    proposed.render_key = job->render_key;
                    bytes = weight;
                    replace = TRUE;
                }
            }
            if (replace)
            {
                job->attempted_key = proposed.render_key;
                render_result (job, RSTTO_RENDER_READY, RSTTO_RENDER_REASON_NONE);
            }
            else if (completed_output)
            {
                /* A live reduction can refuse an output after its work was
                 * admitted. Offer direct drawing, never an incompatible hit. */
                guint64 weight = job->image.rendered != NULL
                                     ? (guint64) cairo_image_surface_get_stride (job->image.rendered)
                                           * cairo_image_surface_get_height (job->image.rendered)
                                     : 0;
                guint64 source_weight = entry->bytes - render_weight (entry);
                RsttoRenderReason reason = RSTTO_RENDER_REASON_BUDGET;
                if (!enabled (self) || entry->link == NULL)
                    reason = RSTTO_RENDER_REASON_UNCACHED;
                else if (source_weight > self->limits.max_image_bytes
                         || weight > self->limits.max_image_bytes - source_weight)
                    reason = RSTTO_RENDER_REASON_IMAGE_LIMIT;
                render_result (job, RSTTO_RENDER_DIRECT, reason);
            }
            else if (job->preparation == RSTTO_RENDER_STARTED)
                render_result (job, RSTTO_RENDER_DIRECT, RSTTO_RENDER_REASON_FAILED);
            if (proposed.rendered != job->image.rendered)
                g_clear_pointer (&job->image.rendered, cairo_surface_destroy);
            if (proposed.gpu_image != job->image.gpu_image)
                g_clear_pointer (&job->image.gpu_image, rstto_gpu_image_unref);
            job->reservation = bytes;
            /* Background work may update a cached entry, but never replace a
             * derivative still displayed without its consumer's acceptance. */
            gboolean accepted = entry != self->displayed;
            if (is_current (self, job))
            {
                self->speculation_allowed = entry->link != NULL;
                accepted = publish_job (job, entry, &proposed);
            }
            if (replace && accepted)
            {
                guint64 old_bytes = render_weight (entry);
                g_clear_pointer (&entry->image.rendered, cairo_surface_destroy);
                g_clear_pointer (&entry->image.gpu_image, rstto_gpu_image_unref);
                entry->image.rendered = g_steal_pointer (&job->image.rendered);
                entry->image.gpu_image = g_steal_pointer (&job->image.gpu_image);
                entry->image.render_key = proposed.render_key;
                entry->bytes = entry->bytes - old_bytes + bytes;
                self->resident_bytes = self->resident_bytes - old_bytes + bytes;
            }
            g_clear_pointer (&job->image.rendered, cairo_surface_destroy);
            g_clear_pointer (&job->image.gpu_image, rstto_gpu_image_unref);
            job->reservation = 0;
        }
        finish_job (job);
        return;
    }
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
                    rstto_profile_end (rstto_profile_now (), self, self->generation, "cache-hit", "job=%p", (void *) job);
                    self->speculation_allowed = TRUE;
                    entry->demanded = TRUE;
                    if (prepare_render (job, entry) == RSTTO_RENDER_STARTED)
                        return;
                    if (is_current (self, job))
                        publish_job (job, entry, &entry->image);
                }
                if (!current && prepare_render (job, entry) == RSTTO_RENDER_STARTED)
                    return;
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
            if (make_room (self, PROBE_RESERVATION, 1, priority != 0, priority))
            {
                job->reservation = PROBE_RESERVATION;
                job->reserves_slot = TRUE;
                job->phase = PROBE;
                launch (job);
                return;
            }
        }
        if (current)
            wait_or_uncached (job);
        else if (enabled (self) && job->identified && !job->options.ephemeral
                 && admit_after_cleanup (job, PROBE_RESERVATION, 1, 1, priority))
        {
            job->running = FALSE;
            job->phase = WAIT_PROBE;
            pump (self);
        }
        else
            finish_job (job);
        return;
    }
    if (job->phase == PROBE)
    {
        if (job->probe_prefix != NULL)
        {
            job->phase = PROBE_INFO;
            job->profile_metadata = rstto_profile_now ();
            g_object_ref (self);
            rstto_image_probe_async (job->probe_prefix, job->cancel, probe_metadata_done, job);
            g_clear_pointer (&job->probe_prefix, g_bytes_unref);
#ifdef RSTTO_CACHE_TEST
            if (metadata_started_hook != NULL)
                metadata_started_hook (job);
#endif
            return;
        }
        job->phase = PROBE_VERIFY;
    }
    if (job->phase == PROBE_VERIFY)
    {
#ifdef RSTTO_CACHE_TEST
        self->probes++;
        self->probe_time_us += g_get_monotonic_time () - job->probe_start;
#endif
        job->reservation = 0;
        if (job->identified && rgba_bytes (job->width, job->height, &source_bytes)
            && rgba_bytes (job->decoded_width, job->decoded_height, &output_bytes)
            && source_bytes <= self->limits.max_image_bytes
            && output_bytes <= self->limits.max_image_bytes
            && output_bytes <= (G_MAXUINT64 - source_bytes - READ_BUFFER_SIZE) / 3)
        {
            reservation = source_bytes + 3 * output_bytes + READ_BUFFER_SIZE;
            if (!current)
            {
                /* Keep the probed dimensions and slot while other decoders
                 * release their transient reservations. No decoder is live. */
                job->required_bytes = reservation;
                job->phase = WAIT_DECODE;
                job->running = FALSE;
                pump (self);
                return;
            }
            if (make_room (self, reservation, 0, 0, priority))
            {
                job->cacheable = TRUE;
                job->reservation = reservation;
                job->reserves_slot = TRUE;
                job->phase = DECODE;
                self->decodes++;
                self->speculation_allowed = TRUE;
                launch (job);
                pump (self);
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
        && make_room (self, 0, 0, !current, priority))
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
        entry->demanded = current;
    }
    if (prepare_render (job, entry) == RSTTO_RENDER_STARTED)
    {
        entry_unref (entry);
        return;
    }
    if (is_current (self, job))
    {
        self->speculation_allowed = entry->link != NULL;
        if (!self->speculation_allowed)
            evict_all (self);
        publish_job (job, entry, &entry->image);
    }
    entry_unref (entry);
    finish_job (job);
}

typedef struct
{
    DecodeJob *job;
    guint row, rows;
    gboolean ok;
} RenderStripe;


/**
 * stripe_worker:
 *
 * Render one admitted CPU row stripe using independent Cairo wrappers over
 * shared pixel allocations.
 */
static void
stripe_worker (GTask *task, gpointer source, gpointer data, GCancellable *cancel)
{
    RenderStripe *stripe = data;
    DecodeJob *job = stripe->job;
#ifdef RSTTO_CACHE_TEST
    if (worker_hook != NULL)
        worker_hook (job);
#endif
#ifdef RSTTO_VIEWER_TEST
    rstto_test_worker_checkpoint (RENDER, g_atomic_int_get (&job->demand), cancel, TRUE);
#endif
#ifdef RSTTO_CACHE_TEST
    if (!job->test_fail_stripe)
#endif
    stripe->ok = job_useful (job) && job->render_output.data != NULL && rstto_image_render_stripe (&job->render_source, &job->render_output, &job->render_key, stripe->row, stripe->rows) && job_useful (job);
#ifdef RSTTO_VIEWER_TEST
    rstto_test_worker_checkpoint (RENDER, g_atomic_int_get (&job->demand), cancel, FALSE);
#endif
    g_task_return_boolean (task, stripe->ok);
}


/**
 * task_done:
 *
 * Release a CPU stripe permit and advance only after all stripes have finished
 * and their temporary resources are gone.
 */
static void
task_done (GObject *source, GAsyncResult *result, gpointer data)
{
    RsttoImageCache *self = RSTTO_IMAGE_CACHE (source);
    DecodeJob *job = data;
    g_assert (self->active_tasks > 0);
    self->active_tasks--;
    if (job->phase == RENDER)
    {
        RenderStripe *stripe = g_task_get_task_data (G_TASK (result));
        job->render_failed |= !stripe->ok;
        job->render_tasks--;
        job->running = job->render_tasks != 0;
        if (job->render_tasks != 0 || (!job->render_failed && !g_cancellable_is_cancelled (job->cancel) && g_atomic_int_get (&job->interest) > 0 && job->render_next < (guint) job->render_key.height))
        {
            pump (self);
            return;
        }
        job->queued = FALSE;
        job->running = TRUE; /* protect through synchronous completion signals */
        rstto_profile_end (job->render_start, job, job->serial, "render-prepare",
                           "width=%d height=%d parallel=%u cancelled=%d demand=%d", job->render_key.width, job->render_key.height,
                           job->render_peak, job->render_failed || g_cancellable_is_cancelled (job->cancel) || g_atomic_int_get (&job->interest) <= 0, g_atomic_int_get (&job->demand));
        if (job->render_failed || g_cancellable_is_cancelled (job->cancel) || g_atomic_int_get (&job->interest) <= 0)
            g_clear_pointer (&job->image.rendered, cairo_surface_destroy);
        else
        {
            cairo_surface_mark_dirty (job->image.rendered);
            cairo_surface_set_device_scale (job->image.rendered, job->render_key.pixel_scale, job->render_key.pixel_scale);
        }
        if (self->stopped || g_cancellable_is_cancelled (job->cancel) || g_atomic_int_get (&job->interest) <= 0)
            render_result (job, RSTTO_RENDER_ABORTED, RSTTO_RENDER_REASON_NONE);
        else if (job->render_failed)
            render_result (job, RSTTO_RENDER_DIRECT, RSTTO_RENDER_REASON_FAILED);
        if (!self->stopped && job_useful (job))
        {
            /* Revalidation is I/O too: keep it off the GTK thread and retain
             * the full batch reservation until this last worker cleans up. */
            job->phase = RENDER_VERIFY;
            launch (job);
            return;
        }
    }
    job_done (source, result, job);
    pump (self);
}


/**
 * job_schedule_state:
 *
 * Profile a job's scheduling transition once, suppressing repeated reports of
 * an unchanged deferral.
 */
static void
job_schedule_state (DecodeJob *job, guint reason)
{
    static const gchar *names[] = { "admitted", "foreground", "quiet", "budget-cleanup", "gpu-worker", "view", "task" };
    guint priority = is_current (job->owner, job) ? 0 : target_priority (job->owner, job->uri);
    guint64 state = 1 + reason + ((guint64) job->phase << 8) + ((guint64) priority << 16);
    if (state == job->deferral)
        return;
    job->deferral = state;
    rstto_profile_end (rstto_profile_now (), job, job->serial, "schedule-job",
                       "phase=%d priority=%u state=%s burst=%u", job->phase, priority, names[reason], job->owner->burst_steps);
}


/**
 * launch:
 *
 * Queue the next job phase and allocate the already-reserved shared output for
 * CPU rendering.
 */
static void
launch (DecodeJob *job)
{
    job_schedule_state (job, 0);
    job->profile_launch = rstto_profile_now ();
#ifdef RSTTO_CACHE_TEST
    if (job->phase == PROBE)
        job->probe_start = g_get_monotonic_time ();
#endif
    job->running = FALSE;
    job->queued = TRUE;
    if (job->phase == RENDER)
    {
        job->render_start = rstto_profile_now ();
        job->render_next = job->render_tasks = job->render_peak = 0;
        job->render_failed = FALSE;
        cairo_format_t format = CAIRO_FORMAT_ARGB32;
#ifdef RSTTO_CACHE_TEST
        if (job->owner->test_render_failure == 1)
            format = CAIRO_FORMAT_INVALID;
        job->test_fail_stripe = job->owner->test_render_failure == 2;
        job->owner->test_render_failure = 0;
#endif
        job->image.rendered = cairo_image_surface_create (format, job->render_key.width, job->render_key.height);
        if (cairo_surface_status (job->image.rendered) != CAIRO_STATUS_SUCCESS)
        {
            g_clear_pointer (&job->image.rendered, cairo_surface_destroy);
            render_result (job, RSTTO_RENDER_DIRECT, RSTTO_RENDER_REASON_ALLOCATION);
            job->phase = RENDER_VERIFY;
        }
        else
        {
            rstto_image_render_pixels (job->render_entry->image.surface, &job->render_source);
            rstto_image_render_pixels (job->image.rendered, &job->render_output);
        }
    }
    pump (job->owner);
}


/**
 * gpu_done:
 *
 * Release a completed GPU task permit, verify successful results or schedule
 * admitted CPU fallback after failure.
 */
static void
gpu_done (GObject *source, GAsyncResult *result, gpointer data)
{
    DecodeJob *job = data;
    RsttoImageCache *self = job->owner;
    job->image.gpu_image = rstto_gpu_render_finish (RSTTO_GPU_RENDERER (source), result, NULL);
    rstto_profile_end (job->profile_launch, job, job->serial, "gpu-prepare",
                       "success=%d demand=%d", job->image.gpu_image != NULL, g_atomic_int_get (&job->demand));
    self->active_tasks--;
    job->running = TRUE; /* protect through prepare-render signals */
    if (self->stopped || !job_useful (job))
        finish_job (job);
    else
    {
        if (job->image.gpu_image == NULL)
        {
            rstto_gpu_source_invalidate (job->render_gpu, job->render_entry->source_serial);
            job->render_entry->gpu_rejected = TRUE;
            job->render_entry->gpu_rejected_key = job->render_key;
            job->reservation = 0; /* GPU completion includes staging cleanup */
            rstto_profile_end (rstto_profile_now (), job, job->serial, "gpu-fallback", "reason=render-failed");
            if (prepare_render (job, job->render_entry) == RSTTO_RENDER_STARTED)
            {
                g_object_unref (self);
                return;
            }
        }
        job->phase = RENDER_VERIFY;
        launch (job);
    }
    pump (self);
    g_object_unref (self);
}


/**
 * warmup_done:
 *
 * Release warmup resources and reservations, then resume scheduling on the
 * main thread.
 */
static void
warmup_done (GObject *source, GAsyncResult *result, gpointer data)
{
    RsttoImageCache *self = data;
    rstto_gpu_warmup_finish (RSTTO_GPU_RENDERER (source), result, NULL);
    self->active_tasks--;
    self->warmups--;
    self->warmup_bytes = 0;
    if (!self->stopped && !self->pressure_blocked && enabled (self)
        && g_cancellable_is_cancelled (self->warm_cancel) && fits (self, RSTTO_GPU_WARMUP_BYTES, 0))
    {
        g_clear_object (&self->warm_cancel);
        self->warm_cancel = g_cancellable_new ();
    }
    pump (self);
    g_object_unref (self);
}


/**
 * dispatch_tasks:
 *
 * Assign shared worker permits to foreground work, pending selection and
 * ranked neighbours, with warmup using spare capacity.
 */
static void
dispatch_tasks (RsttoImageCache *self)
{
    guint ceiling = 1 + MIN (self->preload_workers, preload_capacity (self->limits.max_images));
    while (self->active_tasks < ceiling)
    {
        DecodeJob *best = NULL;
        guint best_rank = G_MAXUINT;
        for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        {
            DecodeJob *job = lp->data;
            if (!job->queued || g_cancellable_is_cancelled (job->cancel) || g_atomic_int_get (&job->interest) <= 0)
                continue;
            if (job->phase == GPU_RENDER && !rstto_gpu_request_available (job->gpu_request))
            {
                job_schedule_state (job, 4);
                continue;
            }
            gboolean current = is_current (self, job);
            if (!current && job->phase == GPU_RENDER && self->view_pending
                && g_strcmp0 (self->pending_target, job->uri) != 0)
            {
                job_schedule_state (job, 5);
                continue;
            }
            if (job->phase == RENDER && (job->render_next >= (guint) job->render_key.height || job->render_failed || (!current && job->render_tasks != 0)))
                continue;
            guint rank = current ? 0 : target_priority (self, job->uri);
            if (best == NULL || rank < best_rank)
            {
                best = job;
                best_rank = rank;
            }
        }
        if (best == NULL)
            break;
        job_schedule_state (best, 0);
        if (best->phase == GPU_RENDER)
        {
            best->queued = FALSE;
            best->running = TRUE;
            self->active_tasks++;
            self->peak_tasks = MAX (self->peak_tasks, self->active_tasks);
            g_object_ref (self); /* paired with gpu_done after all resource cleanup */
            rstto_gpu_render_request_async (g_steal_pointer (&best->gpu_request),
                                            best->cancel, job_useful, best, &best->demand, gpu_done, best);
            continue;
        }
        GTask *task = g_task_new (self, best->cancel, task_done, best);
        g_task_set_return_on_cancel (task, FALSE);
        g_task_set_priority (task, best_rank == 0 ? G_PRIORITY_DEFAULT : G_PRIORITY_LOW);
        best->running = TRUE;
        self->active_tasks++;
        self->peak_tasks = MAX (self->peak_tasks, self->active_tasks);
        if (best->phase == RENDER)
        {
            RenderStripe *stripe = g_new0 (RenderStripe, 1);
            stripe->job = best;
            stripe->row = best->render_next;
            /* Pixman's nearest sampling rounds relative to clip extents. Keep
             * its fast whole-image path to preserve exact legacy pixels. */
            stripe->rows = best->render_key.smoothing
                               ? MIN (RSTTO_RENDER_STRIPE_ROWS, best->render_key.height - best->render_next)
                               : (guint) best->render_key.height;
            best->render_next += stripe->rows;
            best->render_tasks++;
            best->render_peak = MAX (best->render_peak, best->render_tasks);
            g_task_set_task_data (task, stripe, g_free);
            g_task_run_in_thread (task, stripe_worker);
        }
        else
        {
            best->queued = FALSE;
            g_task_set_task_data (task, best, NULL);
            g_task_run_in_thread (task, worker);
        }
        g_object_unref (task);
    }
    /* Warmup has no image slot, but owns a task permit and host reservation.
     * Start the first context before others and leave room for decoding. */
    if (!self->stopped && !self->pressure_blocked && self->warmups == 0
        && !g_cancellable_is_cancelled (self->warm_cancel)
        && self->active_tasks < ceiling && fits (self, RSTTO_GPU_WARMUP_BYTES, 0)
        && rstto_gpu_renderer_needs_warmup (self->gpu)
        && (!rstto_gpu_renderer_available (self->gpu) || self->active_tasks + 1 < ceiling))
    {
        gboolean persistent = rstto_gpu_shader_cache_enabled ()
                              && fits (self, RSTTO_GPU_WARMUP_BYTES + RSTTO_SHADER_HOST_BYTES, 0);
        self->warmup_bytes = RSTTO_GPU_WARMUP_BYTES + (persistent ? RSTTO_SHADER_HOST_BYTES : 0);
        self->warmups++;
        self->active_tasks++;
        self->peak_tasks = MAX (self->peak_tasks, self->active_tasks);
        rstto_gpu_warmup_async (self->gpu, persistent, self->warm_cancel, warmup_done, g_object_ref (self));
    }
}


/**
 * new_job:
 *
 * Create a job with owned file, cancellation and immutable option state, then
 * register it for request coalescing.
 */
static DecodeJob *
new_job (RsttoImageCache *self, GFile *file, gboolean demand)
{
    DecodeJob *job = g_new0 (DecodeJob, 1);
    job->owner = self;
    job->serial = ++self->next_job_serial;
    job->file = g_object_ref (file);
    job->uri = g_file_get_uri (file);
    job->options = self->options;
    job->generation = self->generation;
    job->demand = demand;
    job->interest = 1;
    job->foreground_slot = demand;
    job->phase = IDENTIFY;
    job->preparation = RSTTO_RENDER_ABORTED;
    job->cancel = g_cancellable_new ();
    guint configured = g_strv_length (self->probe_raw_types);
    guint excluded = g_strv_length (self->excluded_mime_types);
    job->probe_raw_types = g_new0 (gchar *, configured + excluded + 1);
    for (guint i = 0; i < configured; i++)
        job->probe_raw_types[i] = g_strdup (self->probe_raw_types[i]);
    for (guint i = 0; i < excluded; i++)
        job->probe_raw_types[configured + i] = g_strdup (self->excluded_mime_types[i]);
    if (demand && self->mime_type != NULL
        && !g_strv_contains ((const gchar *const *) self->excluded_mime_types, self->mime_type))
        job->mime_type = g_strdup (self->mime_type);
    g_queue_push_tail (&self->jobs, job);
    g_hash_table_replace (self->requests, job, job);
    return job;
}


/**
 * speculative_jobs:
 *
 * Count speculative jobs that still occupy loading capacity, including
 * cancelled work awaiting cleanup.
 *
 * A promoted preload retains its original worker slot until cleanup.
 */
static guint
speculative_jobs (RsttoImageCache *self)
{
    guint count = 0;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        count += !((DecodeJob *) lp->data)->foreground_slot;
    return count;
}


/**
 * foreground_job:
 *
 * Check whether a live job occupies foreground demand or matches the selected
 * file, preventing a second foreground request.
 */
static gboolean
foreground_job (RsttoImageCache *self)
{
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (job->foreground_slot || is_current (self, job)
            || (self->selected != NULL && g_file_equal (self->selected, job->file)
                && options_equal (&self->options, &job->options)))
            return TRUE;
    }
    return FALSE;
}


/**
 * preparing_preload:
 *
 * Check for a useful speculative job of equal or higher priority still
 * preparing its decode reservation.
 */
static gboolean
preparing_preload (RsttoImageCache *self, guint priority)
{
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (!job->foreground_slot && !g_cancellable_is_cancelled (job->cancel)
            && g_atomic_int_get (&job->interest) > 0 && target_priority (self, job->uri) <= priority
            && (job->phase == IDENTIFY || job->phase == PROBE || job->phase == PROBE_INFO
                || job->phase == PROBE_VERIFY || ((job->phase == WAIT_PROBE || job->phase == WAIT_DECODE) && speculation_due (self, job->uri))))
            return TRUE;
    }
    return FALSE;
}


/**
 * demand_admitted:
 *
 * Check whether foreground demand has reached an admitted phase before
 * starting additional speculative work.
 */
static gboolean
demand_admitted (RsttoImageCache *self)
{
    if (self->foreground_pending)
        return FALSE;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (is_current (self, job))
            return job->phase == DECODE || job->phase == RENDER || job->phase == RENDER_VERIFY || job->phase == GPU_RENDER;
        if (job->foreground_slot)
            return FALSE; /* cancelled demand still draining */
    }
    return !self->busy;
}


/**
 * preload_worker_limit:
 *
 * Combine the configured preload workers with speculative image capacity to
 * obtain the effective job limit.
 */
static guint
preload_worker_limit (RsttoImageCache *self)
{
    return MIN (self->preload_workers, preload_capacity (self->limits.max_images));
}


/**
 * pump:
 *
 * Reconcile foreground and neighbour requests, enforce admission limits and
 * dispatch eligible work without recursive scheduling.
 */
static void
pump (RsttoImageCache *self)
{
    if (self->stopped || self->pumping || self->update_depth != 0)
        return;
    self->pumping = TRUE;
    for (GList *lp = self->jobs.head, *next; lp != NULL; lp = next)
    {
        DecodeJob *job = lp->data;
        next = lp->next;
        if (!job->running && (g_cancellable_is_cancelled (job->cancel) || !job_useful (job)))
        {
            finish_job (job);
            continue;
        }
        if (job->queued)
            continue;
        if (!job->running && job->phase == WAIT_PROBE && !is_current (self, job))
        {
            guint priority = target_priority (self, job->uri);
            if (!demand_admitted (self) || !speculation_due (self, job->uri))
            {
                job_schedule_state (job, !demand_admitted (self) ? 1 : self->view_pending ? 5
                                                                                          : 2);
                continue;
            }
            if (make_room (self, PROBE_RESERVATION, 1, priority != 0, priority))
            {
                job->reservation = PROBE_RESERVATION;
                job->reserves_slot = TRUE;
                job->phase = PROBE;
                launch (job);
            }
            else if (!admit_after_cleanup (job, PROBE_RESERVATION, 1, 1, priority))
                finish_job (job);
            else
                job_schedule_state (job, 3);
            continue;
        }
        if (!job->running && job->phase == WAIT_DECODE)
        {
            gboolean current = is_current (self, job);
            guint priority = current ? 0 : target_priority (self, job->uri);
            if (!current && !demand_admitted (self))
            {
                job_schedule_state (job, 1);
                continue;
            }
            if (make_room (self, job->required_bytes, 0, 0, priority))
            {
                job->cacheable = TRUE;
                job->reservation = job->required_bytes;
                job->phase = DECODE;
                self->decodes++;
                launch (job);
                continue;
            }
            if (current)
                wait_or_uncached (job);
            else if (admit_after_cleanup (job, job->required_bytes, 0, 0, priority))
            {
                job_schedule_state (job, 3);
                continue;
            } /* Retry only after cleanup. */
            else
            {
                finish_job (job); /* Cannot coexist with the retained targets. */
                continue;
            }
        }
        if (!job->running && self->jobs.length == 1)
        {
            if (job->phase == WAIT_PROBE
                && make_room (self, PROBE_RESERVATION, !job->reserves_slot, 0, 0))
            {
                job->reservation = PROBE_RESERVATION;
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
    if (self->foreground_pending && !foreground_job (self))
    {
        self->foreground_pending = FALSE;
        launch (new_job (self, self->selected, TRUE));
    }
    if (demand_admitted (self) && self->speculation_allowed && !self->pressure_blocked
        && enabled (self) && g_get_monotonic_time () >= self->pressure_until
        && speculative_jobs (self) < preload_worker_limit (self))
    {
        /* Probe/admit candidates in priority order, then overlap their actual
         * decodes. A lower-priority probe cannot consume a higher target's budget. */
        GFile *file = NULL;
        for (GList *lp = self->pending.head; lp != NULL; lp = lp->next)
        {
            gchar *uri = g_file_get_uri (lp->data);
            gboolean due = speculation_due (self, uri)
                           && !preparing_preload (self, target_priority (self, uri));
            g_free (uri);
            if (due)
            {
                file = lp->data;
                g_queue_delete_link (&self->pending, lp);
                break;
            }
        }
        if (file != NULL)
        {
            launch (new_job (self, file, FALSE));
            g_object_unref (file);
        }
    }
    dispatch_tasks (self);
    self->pumping = FALSE;
}


/**
 * pressure:
 *
 * Cancel speculation and evict unpinned history on a low-memory warning,
 * starting the recovery cooldown.
 */
static void
pressure (GMemoryMonitor *monitor, GMemoryMonitorWarningLevel level, gpointer data)
{
    RsttoImageCache *self = data;
    rstto_gpu_source_select (self->gpu, 0);
    self->pressure_until = g_get_monotonic_time () + PRESSURE_COOLDOWN;
    self->pressure_blocked = TRUE;
    g_cancellable_cancel (self->warm_cancel);
    self->speculation_allowed = FALSE;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        if (!is_current (self, lp->data))
            cancel_job (lp->data, G_STRFUNC);
    evict_all (self);
}


/**
 * rstto_image_cache_finalize:
 *
 * Release cache containers, settings, monitors and remaining object references
 * after asynchronous owners have drained.
 */
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
    g_strfreev (self->probe_raw_types);
    g_clear_object (&self->memory_monitor);
    g_clear_object (&self->gpu);
    g_clear_object (&self->warm_cancel);
    G_OBJECT_CLASS (rstto_image_cache_parent_class)->finalize (object);
}


/**
 * ready_accepted:
 *
 * Collect the consumer's acceptance without skipping later observer handlers.
 * Observers return FALSE; a delivery without a consumer is not accepted.
 */
static gboolean
ready_accepted (GSignalInvocationHint *hint, GValue *accumulator, const GValue *result, gpointer unused)
{
    g_value_set_boolean (accumulator, g_value_get_boolean (accumulator) || g_value_get_boolean (result));
    return TRUE;
}


/**
 * rstto_image_cache_class_init:
 *
 * Register cache delivery and preparation signals and install object cleanup.
 */
static void
rstto_image_cache_class_init (RsttoImageCacheClass *klass)
{
    G_OBJECT_CLASS (klass)->finalize = rstto_image_cache_finalize;
    signals[READY] = g_signal_new ("ready", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                   0, ready_accepted, NULL, NULL, G_TYPE_BOOLEAN, 1, G_TYPE_POINTER);
    signals[PREPARE_RENDER] = g_signal_new ("prepare-render", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                            0, NULL, NULL, NULL, G_TYPE_NONE, 3,
                                            G_TYPE_FILE, G_TYPE_POINTER, G_TYPE_POINTER);
    signals[IDENTITY_CHANGED] = g_signal_new ("identity-changed", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST,
                                              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_FILE);
}


/**
 * rstto_image_cache_init:
 *
 * Initialize containers, explicit defaults, scheduling state and low-memory
 * monitoring.
 */
static void
rstto_image_cache_init (RsttoImageCache *self)
{
    self->entries = g_hash_table_new_full (key_hash, key_equal, NULL, entry_unref);
    self->requests = g_hash_table_new (request_hash, request_equal);
    self->targets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    g_queue_init (&self->lru);
    g_queue_init (&self->jobs);
    g_queue_init (&self->pending);
    self->preload_workers = RSTTO_PRELOAD_WORKERS_DEFAULT;
    self->warm_cancel = g_cancellable_new ();
    self->limits = (RsttoCacheLimits) { TRUE, 5, 256 * MIB, 128 * MIB };
    self->excluded_mime_types = g_new0 (gchar *, 1);
    self->probe_raw_types = rstto_image_probe_raw_mime_types ();
    self->memory_monitor = g_memory_monitor_dup_default ();
    g_signal_connect_object (self->memory_monitor, "low-memory-warning", G_CALLBACK (pressure), self, 0);
}


/**
 * rstto_image_cache_new:
 *
 * Create a cache manager whose container mutations and signal delivery belong
 * to the main thread.
 */
RsttoImageCache *
rstto_image_cache_new (void)
{
    return g_object_new (RSTTO_TYPE_IMAGE_CACHE, NULL);
}


/**
 * rstto_image_cache_set_gpu:
 *
 * Replace the optional GPU renderer and invalidate derivatives tied to the
 * previous context.
 */
void
rstto_image_cache_set_gpu (RsttoImageCache *self, RsttoGpuRenderer *gpu)
{
    if (self->gpu == gpu)
        return;
    if (self->gpu != NULL)
    {
        rstto_gpu_source_select (self->gpu, 0);
        cancel_other_jobs (self, NULL);
        evict_all (self);
    }
    g_cancellable_cancel (self->warm_cancel);
    g_clear_object (&self->warm_cancel);
    self->warm_cancel = g_cancellable_new ();
    g_set_object (&self->gpu, gpu);
    pump (self);
}


/**
 * rstto_image_cache_get_gpu_stats:
 *
 * Copy GPU accounting counters, returning zero counters when no renderer is
 * installed.
 */
void
rstto_image_cache_get_gpu_stats (RsttoImageCache *self, RsttoGpuStats *stats)
{
    rstto_gpu_renderer_get_stats (self->gpu, stats);
}


/**
 * rstto_image_cache_set_excluded_mime_types:
 *
 * Replace the MIME exclusions used to keep unsuitable formats on the normal
 * foreground path.
 */
void
rstto_image_cache_set_excluded_mime_types (RsttoImageCache *self, const gchar *const *types)
{
    g_strfreev (self->excluded_mime_types);
    self->excluded_mime_types = types != NULL ? g_strdupv ((gchar **) types) : g_new0 (gchar *, 1);
}


/**
 * rstto_image_cache_set_preload_workers:
 *
 * Apply a live worker limit and cancel excess speculative jobs without
 * releasing their reservations prematurely.
 */
void
rstto_image_cache_set_preload_workers (RsttoImageCache *self, guint workers)
{
    self->preload_workers = CLAMP (workers, 1, RSTTO_PRELOAD_WORKERS_MAX);
    if (self->active_tasks > 1 + preload_worker_limit (self))
        g_cancellable_cancel (self->warm_cancel);
    guint active = 0;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        active += !job->foreground_slot && !g_cancellable_is_cancelled (job->cancel);
    }
    while (active > preload_worker_limit (self))
    {
        DecodeJob *worst = NULL;
        guint worst_priority = 0;
        for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        {
            DecodeJob *job = lp->data;
            if (job->foreground_slot || is_current (self, job) || g_cancellable_is_cancelled (job->cancel))
                continue;
            guint priority = GPOINTER_TO_UINT (g_hash_table_lookup (self->targets, job->uri));
            if (priority == 0)
                priority = G_MAXUINT;
            if (worst == NULL || priority > worst_priority)
            {
                worst = job;
                worst_priority = priority;
            }
        }
        if (worst == NULL)
            break;
        cancel_job (worst, G_STRFUNC);
        active--;
    }
    /* Cancelled jobs still count in speculative_jobs() until job_free(). */
    pump (self);
}


/**
 * rstto_image_cache_set_limits:
 *
 * Apply new cache limits, reclaim eligible storage and preserve the
 * displayed-image exception while work drains.
 */
void
rstto_image_cache_set_limits (RsttoImageCache *self, const RsttoCacheLimits *limits)
{
    if (self->limits.enabled != limits->enabled || self->limits.max_images != limits->max_images
        || self->limits.max_bytes != limits->max_bytes || self->limits.max_image_bytes != limits->max_image_bytes)
        for (GList *lp = self->lru.head; lp != NULL; lp = lp->next)
            ((Entry *) lp->data)->gpu_rejected = FALSE;
    if (limits->max_bytes < self->limits.max_bytes || limits->max_image_bytes < self->limits.max_image_bytes
        || limits->max_images < self->limits.max_images || !limits->enabled)
        rstto_gpu_source_select (self->gpu, 0);
    self->limits = *limits;
    if (!fits (self, 0, 0))
        g_cancellable_cancel (self->warm_cancel);
    else if (g_cancellable_is_cancelled (self->warm_cancel) && !self->pressure_blocked)
    {
        g_clear_object (&self->warm_cancel);
        self->warm_cancel = g_cancellable_new ();
    }
    GHashTableIter targets;
    gpointer value;
    g_hash_table_iter_init (&targets, self->targets);
    while (g_hash_table_iter_next (&targets, NULL, &value))
        if (GPOINTER_TO_UINT (value) > preload_capacity (limits->max_images))
            g_hash_table_iter_remove (&targets);
    for (GList *lp = self->pending.head, *next; lp != NULL; lp = next)
    {
        GFile *file = lp->data;
        gchar *uri = g_file_get_uri (file);
        next = lp->next;
        if (!g_hash_table_contains (self->targets, uri))
        {
            g_queue_delete_link (&self->pending, lp);
            g_object_unref (file);
        }
        g_free (uri);
    }
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (!is_current (self, job))
            cancel_job (job, G_STRFUNC);
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
            if (entry->bytes > limits->max_image_bytes && can_drop_render (self, entry))
                drop_render (entry);
            if (entry->bytes > limits->max_image_bytes
                || !rgba_bytes (entry->image.width, entry->image.height, &full)
                || full > limits->max_image_bytes)
                remove_entry (self, entry);
        }
        trim_to_limits (self);
    }
    if (!fits (self, 0, 0))
        for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
        {
            DecodeJob *job = lp->data;
            if (is_current (self, job) && job->cacheable)
            {
                self->foreground_pending = TRUE;
                cancel_job (job, G_STRFUNC);
            }
        }
    pump (self);
}


/**
 * rstto_image_cache_request:
 *
 * Make a file the foreground demand, coalescing or promoting existing work and
 * refreshing demand recency on a hit.
 */
void
rstto_image_cache_request (RsttoImageCache *self, GFile *file, const gchar *mime_type, const RsttoDecodeOptions *options)
{
    if (self->stopped)
        return;
    if ((self->selected == NULL || !g_file_equal (self->selected, file))
        && self->request_revision == self->navigation_revision)
        rstto_image_cache_navigation (self, RSTTO_NAV_JUMP, 1);
    self->request_revision = self->navigation_revision;
    self->generation++;
    rstto_profile_end (rstto_profile_now (), self, self->generation, "cache-request", "%s", "");
    if ((self->selected == NULL || !g_file_equal (self->selected, file))
        && g_get_monotonic_time () >= self->pressure_until)
    {
        self->pressure_blocked = FALSE;
        if (g_cancellable_is_cancelled (self->warm_cancel))
        {
            g_clear_object (&self->warm_cancel);
            self->warm_cancel = g_cancellable_new ();
        }
    }
    if (!options_equal (&self->options, options))
    {
        rstto_gpu_source_select (self->gpu, 0);
        evict_all (self);
    }
    self->options = *options;
    g_set_object (&self->selected, file);
    g_free (self->mime_type);
    self->mime_type = g_strdup (mime_type);
    self->busy = TRUE;
    self->foreground_pending = TRUE;
    gchar *uri = g_file_get_uri (file);
    g_hash_table_remove (self->targets, uri);
    for (GList *lp = self->pending.head, *next; lp != NULL; lp = next)
    {
        next = lp->next;
        if (g_file_equal (lp->data, file))
        {
            g_object_unref (lp->data);
            g_queue_delete_link (&self->pending, lp);
        }
    }
    DecodeJob lookup = { .uri = uri, .options = *options };
    DecodeJob *matching = g_hash_table_lookup (self->requests, &lookup);
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (!g_cancellable_is_cancelled (job->cancel)
            && job == matching && (g_atomic_int_get (&job->interest) == 1 || g_atomic_int_compare_and_exchange (&job->interest, 0, 1)))
        {
            g_atomic_int_set (&job->demand, TRUE);
            if (job->render_entry != NULL)
            {
                job->render_entry->demanded = TRUE;
                rstto_gpu_source_select (self->gpu, job->render_entry->source_serial);
            }
            job->generation = self->generation;
            self->foreground_pending = FALSE;
        }
        else if (g_atomic_int_get (&job->demand) || !options_equal (&job->options, options))
            cancel_job (job, G_STRFUNC);
        /* set_candidates() cancels obsolete speculation once the new order is
         * known. Keep a still-useful preload running across demand cache hits. */
    }
    g_free (uri);
    pump (self);
}


/**
 * rstto_image_cache_begin_update:
 *
 * Defer scheduling while the caller atomically updates demand and its
 * candidate snapshot.
 */
void
rstto_image_cache_begin_update (RsttoImageCache *self)
{
    self->update_depth++;
}


/**
 * rstto_image_cache_end_update:
 *
 * Commit the outermost scheduling transaction and pump its complete snapshot.
 */
void
rstto_image_cache_end_update (RsttoImageCache *self)
{
    g_return_if_fail (self->update_depth != 0);
    self->update_depth--;
    pump (self);
}


/**
 * rstto_image_cache_set_schedule:
 *
 * Replace pending selection and ranked neighbours together, retaining useful
 * jobs without refreshing completed-entry recency.
 */
void
rstto_image_cache_set_schedule (RsttoImageCache *self, GFile *pending, GPtrArray *files)
{
    if (self->stopped)
        return;
    GHashTable *old_targets = self->targets;
    self->targets = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    gchar *pending_uri = pending != NULL ? g_file_get_uri (pending) : NULL;
    gboolean changed = g_strcmp0 (pending_uri, self->pending_target) != 0;
    g_free (self->pending_target);
    self->pending_target = pending_uri;
    self->urgent_rank = 0;
    g_queue_clear_full (&self->pending, g_object_unref);
    guint capacity = preload_capacity (self->limits.max_images);
    for (guint n = 0; n < files->len + (pending != NULL) && self->targets != NULL
                      && g_hash_table_size (self->targets) < capacity;
         n++)
    {
        GFile *file = pending != NULL && n == 0 ? pending : g_ptr_array_index (files, n - (pending != NULL));
        gchar *uri = g_file_get_uri (file);
        if ((self->selected != NULL && g_file_equal (self->selected, file))
            || g_hash_table_contains (self->targets, uri) || !g_file_is_native (file))
        {
            g_free (uri);
            continue;
        }
        guint rank = g_hash_table_size (self->targets) + 1;
        changed |= GPOINTER_TO_UINT (g_hash_table_lookup (old_targets, uri)) != rank;
        g_hash_table_insert (self->targets, uri, GUINT_TO_POINTER (rank));
        if (n < 2 + (pending != NULL))
            self->urgent_rank = rank;
        DecodeJob lookup = { .uri = uri, .options = self->options };
        DecodeJob *running = g_hash_table_lookup (self->requests, &lookup);
        if (running == NULL || g_cancellable_is_cancelled (running->cancel))
            g_queue_push_tail (&self->pending, g_object_ref (file));
    }
    changed |= g_hash_table_size (self->targets) != g_hash_table_size (old_targets);
    g_hash_table_unref (old_targets);
    if (changed)
        rstto_profile_end (rstto_profile_now (), self, self->navigation_revision, "schedule-targets",
                           "targets=%u urgent=%u burst=%u", g_hash_table_size (self->targets), self->urgent_rank, self->burst_steps);
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        gboolean useful = is_current (self, job) || g_hash_table_contains (self->targets, job->uri);
        /* CAS cannot revive work that has committed to stopping. */
        if (g_atomic_int_compare_and_exchange (&job->interest, useful ? 0 : 1, useful ? 1 : 0))
            rstto_profile_end (rstto_profile_now (), job, job->serial, "work-interest",
                               "phase=%d useful=%d", job->phase, useful);
    }
    pump (self);
}


/**
 * rstto_image_cache_view_changed:
 *
 * Track meaningful view changes and defer new neighbour work while a
 * same-image render is pending.
 */
void
rstto_image_cache_view_changed (RsttoImageCache *self, gboolean pending)
{
    if (self->stopped)
        return;
    self->view_pending = pending;
    if (pending)
    {
        self->burst_steps = 0;
        self->navigation_direction = 0;
        schedule_activity (self);
    }
    pump (self);
}


/**
 * rstto_image_cache_set_candidates:
 *
 * Update ranked neighbours using the schedule path without a separate pending
 * selection.
 */
void
rstto_image_cache_set_candidates (RsttoImageCache *self, GPtrArray *files)
{
    rstto_image_cache_set_schedule (self, NULL, files);
}


/**
 * rstto_image_cache_invalidate:
 *
 * Discard pixels and GPU source reuse for a changed file and hard-cancel
 * matching work.
 */
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
            cancel_job (job, G_STRFUNC);
        }
    }
    g_free (uri);
    pump (self);
}


/**
 * rstto_image_cache_cancel_request:
 *
 * Cancel foreground demand without destroying useful completed history.
 */
void
rstto_image_cache_cancel_request (RsttoImageCache *self)
{
    self->generation++;
    self->busy = self->foreground_pending = FALSE;
    for (GList *lp = self->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (g_atomic_int_get (&job->demand))
            cancel_job (job, G_STRFUNC);
    }
    g_clear_object (&self->selected);
    pump (self);
}


/**
 * rstto_image_cache_clear:
 *
 * Clear demand and lookup history while cancelling work and retaining
 * resources until actual cleanup.
 */
void
rstto_image_cache_clear (RsttoImageCache *self)
{
    self->display_revision++;
    rstto_gpu_source_select (self->gpu, 0);
    self->generation++;
    g_clear_handle_id (&self->quiet_source, g_source_remove);
    g_clear_pointer (&self->pending_target, g_free);
    self->view_pending = FALSE;
    self->burst_steps = 0;
    self->last_navigation = 0;
    self->navigation_direction = 0;
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


/**
 * rstto_image_cache_stop:
 *
 * Permanently stop scheduling and delivery, cancelling asynchronous work
 * before releasing cache ownership.
 */
void
rstto_image_cache_stop (RsttoImageCache *self)
{
    self->stopped = TRUE;
    g_cancellable_cancel (self->warm_cancel);
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


/**
 * rstto_image_cache_is_busy:
 *
 * Report foreground, warmup or quiet-timer work that still needs main-context
 * progress.
 */
gboolean
rstto_image_cache_is_busy (RsttoImageCache *self)
{
    return self->busy || self->warmups != 0
           || (self->quiet_source != 0 && !self->view_pending && !g_queue_is_empty (&self->pending));
}


/**
 * rstto_image_cache_get_stats:
 *
 * Snapshot retained bytes, reservations, slots and task counters for
 * diagnostics and invariant checks.
 */
void
rstto_image_cache_get_stats (RsttoImageCache *self, RsttoCacheStats *stats)
{
    *stats = (RsttoCacheStats) { self->resident_slots, self->jobs.length,
                                 self->resident_bytes, reserved_bytes (self), self->decodes, self->hits,
                                 self->renders, self->render_hits, reserved_slots (self), speculative_slots (self), self->active_tasks, self->peak_tasks };
}


/**
 * rstto_image_cache_neighbor_positions:
 *
 * Build ordered, deduplicated neighbour positions using initial or directional
 * lookahead, wrap policy and speculative capacity.
 */
GArray *
rstto_image_cache_neighbor_positions (guint length, guint current, gint direction, gboolean initial, guint count, guint capacity, gboolean wrap)
{
    GArray *positions = g_array_new (FALSE, FALSE, sizeof (guint));
    if (length < 2 || current >= length || preload_capacity (capacity) == 0 || count == 0)
        return positions;
    guint wanted = MIN (length - 1, MIN (preload_capacity (capacity), initial ? 4 : count));
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

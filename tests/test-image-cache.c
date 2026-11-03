/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Include the implementation to exercise private accounting and deterministic
 * worker barriers without adding test hooks to the application API. */
#define RSTTO_CACHE_TEST
#include "../src/image_cache.c"

#include <fcntl.h>
#include <gdk/gdk.h>
#include <sys/resource.h>
#include <unistd.h>
#include "probe-fixtures.h"

typedef struct
{
    gchar *directory;
    GFile *files[8];
    RsttoImageCache *cache;
    guint ready, errors, invalidations;
    guint32 pixel;
    guint64 peak_accounted;
    gboolean enforce_limits;
} Fixture;

static RsttoDecodeOptions full_size = { FALSE, 0, 0 };
static GMutex gate_mutex;
static GCond gate_cond;
static gint gate_entered;
static gboolean gate_released;
static JobPhase gate_phase;
static gchar *gate_uri;

static void
check_limits (Fixture *fixture)
{
    if (!fixture->enforce_limits)
        return;
    RsttoImageCache *cache = fixture->cache;
    g_assert_cmpuint (cache->resident_bytes + reserved_bytes (cache), <=, cache->limits.max_bytes);
    g_assert_cmpuint (cache->resident_slots + reserved_slots (cache), <=, cache->limits.max_images);
    g_assert_cmpuint (speculative_slots (cache), <=, preload_capacity (cache->limits.max_images));
    guint draining = 0;
    for (GList *lp = cache->jobs.head; lp != NULL; lp = lp->next)
    {
        DecodeJob *job = lp->data;
        if (job->running && g_cancellable_is_cancelled (job->cancel))
            draining += job->phase == RENDER ? job->render_tasks : 1;
    }
    g_assert_cmpuint (cache->active_tasks - draining, <=, 1 + preload_worker_limit (cache));
}

static void
received (RsttoImageCache *cache, RsttoImageData *image, Fixture *fixture)
{
    fixture->ready++;
    check_limits (fixture);
    fixture->peak_accounted = MAX (fixture->peak_accounted, cache->resident_bytes + reserved_bytes (cache));
    fixture->errors += image->error != NULL;
    if (image->surface != NULL)
        fixture->pixel = *(guint32 *) cairo_image_surface_get_data (image->surface);
}

static void
invalidated (RsttoImageCache *cache, GFile *file, Fixture *fixture)
{
    fixture->invalidations++;
}

static void
iterate (void)
{
    for (guint n = 0; n < 10 && g_main_context_pending (NULL); n++)
        g_main_context_iteration (NULL, FALSE);
    g_usleep (1000);
}

static void
drain (Fixture *fixture)
{
    gint64 until = g_get_monotonic_time () + 15 * G_USEC_PER_SEC;
    do
    {
        iterate ();
        fixture->peak_accounted = MAX (fixture->peak_accounted, fixture->cache->resident_bytes + reserved_bytes (fixture->cache));
        check_limits (fixture);
        g_assert_cmpuint (fixture->cache->jobs.length, <=, RSTTO_PRELOAD_WORKERS_MAX + 1);
        g_assert_cmpuint (fixture->cache->jobs.length - speculative_jobs (fixture->cache), <=, 1);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    } while (fixture->cache->jobs.length != 0 || rstto_image_cache_is_busy (fixture->cache));
}

static void
save_image (GFile *file, guint32 color, const gchar *format, gint width, gint height)
{
    GError *error = NULL;
    gchar *path = g_file_get_path (file);
    GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, !g_str_equal (format, "jpeg"), 8, width, height);
    gdk_pixbuf_fill (pixbuf, color);
    gboolean saved = gdk_pixbuf_save (pixbuf, path, format, &error, NULL);
    g_assert_no_error (error);
    g_assert_true (saved);
    g_object_unref (pixbuf);
    g_free (path);
}

static void
setup (Fixture *fixture, gconstpointer unused)
{
    fixture->directory = g_dir_make_tmp ("ristretto-cache-test-XXXXXX", NULL);
    g_assert_nonnull (fixture->directory);
    for (guint n = 0; n < G_N_ELEMENTS (fixture->files); n++)
    {
        gchar *path = g_strdup_printf ("%s/%u.png", fixture->directory, n);
        fixture->files[n] = g_file_new_for_path (path);
        save_image (fixture->files[n], 0x102030ff + (n << 24), "png", 128, 96);
        g_free (path);
    }
    fixture->cache = rstto_image_cache_new ();
    g_assert_cmpuint (fixture->cache->preload_workers, ==, RSTTO_PRELOAD_WORKERS_DEFAULT);
    /* Keep the original policy cases serial; parallel cases opt in below. */
    rstto_image_cache_set_preload_workers (fixture->cache, 1);
    g_signal_connect (fixture->cache, "ready", G_CALLBACK (received), fixture);
    g_signal_connect (fixture->cache, "identity-changed", G_CALLBACK (invalidated), fixture);
}

static void
teardown (Fixture *fixture, gconstpointer unused)
{
    if (fixture->cache != NULL)
    {
        rstto_image_cache_stop (fixture->cache);
        drain (fixture);
        g_assert_cmpuint (fixture->cache->resident_bytes, ==, 0);
        g_assert_cmpuint (reserved_bytes (fixture->cache), ==, 0);
        g_object_unref (fixture->cache);
    }
    for (guint n = 0; n < G_N_ELEMENTS (fixture->files); n++)
    {
        g_file_delete (fixture->files[n], NULL, NULL);
        g_object_unref (fixture->files[n]);
    }
    g_rmdir (fixture->directory);
    g_free (fixture->directory);
}

static void
select_image (Fixture *fixture, guint n)
{
    guint before = fixture->ready;
    rstto_image_cache_request (fixture->cache, fixture->files[n], NULL, &full_size);
    drain (fixture);
    g_assert_cmpuint (fixture->ready, ==, before + 1);
}

static void
candidates (Fixture *fixture, guint first, guint count)
{
    GPtrArray *files = g_ptr_array_new ();
    for (guint n = first; n < first + count; n++)
        g_ptr_array_add (files, fixture->files[n]);
    rstto_image_cache_set_candidates (fixture->cache, files);
    g_ptr_array_unref (files);
}

static void
test_reuse (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    candidates (fixture, 1, 3);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, 4);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 4);
    for (guint n = 0; n < 4; n++)
        select_image (fixture, n);
    for (gint n = 3; n >= 0; n--)
        select_image (fixture, n);
    g_assert_cmpuint (fixture->cache->decodes, ==, 4);
    g_assert_cmpuint (fixture->cache->hits, ==, 8);
    g_assert_cmpuint (fixture->errors, ==, 0);
}

static void
history_candidates (Fixture *fixture, guint selected)
{
    GArray *positions = rstto_image_cache_neighbor_positions (8, selected,
                          selected == 1 ? 1 : -1, FALSE, 5,
                          fixture->cache->limits.max_images, TRUE);
    GPtrArray *files = g_ptr_array_new ();
    for (guint n = 0; n < positions->len; n++)
        g_ptr_array_add (files, fixture->files[g_array_index (positions, guint, n)]);
    rstto_image_cache_set_candidates (fixture->cache, files);
    g_ptr_array_unref (files);
    g_array_unref (positions);
}

static void
test_history_switch (Fixture *fixture, gconstpointer unused)
{
    RsttoCacheLimits limits = { TRUE, 6, 2048 * MIB, 256 * MIB };
    fixture->enforce_limits = TRUE;
    rstto_image_cache_set_limits (fixture->cache, &limits);
    rstto_image_cache_set_preload_workers (fixture->cache, 5);
    select_image (fixture, 0);
    select_image (fixture, 1);
    history_candidates (fixture, 1);
    drain (fixture);
    for (guint n = 0; n < 8; n++)
    {
        guint64 hits = fixture->cache->hits, decodes = fixture->cache->decodes;
        select_image (fixture, n % 2);
        g_assert_cmpuint (fixture->cache->hits, ==, hits + 1);
        g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
        history_candidates (fixture, n % 2);
        drain (fixture);
    }
}

static void
test_lru (Fixture *fixture, gconstpointer unused)
{
    RsttoCacheLimits limits = { TRUE, 3, 16 * MIB, MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    select_image (fixture, 0);
    select_image (fixture, 1);
    select_image (fixture, 2);
    select_image (fixture, 0); /* 1 is now the least recently demanded. */
    select_image (fixture, 3);
    guint64 decodes = fixture->cache->decodes;
    select_image (fixture, 0);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    select_image (fixture, 1);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes + 1);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 3);
}

static void
large_images (Fixture *fixture)
{
    for (guint i = 0; i < G_N_ELEMENTS (fixture->files); i++)
        save_image (fixture->files[i], 0x102030ff + (i << 24), "png", 1024, 1024);
}

static void
test_weight (Fixture *fixture, gconstpointer unused)
{
    large_images (fixture);
    RsttoCacheLimits limits = { TRUE, 5, 26 * MIB, 8 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    select_image (fixture, 0);
    candidates (fixture, 1, 4);
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    do
    {
        iterate ();
        g_assert_cmpuint (fixture->cache->resident_bytes + reserved_bytes (fixture->cache), <=, limits.max_bytes);
        g_assert_cmpuint (fixture->cache->resident_slots + reserved_slots (fixture->cache), <=, limits.max_images);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    } while (fixture->cache->jobs.length != 0);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 3);
    /* Admission is a skip, not an eviction/reload cycle. */
    g_assert_cmpuint (fixture->cache->decodes, ==, 3);
    limits.max_image_bytes = 1024;
    rstto_image_cache_set_limits (fixture->cache, &limits);
    select_image (fixture, 1);
    candidates (fixture, 2, 2);
    drain (fixture);
    g_assert_cmpuint (g_hash_table_size (fixture->cache->entries), ==, 0);
    g_assert_false (fixture->cache->speculation_allowed);
    g_assert_cmpuint (fixture->cache->decodes, ==, 4);
}

static void
test_freshness (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    gchar *path = g_file_get_path (fixture->files[0]);
    struct stat before;
    g_assert_cmpint (g_stat (path, &before), ==, 0);
    save_image (fixture->files[0], 0xaa2233ff, "png", 128, 96);
    struct timespec times[2] = { before.st_atim, before.st_mtim };
    g_assert_cmpint (utimensat (AT_FDCWD, path, times, 0), ==, 0);
    select_image (fixture, 0);
    g_assert_cmphex (fixture->pixel, ==, 0xffaa2233);
    g_assert_cmpuint (fixture->cache->decodes, ==, 2);
    g_assert_cmpuint (fixture->invalidations, ==, 1);
    gchar *replacement = g_strconcat (path, ".new", NULL);
    GFile *new_file = g_file_new_for_path (replacement);
    save_image (new_file, 0xbb4455ff, "png", 128, 96);
    g_assert_cmpint (g_rename (replacement, path), ==, 0);
    select_image (fixture, 0);
    g_assert_cmphex (fixture->pixel, ==, 0xffbb4455);
    g_assert_cmpuint (fixture->cache->decodes, ==, 3);
    rstto_image_cache_invalidate (fixture->cache, fixture->files[0]);
    select_image (fixture, 0);
    g_assert_cmpuint (fixture->cache->decodes, ==, 4);
    g_file_delete (fixture->files[0], NULL, NULL);
    select_image (fixture, 0);
    g_assert_cmpuint (fixture->errors, ==, 1);
    save_image (fixture->files[0], 0xcc6677ff, "png", 128, 96);
    select_image (fixture, 0);
    g_assert_cmphex (fixture->pixel, ==, 0xffcc6677);
    g_object_unref (new_file);
    g_free (replacement);
    g_free (path);
}

static void
test_symlink (Fixture *fixture, gconstpointer unused)
{
    gchar *link = g_file_get_path (fixture->files[0]);
    gchar *target1 = g_file_get_path (fixture->files[1]);
    gchar *target2 = g_file_get_path (fixture->files[2]);
    g_unlink (link);
    g_assert_cmpint (symlink (target1, link), ==, 0);
    select_image (fixture, 0);
    guint32 first = fixture->pixel;
    g_unlink (link);
    g_assert_cmpint (symlink (target2, link), ==, 0);
    select_image (fixture, 0);
    g_assert_cmphex (fixture->pixel, !=, first);
    g_assert_cmpuint (fixture->cache->decodes, ==, 2);
    g_free (target1);
    g_free (target2);
    g_free (link);
}

static void
barrier (DecodeJob *job)
{
    if (job->phase != gate_phase || (gate_uri != NULL && !g_str_equal (job->uri, gate_uri)))
        return;
    g_mutex_lock (&gate_mutex);
    g_atomic_int_inc (&gate_entered);
    while (!gate_released)
        g_cond_wait (&gate_cond, &gate_mutex);
    g_mutex_unlock (&gate_mutex);
}

static void
start_barrier (GFile *file, JobPhase phase)
{
    gate_uri = file != NULL ? g_file_get_uri (file) : NULL;
    gate_phase = phase;
    gate_released = FALSE;
    g_atomic_int_set (&gate_entered, FALSE);
    worker_hook = barrier;
}

static void
await_barrier (void)
{
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (!g_atomic_int_get (&gate_entered))
    {
        iterate ();
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
}

static void
release_barrier (Fixture *fixture)
{
    g_mutex_lock (&gate_mutex);
    gate_released = TRUE;
    g_cond_broadcast (&gate_cond);
    g_mutex_unlock (&gate_mutex);
    drain (fixture);
    worker_hook = NULL;
    g_clear_pointer (&gate_uri, g_free);
}

static void
test_promotion (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    start_barrier (fixture->files[1], DECODE);
    candidates (fixture, 1, 2);
    await_barrier ();
    guint64 decodes = fixture->cache->decodes;
    guint ready = fixture->ready;
    rstto_image_cache_request (fixture->cache, fixture->files[1], NULL, &full_size);
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 1);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->ready, ==, ready + 1);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    g_assert_cmpuint (fixture->errors, ==, 0);
}

static void
test_cancellation (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    start_barrier (fixture->files[1], DECODE);
    candidates (fixture, 1, 2);
    await_barrier ();
    guint64 reservation = reserved_bytes (fixture->cache);
    guint ready = fixture->ready;
    for (guint n = 0; n < 100; n++)
        rstto_image_cache_request (fixture->cache, fixture->files[2 + n % 4], NULL, &full_size);
    g_assert_cmpuint (fixture->cache->jobs.length, <=, 2);
    g_assert_cmpuint (reserved_bytes (fixture->cache), >=, reservation);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->ready, ==, ready + 1);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, 0);
}

static void
test_changed_during_decode (Fixture *fixture, gconstpointer unused)
{
    start_barrier (fixture->files[0], DECODE);
    rstto_image_cache_request (fixture->cache, fixture->files[0], NULL, &full_size);
    await_barrier ();
    save_image (fixture->files[0], 0xfe1234ff, "png", 128, 96);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->errors, ==, 1);
    g_assert_cmpuint (g_hash_table_size (fixture->cache->entries), ==, 0);
    select_image (fixture, 0);
    g_assert_cmphex (fixture->pixel, ==, 0xfffe1234);
}

static void
test_stop (Fixture *fixture, gconstpointer unused)
{
    start_barrier (fixture->files[0], DECODE);
    rstto_image_cache_request (fixture->cache, fixture->files[0], NULL, &full_size);
    await_barrier ();
    rstto_image_cache_stop (fixture->cache);
    g_assert_cmpuint (reserved_bytes (fixture->cache), >, 0);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->ready, ==, 0);
    g_assert_cmpuint (fixture->cache->resident_bytes, ==, 0);
}

static void
test_limits_and_pressure (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    start_barrier (fixture->files[1], DECODE);
    candidates (fixture, 1, 2);
    await_barrier ();
    guint64 reserved = reserved_bytes (fixture->cache);
    RsttoCacheLimits limits = { TRUE, 2, 64 * 1024, 64 * 1024 };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reserved);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->cache->resident_bytes, <=, limits.max_bytes);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    limits = (RsttoCacheLimits) { TRUE, 5, 16 * MIB, MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    select_image (fixture, 1);
    pressure (NULL, G_MEMORY_MONITOR_WARNING_LEVEL_LOW, fixture->cache);
    candidates (fixture, 2, 2);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    guint64 decodes = fixture->cache->decodes;
    fixture->cache->pressure_until = 0;
    candidates (fixture, 2, 2); /* cooldown alone must not restart speculation */
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    select_image (fixture, 2);
    candidates (fixture, 3, 2);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, >, decodes);
}

static void
test_formats_and_pixels (Fixture *fixture, gconstpointer unused)
{
    const gchar *formats[] = { "png", "jpeg", "bmp" };
    for (guint n = 0; n < G_N_ELEMENTS (formats); n++)
    {
        save_image (fixture->files[n], 0x66339980, formats[n], 128, 96);
        select_image (fixture, n);
        gchar *path = g_file_get_path (fixture->files[n]);
        GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file (path, NULL);
        cairo_surface_t *reference = cairo_image_surface_create (CAIRO_FORMAT_ARGB32, 128, 96);
        cairo_t *cr = cairo_create (reference);
        gdk_cairo_set_source_pixbuf (cr, pixbuf, 0, 0);
        cairo_paint (cr);
        cairo_surface_flush (reference);
        g_assert_cmphex (fixture->pixel, ==, *(guint32 *) cairo_image_surface_get_data (reference));
        cairo_destroy (cr);
        cairo_surface_destroy (reference);
        g_object_unref (pixbuf);
        g_free (path);
    }
    RsttoDecodeOptions small = { TRUE, 32, 24 };
    rstto_image_cache_request (fixture->cache, fixture->files[0], NULL, &small);
    drain (fixture);
    g_assert_cmpint (fixture->cache->displayed->image.decoded_width, ==, 32);
    g_assert_cmpint (fixture->cache->displayed->image.decoded_height, ==, 24);
    g_assert_cmpfloat (fixture->cache->displayed->image.quality_scale, ==, 0.25);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    guint64 decodes = fixture->cache->decodes;
    rstto_image_cache_request (fixture->cache, fixture->files[0], NULL, &small);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
}

static void
test_animation (Fixture *fixture, gconstpointer unused)
{
    const gchar gif[] = "GIF89a\1\0\1\0\200\0\0\0\0\0\377\377\377"
                        "!\371\4\0\12\0\0\0,\0\0\0\0\1\0\1\0\0\2\2\104\1\0"
                        "!\371\4\0\12\0\0\0,\0\0\0\0\1\0\1\0\0\2\2\114\1\0;";
    gchar *path = g_file_get_path (fixture->files[1]);
    g_assert_true (g_file_set_contents (path, gif, sizeof gif - 1, NULL));
    select_image (fixture, 0);
    candidates (fixture, 1, 1);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    select_image (fixture, 1);
    g_assert_nonnull (fixture->cache->displayed->image.animation);
    g_assert_false (fixture->cache->speculation_allowed);
    g_assert_cmpuint (fixture->errors, ==, 0);
    g_free (path);
}

static void
assert_positions (guint length, guint current, gint direction, gboolean initial, gboolean wrap, const guint *expected, guint count)
{
    GArray *positions = rstto_image_cache_neighbor_positions (length, current, direction, initial, 2, 5, wrap);
    g_assert_cmpuint (positions->len, ==, count);
    for (guint n = 0; n < count; n++)
        g_assert_cmpuint (g_array_index (positions, guint, n), ==, expected[n]);
    g_array_unref (positions);
}

static void
test_neighbors (void)
{
    assert_positions (10, 4, 1, TRUE, FALSE, (guint[]) { 5, 6, 3 }, 3);
    assert_positions (10, 4, -1, TRUE, FALSE, (guint[]) { 3, 2, 5 }, 3);
    assert_positions (10, 4, 1, FALSE, FALSE, (guint[]) { 5, 6 }, 2);
    assert_positions (10, 4, -1, FALSE, FALSE, (guint[]) { 3, 2 }, 2);
    assert_positions (10, 9, 1, FALSE, TRUE, (guint[]) { 0, 1 }, 2);
    assert_positions (10, 0, -1, FALSE, TRUE, (guint[]) { 9, 8 }, 2);
    assert_positions (10, 9, 1, FALSE, FALSE, NULL, 0);
    assert_positions (2, 0, 1, TRUE, TRUE, (guint[]) { 1 }, 1);
    assert_positions (1, 0, 1, TRUE, TRUE, NULL, 0);
    GArray *none = rstto_image_cache_neighbor_positions (10, 4, 1, TRUE, 0, 5, TRUE);
    g_assert_cmpuint (none->len, ==, 0);
    g_array_unref (none);
    GArray *large = rstto_image_cache_neighbor_positions (100, 4, 1, TRUE, 2, 50, TRUE);
    g_assert_cmpuint (large->len, ==, 4);
    g_array_unref (large);
    guint64 bytes;
    g_assert_false (rgba_bytes (0, 100, &bytes));
    g_assert_false (rgba_bytes (-1, 100, &bytes));
    g_assert_true (rgba_bytes (G_MAXINT, G_MAXINT, &bytes));
    g_assert_cmpuint (bytes, >, 256 * MIB);
}


static void
stop_on_ready (RsttoImageCache *cache, RsttoImageData *image, gpointer unused)
{
    rstto_image_cache_stop (cache);
    /* The borrowed result must survive clearing its cache inside delivery. */
    g_assert_cmpint (cairo_image_surface_get_width (image->surface), ==, 128);
}

static void
test_reentrant_stop (Fixture *fixture, gconstpointer unused)
{
    g_signal_connect (fixture->cache, "ready", G_CALLBACK (stop_on_ready), NULL);
    select_image (fixture, 0);
    g_assert_cmpuint (fixture->cache->resident_bytes, ==, 0);
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 0);
}

static void
test_disabled_and_ephemeral (Fixture *fixture, gconstpointer unused)
{
    RsttoCacheLimits limits = { FALSE, 5, 256 * MIB, 128 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    select_image (fixture, 0);
    select_image (fixture, 1);
    select_image (fixture, 0);
    g_assert_cmpuint (fixture->cache->decodes, ==, 3);
    g_assert_cmpuint (g_hash_table_size (fixture->cache->entries), ==, 0);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    limits.enabled = TRUE;
    rstto_image_cache_set_limits (fixture->cache, &limits);
    RsttoDecodeOptions ephemeral = { FALSE, 0, 0, TRUE };
    rstto_image_cache_request (fixture->cache, fixture->files[0], NULL, &ephemeral);
    drain (fixture);
    candidates (fixture, 1, 2);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, 4);
    g_assert_cmpuint (g_hash_table_size (fixture->cache->entries), ==, 0);
    g_assert_false (fixture->cache->speculation_allowed);
}

static void
test_foreground_limit_reduction (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    start_barrier (fixture->files[1], DECODE);
    rstto_image_cache_request (fixture->cache, fixture->files[1], NULL, &full_size);
    await_barrier ();
    guint64 reservation = reserved_bytes (fixture->cache);
    RsttoCacheLimits limits = { TRUE, 1, 1024, 1024 };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reservation);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->ready, ==, 2);
    g_assert_cmpuint (g_hash_table_size (fixture->cache->entries), ==, 0);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, 0);
}

static void
test_clear_during_load (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    start_barrier (fixture->files[1], DECODE);
    candidates (fixture, 1, 2);
    await_barrier ();
    rstto_image_cache_clear (fixture->cache);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 0);
    g_assert_cmpuint (reserved_bytes (fixture->cache), >, 0);
    rstto_image_cache_request (fixture->cache, fixture->files[7], NULL, &full_size);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->ready, ==, 2);
    g_assert_cmphex (fixture->pixel, ==, 0xff172030);
}

static void
test_probe_slot (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    start_barrier (fixture->files[1], PROBE);
    candidates (fixture, 1, 2);
    await_barrier ();
    g_assert_cmpuint (reserved_slots (fixture->cache), ==, 1);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, PROBE_RESERVATION);
    rstto_image_cache_set_candidates (fixture->cache, &(GPtrArray) { 0 });
    g_assert_cmpuint (reserved_slots (fixture->cache), ==, 1);
    release_barrier (fixture);
    g_assert_cmpuint (reserved_slots (fixture->cache), ==, 0);
}

static void
test_admission_after_cancellation (Fixture *fixture, gconstpointer unused)
{
    large_images (fixture);
    RsttoCacheLimits limits = { TRUE, 5, 26 * MIB, 8 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    select_image (fixture, 0);
    start_barrier (fixture->files[1], DECODE);
    candidates (fixture, 1, 1);
    await_barrier ();
    rstto_image_cache_request (fixture->cache, fixture->files[2], NULL, &full_size);
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (((DecodeJob *) fixture->cache->jobs.tail->data)->phase != WAIT_PROBE)
    {
        iterate ();
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    g_assert_cmpuint (fixture->ready, ==, 1);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->ready, ==, 2);
    g_assert_true (fixture->cache->speculation_allowed);
    g_assert_nonnull (fixture->cache->displayed->key);
}

static void
test_useful_preload (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    candidates (fixture, 1, 1);
    drain (fixture);
    start_barrier (fixture->files[2], DECODE);
    candidates (fixture, 2, 1);
    await_barrier ();
    DecodeJob *preload = fixture->cache->jobs.head->data;
    guint ready = fixture->ready;
    rstto_image_cache_request (fixture->cache, fixture->files[1], NULL, &full_size);
    candidates (fixture, 2, 2);
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (fixture->ready == ready)
    {
        iterate ();
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    g_assert_false (g_cancellable_is_cancelled (preload->cancel));
    release_barrier (fixture);
    guint64 decodes = fixture->cache->decodes;
    select_image (fixture, 2);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
}

static void
test_replaced_format (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    save_image (fixture->files[0], 0x993311ff, "jpeg", 128, 96);
    rstto_image_cache_request (fixture->cache, fixture->files[0], "image/png", &full_size);
    drain (fixture);
    g_assert_cmpuint (fixture->errors, ==, 0);
    g_assert_cmpuint (fixture->invalidations, ==, 1);
    g_assert_cmpuint (fixture->cache->decodes, ==, 2);
}

static void
test_foreground_slot (Fixture *fixture, gconstpointer unused)
{
    start_barrier (fixture->files[0], DECODE);
    rstto_image_cache_request (fixture->cache, fixture->files[0], NULL, &full_size);
    await_barrier ();
    rstto_image_cache_request (fixture->cache, fixture->files[1], NULL, &full_size);
    for (guint n = 0; n < 5; n++)
        iterate ();
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 1);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->ready, ==, 1);
    g_assert_cmphex (fixture->pixel, ==, 0xff112030);
}

static void
test_unsuitable_probe (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    gchar *path = g_file_get_path (fixture->files[1]);
    gchar *junk = g_malloc0 (PROBE_LIMIT + 4096);
    g_assert_true (g_file_set_contents (path, junk, PROBE_LIMIT + 4096, NULL));
    g_free (junk);
    candidates (fixture, 1, 1);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, 1);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    g_remove (path);
    g_assert_cmpint (mkfifo (path, 0600), ==, 0);
    candidates (fixture, 1, 1);
    drain (fixture); /* A speculative FIFO must never block in open(). */
    g_assert_cmpuint (fixture->cache->decodes, ==, 1);
    g_free (path);
}

static void
await_workers (Fixture *fixture, guint count)
{
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while ((guint) g_atomic_int_get (&gate_entered) < count)
    {
        iterate ();
        check_limits (fixture);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    /* Every entered worker is still blocked: these are actual concurrent
     * executions, not merely GTasks queued for later execution. */
    g_assert_cmpuint (g_atomic_int_get (&gate_entered), ==, count);
}

static DecodeJob *
find_job (Fixture *fixture, guint position)
{
    for (GList *lp = fixture->cache->jobs.head; lp != NULL; lp = lp->next)
        if (g_file_equal (((DecodeJob *) lp->data)->file, fixture->files[position]))
            return lp->data;
    return NULL;
}

static void
await_admission (Fixture *fixture, guint position)
{
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (find_job (fixture, position) == NULL || find_job (fixture, position)->phase != WAIT_DECODE)
    {
        iterate ();
        check_limits (fixture);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
}

static void
test_history_memory (Fixture *fixture, gconstpointer unused)
{
    large_images (fixture);
    RsttoCacheLimits limits = { TRUE, 6, 32 * MIB, 8 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    fixture->enforce_limits = TRUE;
    select_image (fixture, 0);
    select_image (fixture, 1);
    select_image (fixture, 2);
    limits.max_bytes = 24 * MIB;
    rstto_image_cache_set_limits (fixture->cache, &limits);
    guint64 decodes = fixture->cache->decodes;
    /* Probe fits (12+8 MiB). Decode does not fit even after evicting image 0
     * (8+17 MiB). That failed attempt must not evict image 0 or the pair. */
    candidates (fixture, 3, 1);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->resident_bytes, ==, 12 * MIB);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 3);
    g_assert_false (make_room (fixture->cache, G_MAXUINT64, 1, 1, 1));
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 3);
    for (guint n = 0; n < 3; n++)
    {
        guint64 hits = fixture->cache->hits;
        select_image (fixture, n);
        g_assert_cmpuint (fixture->cache->hits, ==, hits + 1);
    }
    /* One decode can coexist with the pair after a budget increase. */
    limits.max_bytes = 26 * MIB;
    rstto_image_cache_set_limits (fixture->cache, &limits);
    candidates (fixture, 3, 1);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes + 1);
    guint64 hits = fixture->cache->hits;
    select_image (fixture, 1);
    select_image (fixture, 2);
    g_assert_cmpuint (fixture->cache->hits, ==, hits + 2);
}

static void
test_history_reversal (Fixture *fixture, gconstpointer unused)
{
    RsttoCacheLimits limits = { TRUE, 6, 2048 * MIB, 256 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    rstto_image_cache_set_preload_workers (fixture->cache, 5);
    fixture->enforce_limits = TRUE;
    select_image (fixture, 0);
    select_image (fixture, 1);
    start_barrier (NULL, DECODE);
    history_candidates (fixture, 1);
    await_workers (fixture, 4);
    guint64 reserved = reserved_bytes (fixture->cache);
    guint64 decodes = fixture->cache->decodes;
    guint64 hits = fixture->cache->hits;
    guint ready = fixture->ready;
    rstto_image_cache_request (fixture->cache, fixture->files[0], NULL, &full_size);
    history_candidates (fixture, 0);
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (fixture->ready == ready)
    {
        iterate ();
        check_limits (fixture);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    g_assert_cmpuint (fixture->cache->hits, ==, hits + 1);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    g_assert_cmpuint (speculative_slots (fixture->cache), ==, 4);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reserved);
    g_assert_cmpint (g_atomic_int_get (&find_job (fixture, 2)->interest), ==, 0);
    g_assert_cmpint (g_atomic_int_get (&find_job (fixture, 3)->interest), ==, 0);
    release_barrier (fixture);
    for (guint n = 1; n < 5; n++)
    {
        hits = fixture->cache->hits;
        decodes = fixture->cache->decodes;
        select_image (fixture, n % 2);
        g_assert_cmpuint (fixture->cache->hits, ==, hits + 1);
        g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
        history_candidates (fixture, n % 2);
        drain (fixture);
    }
}

static void
test_history_capacity (Fixture *fixture, gconstpointer unused)
{
    rstto_image_cache_set_preload_workers (fixture->cache, 5);
    for (guint capacity = 0; capacity <= 6; capacity++)
    {
        rstto_image_cache_clear (fixture->cache);
        RsttoCacheLimits limits = { TRUE, capacity, 256 * MIB, 128 * MIB };
        rstto_image_cache_set_limits (fixture->cache, &limits);
        select_image (fixture, 0);
        if (capacity >= 2)
            select_image (fixture, 1);
        candidates (fixture, 2, 5); /* Also test callers bypassing the neighbor helper. */
        drain (fixture);
        guint allowed = capacity > 2 ? capacity - 2 : 0;
        g_assert_cmpuint (g_hash_table_size (fixture->cache->targets), ==, allowed);
        g_assert_cmpuint (preload_worker_limit (fixture->cache), ==, allowed);
        g_assert_cmpuint (speculative_slots (fixture->cache), ==, allowed);
        g_assert_cmpuint (fixture->cache->resident_slots, ==, MAX (1, capacity));
        for (guint initial = 0; initial <= 1; initial++)
        {
            GArray *positions = rstto_image_cache_neighbor_positions (8, 7, 1, initial, 5, capacity, TRUE);
            g_assert_cmpuint (positions->len, ==, MIN (allowed, initial ? 4 : 5));
            for (guint i = 0; i < positions->len; i++)
            {
                guint value = g_array_index (positions, guint, i);
                g_assert_cmpuint (value, <, 8);
                g_assert_cmpuint (value, !=, 7);
                for (guint j = 0; j < i; j++)
                    g_assert_cmpuint (value, !=, g_array_index (positions, guint, j));
            }
            g_array_unref (positions);
        }
    }
    RsttoCacheLimits reduced = { TRUE, 3, 256 * MIB, 128 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &reduced);
    drain (fixture);
    g_assert_cmpuint (speculative_slots (fixture->cache), <=, 1);
    g_assert_cmpuint (g_hash_table_size (fixture->cache->targets), <=, 1);
}

static void
test_parallel_workers (Fixture *fixture, gconstpointer unused)
{
    fixture->enforce_limits = TRUE;
    rstto_image_cache_set_preload_workers (fixture->cache, 2);
    select_image (fixture, 0);
    start_barrier (NULL, DECODE);
    candidates (fixture, 1, 3);
    await_workers (fixture, 2);
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 2);
    g_assert_cmpuint (reserved_slots (fixture->cache), ==, 2);
    for (guint n = 0; n < 10; n++)
        iterate ();
    g_assert_cmpuint (g_atomic_int_get (&gate_entered), ==, 2);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 4);
    g_assert_cmpuint (fixture->cache->decodes, ==, 4);
    for (guint n = 0; n < 4; n++)
        select_image (fixture, n);
    g_assert_cmpuint (fixture->cache->decodes, ==, 4);
}

static void
test_parallel_foreground (Fixture *fixture, gconstpointer unused)
{
    fixture->enforce_limits = TRUE;
    rstto_image_cache_set_preload_workers (fixture->cache, 2);
    select_image (fixture, 0);
    start_barrier (NULL, DECODE);
    candidates (fixture, 1, 2);
    await_workers (fixture, 2);
    rstto_image_cache_request (fixture->cache, fixture->files[5], NULL, &full_size);
    candidates (fixture, 1, 2);
    await_workers (fixture, 3);
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 3);
    g_assert_cmpuint (speculative_jobs (fixture->cache), ==, 2);
    guint64 decodes = fixture->cache->decodes;
    /* Promote a preload while the obsolete foreground worker still owns its
     * slot and memory; it must not start a second decode of that image. */
    g_assert_cmpuint (speculative_slots (fixture->cache), ==, 2);
    rstto_image_cache_request (fixture->cache, fixture->files[2], NULL, &full_size);
    g_assert_cmpuint (speculative_slots (fixture->cache), ==, 1);
    candidates (fixture, 1, 1);
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 3);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    rstto_image_cache_set_preload_workers (fixture->cache, 1);
    g_assert_false (g_cancellable_is_cancelled (find_job (fixture, 2)->cancel));
    g_assert_true (g_cancellable_is_cancelled (find_job (fixture, 1)->cancel));
    release_barrier (fixture);
    g_assert_cmpuint (fixture->ready, ==, 2);
    g_assert_cmphex (fixture->pixel, ==, 0xff122030);
}

static void
test_parallel_memory (Fixture *fixture, gconstpointer unused)
{
    fixture->enforce_limits = TRUE;
    large_images (fixture);
    RsttoCacheLimits limits = { TRUE, 6, 34 * MIB, 8 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    rstto_image_cache_set_preload_workers (fixture->cache, 4);
    select_image (fixture, 0);
    start_barrier (NULL, DECODE);
    candidates (fixture, 1, 4);
    await_workers (fixture, 1);
    await_admission (fixture, 2);
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 2);
    g_assert_cmpuint (g_atomic_int_get (&gate_entered), ==, 1);
    g_assert_cmpuint (find_job (fixture, 2)->reservation, ==, 0);
    release_barrier (fixture);
    /* Retained images fit, concurrent reservations do not: wait and fill the
     * cache serially instead of dropping candidates after transient pressure. */
    g_assert_cmpuint (fixture->cache->decodes, ==, 5);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 5);
}

static void
test_parallel_probe_budget (Fixture *fixture, gconstpointer unused)
{
    fixture->enforce_limits = TRUE;
    large_images (fixture);
    save_image (fixture->files[2], 0x102030ff, "png", 32, 32);
    RsttoCacheLimits limits = { TRUE, 5, 21 * MIB, 8 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    rstto_image_cache_set_preload_workers (fixture->cache, 2);
    select_image (fixture, 0);
    start_barrier (NULL, DECODE);
    candidates (fixture, 1, 2);
    await_workers (fixture, 1);
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (find_job (fixture, 2) == NULL || find_job (fixture, 2)->phase != WAIT_PROBE)
    {
        iterate ();
        check_limits (fixture);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    g_assert_cmpuint (fixture->cache->resident_bytes + reserved_bytes (fixture->cache), ==, limits.max_bytes);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, 3);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 3);
}

static void
test_parallel_slots (Fixture *fixture, gconstpointer unused)
{
    fixture->enforce_limits = TRUE;
    RsttoCacheLimits limits = { TRUE, 4, 256 * MIB, 128 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    rstto_image_cache_set_preload_workers (fixture->cache, 8);
    select_image (fixture, 0);
    start_barrier (NULL, DECODE);
    candidates (fixture, 1, 4);
    await_workers (fixture, 2);
    g_assert_cmpuint (fixture->cache->resident_slots + reserved_slots (fixture->cache), ==, 3);
    g_assert_cmpuint (preload_worker_limit (fixture->cache), ==, 2);
    g_assert_cmpuint (speculative_slots (fixture->cache), ==, 2);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 3);
}

static void
test_parallel_live_workers (Fixture *fixture, gconstpointer unused)
{
    fixture->enforce_limits = TRUE;
    select_image (fixture, 0);
    start_barrier (NULL, DECODE);
    candidates (fixture, 1, 4);
    await_workers (fixture, 1);
    rstto_image_cache_set_preload_workers (fixture->cache, 3);
    await_workers (fixture, 3);
    GPtrArray *reordered = g_ptr_array_new ();
    for (guint n = 3; n > 0; n--)
        g_ptr_array_add (reordered, fixture->files[n]);
    g_ptr_array_add (reordered, fixture->files[4]);
    rstto_image_cache_set_candidates (fixture->cache, reordered);
    g_ptr_array_unref (reordered);
    guint64 reserved = reserved_bytes (fixture->cache);
    rstto_image_cache_set_preload_workers (fixture->cache, 1);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reserved);
    g_assert_cmpuint (speculative_jobs (fixture->cache), ==, 3);
    g_assert_false (g_cancellable_is_cancelled (find_job (fixture, 3)->cancel));
    g_assert_true (g_cancellable_is_cancelled (find_job (fixture, 2)->cancel));
    g_assert_true (g_cancellable_is_cancelled (find_job (fixture, 1)->cancel));
    rstto_image_cache_set_preload_workers (fixture->cache, 2);
    for (guint n = 0; n < 10; n++)
        iterate ();
    g_assert_cmpuint (g_atomic_int_get (&gate_entered), ==, 3);
    g_assert_null (find_job (fixture, 4)); /* Cancelled workers still occupy slots. */
    release_barrier (fixture);
    g_assert_cmpuint (fixture->errors, ==, 0);
}

static void
test_parallel_stop_waiting (Fixture *fixture, gconstpointer unused)
{
    fixture->enforce_limits = TRUE;
    large_images (fixture);
    RsttoCacheLimits limits = { TRUE, 5, 34 * MIB, 8 * MIB };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    rstto_image_cache_set_preload_workers (fixture->cache, 2);
    select_image (fixture, 0);
    start_barrier (NULL, DECODE);
    candidates (fixture, 1, 4);
    await_workers (fixture, 1);
    await_admission (fixture, 2);
    rstto_image_cache_stop (fixture->cache);
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 1);
    g_assert_cmpuint (reserved_bytes (fixture->cache), >, 0);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->ready, ==, 1);
    g_assert_cmpuint (fixture->cache->resident_bytes, ==, 0);
}

static void
test_parallel_limit_reduction (Fixture *fixture, gconstpointer unused)
{
    rstto_image_cache_set_preload_workers (fixture->cache, 3);
    select_image (fixture, 0);
    start_barrier (NULL, DECODE);
    candidates (fixture, 1, 4);
    await_workers (fixture, 3);
    guint64 reserved = reserved_bytes (fixture->cache);
    RsttoCacheLimits limits = { TRUE, 2, 64 * 1024, 64 * 1024 };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reserved);
    g_assert_cmpuint (speculative_jobs (fixture->cache), ==, 3);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    g_assert_cmpuint (fixture->cache->resident_bytes, <=, limits.max_bytes);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, 0);
}

static void
write_header (GFile *file, guint32 width, guint32 height)
{
    GBytes *bytes = png_header (width, height);
    gchar *path = g_file_get_path (file);
    gsize size;
    const gchar *contents = g_bytes_get_data (bytes, &size);
    g_assert_true (g_file_set_contents (path, contents, size, NULL));
    g_free (path);
    g_bytes_unref (bytes);
}

static void
test_probe_oversized (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    write_header (fixture->files[1], 50000, 50000);
    write_header (fixture->files[2], G_MAXINT, G_MAXINT);
    gint loaders = g_atomic_int_get (&pixbuf_loaders_created);
    candidates (fixture, 1, 2);
    drain (fixture);
    g_assert_cmpint (g_atomic_int_get (&pixbuf_loaders_created), ==, loaders);
    g_assert_cmpuint (fixture->cache->decodes, ==, 1);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, 0);
}

static void
cancel_metadata (DecodeJob *job)
{
    g_assert_cmpint (job->phase, ==, PROBE_INFO);
    g_assert_cmpuint (job->reservation, ==, PROBE_RESERVATION);
    g_cancellable_cancel (job->cancel);
    g_assert_true (job->running);
    g_assert_cmpuint (reserved_slots (job->owner), ==, 1);
    RsttoCacheLimits limits = { TRUE, 1, MIB, MIB };
    rstto_image_cache_set_limits (job->owner, &limits);
    g_assert_cmpuint (reserved_bytes (job->owner), ==, PROBE_RESERVATION);
}

static void
test_probe_metadata_cancel (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    metadata_started_hook = cancel_metadata;
    candidates (fixture, 1, 1);
    drain (fixture);
    metadata_started_hook = NULL;
    g_assert_cmpuint (fixture->cache->decodes, ==, 1);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, 0);
}

static void
cache_finalized (gpointer data, GObject *object)
{
    *(gboolean *) data = TRUE;
}

static void
abandon_metadata (DecodeJob *job)
{
    rstto_image_cache_stop (job->owner);
    g_object_unref (job->owner); /* Drop the client's last reference in flight. */
}

static void
test_probe_destroy (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    gboolean finalized = FALSE;
    g_object_weak_ref (G_OBJECT (fixture->cache), cache_finalized, &finalized);
    metadata_started_hook = abandon_metadata;
    candidates (fixture, 1, 1);
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (!finalized)
    {
        iterate ();
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    metadata_started_hook = NULL;
    fixture->cache = NULL;
    g_assert_cmpuint (fixture->ready, ==, 1);
}

static void
test_probe_changed (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    start_barrier (fixture->files[1], PROBE_VERIFY);
    candidates (fixture, 1, 1);
    await_barrier ();
    save_image (fixture->files[1], 0x882233ff, "png", 256, 192);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, 1);
    select_image (fixture, 1);
    g_assert_cmphex (fixture->pixel, ==, 0xff882233);
    g_assert_cmpuint (fixture->cache->displayed->image.width, ==, 256);
}

static void
test_probe_raw_fallback (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    gchar *old = g_file_get_path (fixture->files[1]);
    gchar *raw = g_strconcat (old, ".RAF", NULL);
    g_assert_cmpint (g_rename (old, raw), ==, 0);
    g_object_unref (fixture->files[1]);
    fixture->files[1] = g_file_new_for_path (raw);
    candidates (fixture, 1, 1);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, 1);
    select_image (fixture, 1); /* Existing generic foreground loading still works. */
    g_assert_cmphex (fixture->pixel, ==, 0xff112030);
    g_assert_false (fixture->cache->speculation_allowed);
    g_assert_null (fixture->cache->displayed->key);
    g_free (old);
    g_free (raw);
}

static void
test_probe_unavailable (Fixture *fixture, gconstpointer unused)
{
    select_image (fixture, 0);
    candidates (fixture, 1, 4);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, 1);
    g_assert_false (fixture->cache->speculation_allowed);
    g_assert_null (fixture->cache->displayed->key);
    g_assert_cmphex (fixture->pixel, ==, 0xff102030);
}

static gint
compare_int64 (gconstpointer a, gconstpointer b)
{
    gint64 aa = *(const gint64 *) a, bb = *(const gint64 *) b;
    return (aa > bb) - (aa < bb);
}

static gint
benchmark (gboolean caching, guint workers, gboolean full_resolution)
{
    Fixture fixture = { 0 };
    setup (&fixture, NULL);
    rstto_image_cache_set_preload_workers (fixture.cache, workers);
    gint width = full_resolution ? 7728 : 1920;
    gint height = full_resolution ? 5152 : 1280;
    /* Fixed synthetic photographs: gradients with deterministic fine noise. */
    for (guint n = 0; n < G_N_ELEMENTS (fixture.files); n++)
    {
        GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, width, height);
        guint32 random = 0x12345678 + n;
        for (gint y = 0; y < height; y++)
        {
            guchar *row = gdk_pixbuf_get_pixels (pixbuf) + y * gdk_pixbuf_get_rowstride (pixbuf);
            for (gint x = 0; x < width; x++)
            {
                random = random * 1664525 + 1013904223;
                row[x * 3] = ((x / 8 + (random >> 28)) & 255);
                row[x * 3 + 1] = ((y / 8 + (random >> 28)) & 255);
                row[x * 3 + 2] = (((x + y) / 16 + n * 8) & 255);
            }
        }
        gchar *path = g_file_get_path (fixture.files[n]);
        GError *error = NULL;
        gdk_pixbuf_save (pixbuf, path, "jpeg", &error, "quality", "90", NULL);
        g_assert_no_error (error);
        g_object_unref (pixbuf);
        g_free (path);
    }
    RsttoCacheLimits limits = { caching, 5, (full_resolution ? 2048 : 256) * MIB,
                               (full_resolution ? 256 : 128) * MIB };
    rstto_image_cache_set_limits (fixture.cache, &limits);
    const guint sequence[] = { 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0 };
    gint64 latencies[G_N_ELEMENTS (sequence)];
    guint64 peak_accounted = 0;
    gint64 preload_wait = 0, initial_preload = 0;
    guint64 render_hash = G_GUINT64_CONSTANT (14695981039346656037);
    for (guint n = 0; n < G_N_ELEMENTS (sequence); n++)
    {
        gint64 start = g_get_monotonic_time ();
        select_image (&fixture, sequence[n]);
        latencies[n] = g_get_monotonic_time () - start;
        /* Full-pixel comparison across identical enabled/disabled benchmark
         * runs, outside the timed navigation interval. No pixel copy. */
        cairo_surface_t *surface = fixture.cache->displayed->image.surface;
        g_assert_nonnull (surface);
        cairo_surface_flush (surface);
        g_assert_cmpint (cairo_image_surface_get_width (surface), ==, width);
        g_assert_cmpint (cairo_image_surface_get_height (surface), ==, height);
        for (gint y = 0; y < height; y++)
        {
            const guint32 *row = (const guint32 *) (cairo_image_surface_get_data (surface)
                                 + (gsize) y * cairo_image_surface_get_stride (surface));
            for (gint x = 0; x < width; x++)
                render_hash = (render_hash ^ row[x]) * G_GUINT64_CONSTANT (1099511628211);
        }
        peak_accounted = MAX (peak_accounted, fixture.peak_accounted);
        gint direction = n == 0 || sequence[n] > sequence[n - 1] ? 1 : -1;
        GArray *positions = rstto_image_cache_neighbor_positions (8, sequence[n], direction, n == 0, 2, 5, TRUE);
        GPtrArray *files = g_ptr_array_new ();
        for (guint j = 0; j < positions->len; j++)
            g_ptr_array_add (files, fixture.files[g_array_index (positions, guint, j)]);
        gint64 preload_start = g_get_monotonic_time ();
        rstto_image_cache_set_candidates (fixture.cache, files);
        g_ptr_array_unref (files);
        g_array_unref (positions);
        /* Model dwell time by waiting for speculative work outside the timed
         * navigation interval. This is not a continuous-scroll throughput test. */
        do
        {
            iterate ();
            guint64 accounted = fixture.cache->resident_bytes + reserved_bytes (fixture.cache);
            peak_accounted = MAX (peak_accounted, accounted);
            if (caching)
                g_assert_cmpuint (accounted, <=, limits.max_bytes);
        } while (fixture.cache->jobs.length != 0);
        gint64 elapsed = g_get_monotonic_time () - preload_start;
        preload_wait += elapsed;
        if (n == 0)
            initial_preload = elapsed;
    }
    qsort (latencies, G_N_ELEMENTS (latencies), sizeof latencies[0], compare_int64);
    struct rusage self, children;
    getrusage (RUSAGE_SELF, &self);
    getrusage (RUSAGE_CHILDREN, &children);
    g_print ("cache=%s workers=%u navigations=%zu decodes=%" G_GUINT64_FORMAT " hits=%" G_GUINT64_FORMAT
             " probe_mean_ms=%.3f median_ms=%.3f p95_ms=%.3f initial_preload_ms=%.3f total_preload_wait_ms=%.3f peak_accounted_mib=%.3f parent_peak_rss_mib=%.3f max_child_rss_mib=%.3f render_hash=%" G_GUINT64_FORMAT "\n",
             caching ? "on" : "off", workers, G_N_ELEMENTS (sequence), fixture.cache->decodes, fixture.cache->hits,
             fixture.cache->probes ? fixture.cache->probe_time_us / (1000.0 * fixture.cache->probes) : 0.0,
             latencies[G_N_ELEMENTS (latencies) / 2] / 1000.0,
             latencies[(G_N_ELEMENTS (latencies) * 95) / 100] / 1000.0,
             initial_preload / 1000.0, preload_wait / 1000.0,
             peak_accounted / (double) MIB, self.ru_maxrss / 1024.0, children.ru_maxrss / 1024.0, render_hash);
    teardown (&fixture, NULL);
    return 0;
}

static void
render_spec (RsttoImageCache *cache, GFile *file, RsttoImageData *image, RsttoRenderKey *key, RsttoRenderView *view)
{
    RsttoRenderGeometry geometry;
    g_assert_true (rstto_image_render_geometry (view, image->width, image->height, &geometry));
    g_assert_true (rstto_image_render_key (view, &geometry, image->width, image->decoded_width, key));
}

static RsttoRenderView
install_render_spec (Fixture *fixture, RsttoRenderView *view)
{
    *view = (RsttoRenderView) { .viewport_width = 64, .viewport_height = 48,
        .orientation = 1, .image_scale = 1, .pixel_scale = 1, .scale = 0, .smoothing = TRUE };
    g_signal_connect (fixture->cache, "prepare-render", G_CALLBACK (render_spec), view);
    fixture->enforce_limits = TRUE;
    return *view;
}

static void
test_render_reuse (Fixture *fixture, gconstpointer unused)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    fixture->cache->limits.max_images = 6;
    select_image (fixture, 0);
    g_assert_nonnull (fixture->cache->displayed->image.rendered);
    g_assert_cmpuint (fixture->cache->resident_bytes, ==, 128 * 96 * 4 + 64 * 48 * 4);
    candidates (fixture, 1, 4);
    drain (fixture);
    guint64 decodes = fixture->cache->decodes, renders = fixture->cache->renders;
    for (guint n = 0; n < 8; n++)
        select_image (fixture, (n + 1) % 2);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    g_assert_cmpuint (fixture->cache->renders, ==, renders);
    g_assert_cmpuint (fixture->cache->render_hits, >=, 8);
    view.viewport_width = 32; view.viewport_height = 24;
    select_image (fixture, 0);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    g_assert_cmpuint (fixture->cache->renders, ==, renders + 1);
    g_assert_cmpint (cairo_image_surface_get_width (fixture->cache->displayed->image.rendered), ==, 32);
}

static void
test_render_transaction (Fixture *fixture, gconstpointer unused)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    for (guint n = 0; n < 3; n++)
        select_image (fixture, n);
    guint64 before = fixture->cache->resident_bytes;
    g_assert_false (make_room (fixture->cache, fixture->cache->limits.max_bytes, 1, 1, 1));
    g_assert_cmpuint (fixture->cache->resident_bytes, ==, before);
    for (GList *lp = fixture->cache->lru.head; lp != NULL; lp = lp->next)
        g_assert_nonnull (((Entry *) lp->data)->image.rendered);
    RsttoCacheLimits limits = { TRUE, 5, 110 * 1024, 64 * 1024 };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 2);
    g_assert_cmpuint (fixture->cache->resident_bytes, <=, limits.max_bytes);
    g_assert_nonnull (fixture->cache->displayed->image.rendered);
    g_assert_null (previous_entry (fixture->cache)->image.rendered);
    /* No extra probe or decode is needed to fall back from a render that
     * cannot fit its three-surface reservation alongside the displayed image. */
    guint64 decodes = fixture->cache->decodes;
    select_image (fixture, 1);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    g_assert_null (fixture->cache->displayed->image.rendered);
}

static void
test_render_slots (Fixture *fixture, gconstpointer unused)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    fixture->cache->limits.max_images = 3;
    for (guint n = 0; n < 3; n++)
        select_image (fixture, n);
    Entry *previous = previous_entry (fixture->cache);
    g_assert_nonnull (previous->image.rendered);
    g_assert_true (make_room (fixture->cache, 0, 1, 0, 1));
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 2);
    g_assert_nonnull (previous->image.rendered);
    guint64 renders = fixture->cache->renders;
    select_image (fixture, 1);
    g_assert_cmpuint (fixture->cache->renders, ==, renders);
}

static void
test_render_promotion (Fixture *fixture, gconstpointer unused)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    select_image (fixture, 0);
    start_barrier (fixture->files[1], RENDER);
    candidates (fixture, 1, 1);
    await_barrier ();
    guint64 decodes = fixture->cache->decodes, renders = fixture->cache->renders;
    rstto_image_cache_request (fixture->cache, fixture->files[1], NULL, &full_size);
    candidates (fixture, 0, 0);
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 1);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    g_assert_cmpuint (fixture->cache->renders, ==, renders);
    g_assert_cmphex (fixture->pixel, ==, 0xff112030);
    g_assert_nonnull (fixture->cache->displayed->image.rendered);
}

static void
test_render_cancel (Fixture *fixture, gconstpointer unused)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    select_image (fixture, 0);
    start_barrier (fixture->files[1], RENDER);
    candidates (fixture, 1, 1);
    await_barrier ();
    g_assert_cmpuint (reserved_slots (fixture->cache), ==, 0); /* derivative shares source slot */
    g_assert_cmpuint (speculative_slots (fixture->cache), ==, 1);
    guint64 reservation = reserved_bytes (fixture->cache);
    g_assert_cmpuint (reservation, ==, 3 * 64 * 48 * 4);
    candidates (fixture, 0, 0);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reservation);
    g_assert_cmpuint (speculative_jobs (fixture->cache), ==, 1);
    release_barrier (fixture);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, 0);
    guint64 decodes = fixture->cache->decodes;
    select_image (fixture, 1);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    g_assert_nonnull (fixture->cache->displayed->image.rendered);
}

static void
test_render_changed (Fixture *fixture, gconstpointer unused)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    select_image (fixture, 0);
    start_barrier (fixture->files[1], RENDER);
    rstto_image_cache_request (fixture->cache, fixture->files[1], NULL, &full_size);
    await_barrier ();
    save_image (fixture->files[1], 0x887766ff, "png", 128, 96);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->errors, ==, 1);
    g_assert_null (fixture->cache->displayed->image.surface);
    select_image (fixture, 1);
    g_assert_cmphex (fixture->pixel, ==, 0xff887766);
}

static void
test_render_reduction (Fixture *fixture, gconstpointer unused)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    select_image (fixture, 0);
    start_barrier (fixture->files[1], RENDER);
    candidates (fixture, 1, 1);
    await_barrier ();
    guint64 reservation = reserved_bytes (fixture->cache);
    fixture->enforce_limits = FALSE; /* forced limits wait for pinned/cancelled resources */
    RsttoCacheLimits limits = { TRUE, 1, 60 * 1024, 60 * 1024 };
    rstto_image_cache_set_limits (fixture->cache, &limits);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reservation);
    g_assert_cmpuint (speculative_slots (fixture->cache), ==, 1);
    release_barrier (fixture);
    check_limits (fixture);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 1);
    g_assert_cmpuint (fixture->cache->resident_bytes, ==, 60 * 1024);
    g_assert_cmpuint (speculative_slots (fixture->cache), ==, 0);
}

static void
test_pending_selection (Fixture *fixture, gconstpointer phase)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    fixture->cache->limits.max_images = 6;
    rstto_image_cache_set_preload_workers (fixture->cache, 5);
    select_image (fixture, 0);
    start_barrier (NULL, GPOINTER_TO_UINT (phase));
    candidates (fixture, 1, 1);
    await_workers (fixture, 1);
    rstto_image_cache_request (fixture->cache, fixture->files[6], NULL, &full_size);
    await_workers (fixture, 2);
    DecodeJob *pending = find_job (fixture, 1);
    guint64 decodes = fixture->cache->decodes;
    guint64 reservation = reserved_bytes (fixture->cache);
    GPtrArray *neighbors = g_ptr_array_new ();
    /* A temporarily obsolete target is still reusable before it commits to stopping. */
    rstto_image_cache_set_candidates (fixture->cache, neighbors);
    g_assert_cmpint (g_atomic_int_get (&pending->interest), ==, 0);
    rstto_image_cache_set_schedule (fixture->cache, fixture->files[1], neighbors);
    g_assert_cmpint (g_atomic_int_get (&pending->interest), ==, 1);
    g_assert_false (g_cancellable_is_cancelled (pending->cancel));
    g_assert_true (find_job (fixture, 1) == pending);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reservation);
    g_assert_cmpuint (g_hash_table_size (fixture->cache->targets), ==, 1);
    release_barrier (fixture);
    guint64 renders = fixture->cache->renders, hits = fixture->cache->hits;
    select_image (fixture, 1);
    g_assert_cmpuint (fixture->cache->hits, ==, hits + 1);
    g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
    g_assert_cmpuint (fixture->cache->renders, ==, renders);
    g_assert_nonnull (fixture->cache->displayed->image.rendered);
    g_ptr_array_unref (neighbors);
}

static void
test_abandoned_promotion (Fixture *fixture, gconstpointer unused)
{
    fixture->enforce_limits = TRUE;
    select_image (fixture, 0);
    start_barrier (fixture->files[1], DECODE);
    candidates (fixture, 1, 1);
    await_barrier ();
    DecodeJob *job = find_job (fixture, 1);
    guint64 reservation = reserved_bytes (fixture->cache);
    candidates (fixture, 0, 0);
    g_assert_false (job_useful (job)); /* deterministic stopping checkpoint */
    g_assert_cmpint (g_atomic_int_get (&job->interest), ==, -1);
    rstto_image_cache_request (fixture->cache, fixture->files[1], NULL, &full_size);
    g_assert_false (g_atomic_int_get (&job->demand));
    g_assert_true (fixture->cache->foreground_pending);
    g_assert_cmpuint (fixture->cache->jobs.length, ==, 1);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reservation);
    release_barrier (fixture);
    g_assert_cmpuint (fixture->errors, ==, 0);
    g_assert_cmphex (fixture->pixel, ==, 0xff112030);
    g_assert_cmpuint (fixture->cache->decodes, ==, 3);
}

static void
test_render_history_bytes (Fixture *fixture, gconstpointer unused)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    fixture->cache->limits.max_images = 6;
    rstto_image_cache_set_preload_workers (fixture->cache, 5);
    select_image (fixture, 0);
    select_image (fixture, 1);
    Entry *previous = previous_entry (fixture->cache);
    guint64 retained = fixture->cache->resident_bytes;
    /* Admission could fit by stripping the previous derivative, which speculation must not do. */
    fixture->cache->limits.max_bytes = retained + PROBE_RESERVATION - 1;
    g_assert_false (make_room (fixture->cache, PROBE_RESERVATION, 1, 1, 1));
    g_assert_cmpuint (fixture->cache->resident_bytes, ==, retained);
    g_assert_nonnull (previous->image.rendered);
    for (guint n = 0; n < 8; n++)
    {
        guint64 hits = fixture->cache->hits, decodes = fixture->cache->decodes, renders = fixture->cache->renders;
        select_image (fixture, n % 2);
        history_candidates (fixture, n % 2);
        drain (fixture);
        g_assert_cmpuint (fixture->cache->hits, ==, hits + 1);
        g_assert_cmpuint (fixture->cache->decodes, ==, decodes);
        g_assert_cmpuint (fixture->cache->renders, ==, renders);
    }
}

static void
test_render_stripe_lifetime (Fixture *fixture, gconstpointer mode)
{
    RsttoRenderView view;
    install_render_spec (fixture, &view);
    view.viewport_width = 263; view.viewport_height = 901;
    fixture->cache->limits.max_images = 6;
    rstto_image_cache_set_preload_workers (fixture->cache, 5);
    select_image (fixture, 0);
    start_barrier (fixture->files[1], RENDER);
    rstto_image_cache_request (fixture->cache, fixture->files[1], NULL, &full_size);
    await_workers (fixture, 5);
    guint64 reservation = reserved_bytes (fixture->cache);
    g_assert_cmpuint (fixture->cache->active_tasks, ==, 5);
    g_assert_cmpuint (reservation, ==, 3 * 263 * 901 * 4);
    g_assert_cmpuint (fixture->cache->resident_slots + reserved_slots (fixture->cache), ==, 2);
    if (GPOINTER_TO_UINT (mode) == 1)
    {
        rstto_image_cache_invalidate (fixture->cache, fixture->files[1]);
        fixture->enforce_limits = FALSE; /* existing work drains above the lowered ceiling */
        rstto_image_cache_set_preload_workers (fixture->cache, 1);
    }
    else if (GPOINTER_TO_UINT (mode) == 2)
        rstto_image_cache_stop (fixture->cache);
    else if (GPOINTER_TO_UINT (mode) == 3)
    {
        fixture->enforce_limits = FALSE;
        RsttoCacheLimits limits = { TRUE, 2, 8 * MIB, 4 * MIB };
        rstto_image_cache_set_limits (fixture->cache, &limits);
    }
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, reservation);
    release_barrier (fixture);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, 0);
    g_assert_cmpuint (fixture->cache->active_tasks, ==, 0);
    g_assert_cmpuint (fixture->cache->peak_tasks, ==, 5);
    if (GPOINTER_TO_UINT (mode) != 2)
    {
        g_assert_nonnull (fixture->cache->displayed->image.rendered);
        g_assert_cmphex (fixture->pixel, ==, 0xff112030);
    }
}


int
main (int argc, char **argv)
{
    if (argc == 2 && (g_str_has_prefix (argv[1], "--benchmark=")
                      || g_str_has_prefix (argv[1], "--benchmark-x-t50=")))
    {
        gboolean full_resolution = g_str_has_prefix (argv[1], "--benchmark-x-t50=");
        const gchar *value = strchr (argv[1], '=') + 1;
        if (g_str_equal (value, "off"))
            return benchmark (FALSE, RSTTO_PRELOAD_WORKERS_DEFAULT, full_resolution);
        if (g_str_equal (value, "on"))
            return benchmark (TRUE, RSTTO_PRELOAD_WORKERS_DEFAULT, full_resolution);
        gchar *end;
        guint64 workers = g_ascii_strtoull (value, &end, 10);
        if (*end != '\0' || workers < 1 || workers > RSTTO_PRELOAD_WORKERS_MAX)
            return 2;
        return benchmark (TRUE, workers, full_resolution);
    }
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/cache/neighbors-and-overflow", test_neighbors);
#define ADD(name, func) g_test_add (name, Fixture, NULL, setup, func, teardown)
    if (!rstto_image_probe_available ())
    {
        ADD ("/cache/probe-unavailable", test_probe_unavailable);
        return g_test_run ();
    }
    ADD ("/cache/reuse", test_reuse);
    g_test_add ("/cache/pending-decode", Fixture, GUINT_TO_POINTER (DECODE), setup, test_pending_selection, teardown);
    g_test_add ("/cache/pending-render", Fixture, GUINT_TO_POINTER (RENDER), setup, test_pending_selection, teardown);
    g_test_add ("/cache/render/stripes", Fixture, NULL, setup, test_render_stripe_lifetime, teardown);
    g_test_add ("/cache/render/stripes-invalidation", Fixture, GUINT_TO_POINTER (1), setup, test_render_stripe_lifetime, teardown);
    g_test_add ("/cache/render/stripes-stop", Fixture, GUINT_TO_POINTER (2), setup, test_render_stripe_lifetime, teardown);
    g_test_add ("/cache/render/stripes-reduction", Fixture, GUINT_TO_POINTER (3), setup, test_render_stripe_lifetime, teardown);
    ADD ("/cache/abandoned-promotion", test_abandoned_promotion);
    ADD ("/cache/render/history-bytes", test_render_history_bytes);
    ADD ("/cache/render/reuse", test_render_reuse);
    ADD ("/cache/render/transaction", test_render_transaction);
    ADD ("/cache/render/slots", test_render_slots);
    ADD ("/cache/render/promotion", test_render_promotion);
    ADD ("/cache/render/cancellation", test_render_cancel);
    ADD ("/cache/render/freshness", test_render_changed);
    ADD ("/cache/render/live-reduction", test_render_reduction);
    ADD ("/cache/history-switch", test_history_switch);
    ADD ("/cache/history-memory", test_history_memory);
    ADD ("/cache/history-reversal", test_history_reversal);
    ADD ("/cache/history-capacity", test_history_capacity);
    ADD ("/cache/lru", test_lru);
    ADD ("/cache/weight", test_weight);
    ADD ("/cache/freshness", test_freshness);
    ADD ("/cache/symlink", test_symlink);
    ADD ("/cache/promotion", test_promotion);
    ADD ("/cache/cancellation", test_cancellation);
    ADD ("/cache/changed-during-decode", test_changed_during_decode);
    ADD ("/cache/stop", test_stop);
    ADD ("/cache/limits-and-pressure", test_limits_and_pressure);
    ADD ("/cache/formats-and-pixels", test_formats_and_pixels);
    ADD ("/cache/animation", test_animation);
    ADD ("/cache/reentrant-stop", test_reentrant_stop);
    ADD ("/cache/disabled-and-ephemeral", test_disabled_and_ephemeral);
    ADD ("/cache/foreground-limit-reduction", test_foreground_limit_reduction);
    ADD ("/cache/clear-during-load", test_clear_during_load);
    ADD ("/cache/probe-slot", test_probe_slot);
    ADD ("/cache/foreground-slot", test_foreground_slot);
    ADD ("/cache/replaced-format", test_replaced_format);
    ADD ("/cache/useful-preload", test_useful_preload);
    ADD ("/cache/parallel/workers", test_parallel_workers);
    ADD ("/cache/parallel/foreground", test_parallel_foreground);
    ADD ("/cache/parallel/memory", test_parallel_memory);
    ADD ("/cache/parallel/probe-budget", test_parallel_probe_budget);
    ADD ("/cache/parallel/slots", test_parallel_slots);
    ADD ("/cache/parallel/live-workers", test_parallel_live_workers);
    ADD ("/cache/parallel/stop-waiting", test_parallel_stop_waiting);
    ADD ("/cache/parallel/limit-reduction", test_parallel_limit_reduction);
    ADD ("/cache/admission-after-cancellation", test_admission_after_cancellation);
    ADD ("/cache/unsuitable-probe", test_unsuitable_probe);
    ADD ("/cache/probe/oversized-without-decoding", test_probe_oversized);
    ADD ("/cache/probe/metadata-cancellation", test_probe_metadata_cancel);
    ADD ("/cache/probe/destroy-during-metadata", test_probe_destroy);
    ADD ("/cache/probe/changed-before-admission", test_probe_changed);
    ADD ("/cache/probe/raw-foreground-fallback", test_probe_raw_fallback);
    return g_test_run ();
}

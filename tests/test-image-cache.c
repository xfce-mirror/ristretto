/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Include the implementation to exercise private accounting and deterministic
 * worker barriers without adding test hooks to the application API. */
#define RSTTO_CACHE_TEST
#include "../src/image_cache.c"

#include <fcntl.h>
#include <gdk/gdk.h>
#include <sys/resource.h>
#include <unistd.h>

typedef struct
{
    gchar *directory;
    GFile *files[8];
    RsttoImageCache *cache;
    guint ready, errors, invalidations;
    guint32 pixel;
    guint64 peak_accounted;
} Fixture;

static RsttoDecodeOptions full_size = { FALSE, 0, 0 };
static GMutex gate_mutex;
static GCond gate_cond;
static gint gate_entered;
static gboolean gate_released;
static JobPhase gate_phase;
static gchar *gate_uri;

static void
received (RsttoImageCache *cache, RsttoImageData *image, Fixture *fixture)
{
    fixture->ready++;
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
        g_assert_cmpuint (fixture->cache->jobs.length, <=, 2);
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
    g_signal_connect (fixture->cache, "ready", G_CALLBACK (received), fixture);
    g_signal_connect (fixture->cache, "identity-changed", G_CALLBACK (invalidated), fixture);
}

static void
teardown (Fixture *fixture, gconstpointer unused)
{
    rstto_image_cache_stop (fixture->cache);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->resident_bytes, ==, 0);
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, 0);
    g_object_unref (fixture->cache);
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
    candidates (fixture, 1, 4);
    drain (fixture);
    g_assert_cmpuint (fixture->cache->decodes, ==, 5);
    g_assert_cmpuint (fixture->cache->resident_slots, ==, 5);
    for (guint n = 0; n < 5; n++)
        select_image (fixture, n);
    for (gint n = 4; n >= 0; n--)
        select_image (fixture, n);
    g_assert_cmpuint (fixture->cache->decodes, ==, 5);
    g_assert_cmpuint (fixture->cache->hits, ==, 10);
    g_assert_cmpuint (fixture->errors, ==, 0);
}

static void
test_lru (Fixture *fixture, gconstpointer unused)
{
    RsttoCacheLimits limits = { TRUE, 3, 2 * MIB, MIB };
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
test_weight (Fixture *fixture, gconstpointer unused)
{
    RsttoCacheLimits limits = { TRUE, 5, MIB + 300 * 1024, MIB };
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
    if (job->phase != gate_phase || !g_str_equal (job->uri, gate_uri))
        return;
    g_mutex_lock (&gate_mutex);
    g_atomic_int_set (&gate_entered, TRUE);
    while (!gate_released)
        g_cond_wait (&gate_cond, &gate_mutex);
    g_mutex_unlock (&gate_mutex);
}

static void
start_barrier (GFile *file, JobPhase phase)
{
    gate_uri = g_file_get_uri (file);
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
    limits = (RsttoCacheLimits) { TRUE, 5, 2 * MIB, MIB };
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
    assert_positions (10, 4, 1, TRUE, FALSE, (guint[]) { 5, 6, 3, 7 }, 4);
    assert_positions (10, 4, -1, TRUE, FALSE, (guint[]) { 3, 2, 5, 1 }, 4);
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
    g_assert_cmpuint (reserved_bytes (fixture->cache), ==, PROBE_BUFFER_SIZE);
    rstto_image_cache_set_candidates (fixture->cache, &(GPtrArray) { 0 });
    g_assert_cmpuint (reserved_slots (fixture->cache), ==, 1);
    release_barrier (fixture);
    g_assert_cmpuint (reserved_slots (fixture->cache), ==, 0);
}

static void
test_admission_after_cancellation (Fixture *fixture, gconstpointer unused)
{
    RsttoCacheLimits limits = { TRUE, 5, MIB + 400 * 1024, MIB };
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

static gint
compare_int64 (gconstpointer a, gconstpointer b)
{
    gint64 aa = *(const gint64 *) a, bb = *(const gint64 *) b;
    return (aa > bb) - (aa < bb);
}

static gint
benchmark (gboolean caching)
{
    Fixture fixture = { 0 };
    setup (&fixture, NULL);
    /* Fixed synthetic photographs: gradients with deterministic fine noise. */
    for (guint n = 0; n < G_N_ELEMENTS (fixture.files); n++)
    {
        GdkPixbuf *pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 1920, 1280);
        guint32 random = 0x12345678 + n;
        for (gint y = 0; y < 1280; y++)
        {
            guchar *row = gdk_pixbuf_get_pixels (pixbuf) + y * gdk_pixbuf_get_rowstride (pixbuf);
            for (gint x = 0; x < 1920; x++)
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
    RsttoCacheLimits limits = { caching, 5, 256 * MIB, 128 * MIB };
    rstto_image_cache_set_limits (fixture.cache, &limits);
    const guint sequence[] = { 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0 };
    gint64 latencies[G_N_ELEMENTS (sequence)];
    guint64 peak_accounted = 0;
    for (guint n = 0; n < G_N_ELEMENTS (sequence); n++)
    {
        gint64 start = g_get_monotonic_time ();
        select_image (&fixture, sequence[n]);
        latencies[n] = g_get_monotonic_time () - start;
        peak_accounted = MAX (peak_accounted, fixture.peak_accounted);
        gint direction = n == 0 || sequence[n] > sequence[n - 1] ? 1 : -1;
        GArray *positions = rstto_image_cache_neighbor_positions (8, sequence[n], direction, n == 0, 2, 5, TRUE);
        GPtrArray *files = g_ptr_array_new ();
        for (guint j = 0; j < positions->len; j++)
            g_ptr_array_add (files, fixture.files[g_array_index (positions, guint, j)]);
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
    }
    qsort (latencies, G_N_ELEMENTS (latencies), sizeof latencies[0], compare_int64);
    struct rusage self, children;
    getrusage (RUSAGE_SELF, &self);
    getrusage (RUSAGE_CHILDREN, &children);
    g_print ("cache=%s navigations=%zu decodes=%" G_GUINT64_FORMAT " hits=%" G_GUINT64_FORMAT
             " median_ms=%.3f p95_ms=%.3f peak_accounted_mib=%.3f parent_peak_rss_mib=%.3f max_child_rss_mib=%.3f\n",
             caching ? "on" : "off", G_N_ELEMENTS (sequence), fixture.cache->decodes, fixture.cache->hits,
             latencies[G_N_ELEMENTS (latencies) / 2] / 1000.0,
             latencies[(G_N_ELEMENTS (latencies) * 95) / 100] / 1000.0,
             peak_accounted / (double) MIB, self.ru_maxrss / 1024.0, children.ru_maxrss / 1024.0);
    teardown (&fixture, NULL);
    return 0;
}

int
main (int argc, char **argv)
{
    if (argc == 2 && g_str_has_prefix (argv[1], "--benchmark="))
        return benchmark (g_str_equal (argv[1], "--benchmark=on"));
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/cache/neighbors-and-overflow", test_neighbors);
#define ADD(name, func) g_test_add (name, Fixture, NULL, setup, func, teardown)
    ADD ("/cache/reuse", test_reuse);
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
    ADD ("/cache/admission-after-cancellation", test_admission_after_cancellation);
    ADD ("/cache/unsuitable-probe", test_unsuitable_probe);
    return g_test_run ();
}

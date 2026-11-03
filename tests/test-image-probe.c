/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Including the private implementation permits configuration tests without
 * mutating process-wide environment variables after Glycin starts threads. */
#include "../src/image_probe.c"
#include <glib/gstdio.h>
#include "probe-fixtures.h"

typedef struct
{
    gboolean done, valid;
    gint width, height;
} ProbeResult;

static void
finished (GObject *source, GAsyncResult *result, gpointer data)
{
    ProbeResult *probe = data;
    probe->valid = rstto_image_probe_finish (result, &probe->width, &probe->height);
    probe->done = TRUE;
}

static ProbeResult
probe_bytes (GBytes *bytes, gboolean cancel)
{
    ProbeResult result = { 0 };
    GCancellable *cancellable = g_cancellable_new ();
    rstto_image_probe_async (bytes, cancellable, finished, &result);
    if (cancel)
        g_cancellable_cancel (cancellable);
    gint64 until = g_get_monotonic_time () + 10 * G_USEC_PER_SEC;
    while (!result.done)
    {
        g_main_context_iteration (NULL, FALSE);
        g_usleep (1000);
        g_assert_cmpint (g_get_monotonic_time (), <, until);
    }
    g_object_unref (cancellable);
    return result;
}

static void
test_dimensions (void)
{
    if (!rstto_image_probe_available ())
    {
        g_test_skip ("Glycin probing disabled at build time");
        return;
    }
    GBytes *bytes = png_header (7728, 5152);
    ProbeResult result = probe_bytes (bytes, FALSE);
    g_assert_true (result.valid);
    g_assert_cmpint (result.width, ==, 7728);
    g_assert_cmpint (result.height, ==, 5152);
    g_bytes_unref (bytes);
    /* 10 GB of RGBA pixels, represented by only 41 input bytes. */
    bytes = png_header (50000, 50000);
    result = probe_bytes (bytes, FALSE);
    g_assert_true (result.valid);
    g_assert_cmpint (result.width, ==, 50000);
    g_assert_cmpint (result.height, ==, 50000);
    g_bytes_unref (bytes);
}

static void
test_failures (void)
{
    GBytes *junk = g_bytes_new_static ("not an image", 12);
    g_assert_false (probe_bytes (junk, FALSE).valid);
    g_assert_false (probe_bytes (junk, TRUE).valid);
    g_bytes_unref (junk);
    GBytes *bytes = png_header (1, 1);
    GBytes *truncated = g_bytes_new_from_bytes (bytes, 0, 15);
    g_assert_false (probe_bytes (truncated, FALSE).valid);
    g_assert_false (probe_bytes (bytes, TRUE).valid);
    g_bytes_unref (truncated);
    g_bytes_unref (bytes);
    bytes = png_header (0, 1);
    g_assert_false (probe_bytes (bytes, FALSE).valid);
    g_bytes_unref (bytes);
    bytes = png_header (G_MAXUINT32, 2);
    g_assert_false (probe_bytes (bytes, FALSE).valid);
    g_bytes_unref (bytes);
}

static void
test_prefix_limit (void)
{
    GBytes *bytes = g_bytes_new_take (g_malloc0 (RSTTO_PROBE_LIMIT + 100), RSTTO_PROBE_LIMIT + 100);
    GInputStream *stream = g_memory_input_stream_new_from_bytes (bytes);
    GCancellable *cancel = g_cancellable_new ();
    GBytes *prefix = rstto_image_probe_read (stream, cancel);
    g_assert_cmpuint (g_bytes_get_size (prefix), ==, RSTTO_PROBE_LIMIT);
    g_assert_cmpint (g_seekable_tell (G_SEEKABLE (stream)), ==, RSTTO_PROBE_LIMIT);
    g_bytes_unref (prefix);
    g_cancellable_cancel (cancel);
    g_assert_null (rstto_image_probe_read (stream, cancel));
    g_object_unref (cancel);
    g_object_unref (stream);
    g_bytes_unref (bytes);
}

static void
test_raw (void)
{
    GBytes *png = png_header (1, 1);
    const gchar *names[] = { "picture.RAF", "picture.CR2", "picture.dng", "picture.NEF", "picture.arw" };
    for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
        GFile *file = g_file_new_for_path (names[i]);
        g_assert_true (rstto_image_probe_is_raw (file, png, NULL));
        g_object_unref (file);
    }
    GFile *file = g_file_new_for_path ("picture.png");
    g_assert_false (rstto_image_probe_is_raw (file, png, NULL));
    const gchar *configured[] = { "image/png", NULL };
    g_assert_true (rstto_image_probe_is_raw (file, png, configured));
    GBytes *raf = g_bytes_new_static ("FUJIFILMCCD-RAW ", 16);
    g_assert_true (rstto_image_probe_is_raw (file, raf, NULL));
    g_bytes_unref (raf);
    g_bytes_unref (png);
    g_object_unref (file);
}

static void
test_raw_configuration (void)
{
    gchar *directory = g_dir_make_tmp ("ristretto-probe-config-XXXXXX", NULL);
    gchar *confdir = g_build_filename (directory, "glycin-loaders", "2+", "conf.d", NULL);
    g_assert_cmpint (g_mkdir_with_parents (confdir, 0700), ==, 0);
    gchar *config = g_build_filename (confdir, "custom.conf", NULL);
    g_assert_true (g_file_set_contents (config,
        "[loader:image/x-custom-raw]\nExec=/custom/path/glycin-raw\n"
        "[loader:image/png]\nExec=/usr/libexec/glycin-image-rs\n"
        "[editor:image/jpeg]\nExec=/custom/path/glycin-raw\n", -1, NULL));
    GHashTable *types = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
    collect_raw_types (directory, types);
    g_assert_cmpuint (g_hash_table_size (types), ==, 1);
    g_assert_true (g_hash_table_contains (types, "image/x-custom-raw"));
    g_hash_table_unref (types);
    g_remove (config);
    g_free (config);
    g_rmdir (confdir);
    g_free (confdir);
    confdir = g_build_filename (directory, "glycin-loaders", "2+", NULL);
    g_rmdir (confdir);
    g_free (confdir);
    confdir = g_build_filename (directory, "glycin-loaders", NULL);
    g_rmdir (confdir);
    g_free (confdir);
    g_rmdir (directory);
    g_free (directory);
}

static void
test_missing_loader (void)
{
    if (g_test_subprocess ())
    {
        gchar *directory = g_dir_make_tmp ("ristretto-no-loader-XXXXXX", NULL);
        g_setenv ("GLYCIN_DATA_DIR", directory, TRUE);
        GBytes *bytes = png_header (7728, 5152);
        g_assert_false (probe_bytes (bytes, FALSE).valid);
        g_bytes_unref (bytes);
        g_rmdir (directory);
        g_free (directory);
        return;
    }
    g_test_trap_subprocess (NULL, 10 * G_USEC_PER_SEC, 0);
    g_test_trap_assert_passed ();
}

int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    g_test_add_func ("/probe/dimensions-without-pixels", test_dimensions);
    g_test_add_func ("/probe/failures-and-cancellation", test_failures);
    g_test_add_func ("/probe/prefix-limit", test_prefix_limit);
    g_test_add_func ("/probe/raw-classification", test_raw);
    g_test_add_func ("/probe/raw-configuration", test_raw_configuration);
    g_test_add_func ("/probe/missing-loader", test_missing_loader);
    return g_test_run ();
}

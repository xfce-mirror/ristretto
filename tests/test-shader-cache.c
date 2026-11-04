/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../src/image_shader_cache.h"

#include <fcntl.h>
#include <glib/gstdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
static const gchar *fingerprint = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";


/**
 * remove_tree:
 *
 * Remove an isolated test cache directory recursively after all file handles
 * have closed.
 */
static void
remove_tree (const gchar *path)
{
    GDir *dir = g_dir_open (path, 0, NULL);
    const gchar *name;
    if (dir != NULL)
    {
        while ((name = g_dir_read_name (dir)) != NULL)
        {
            gchar *p = g_build_filename (path, name, NULL);
            if (g_file_test (p, G_FILE_TEST_IS_DIR))
                remove_tree (p);
            else
                g_unlink (p);
            g_free (p);
        }
        g_dir_close (dir);
    }
    g_rmdir (path);
}


/**
 * persistence:
 *
 * Exercise shader-cache round trips and the selected corruption, cancellation,
 * locking or filesystem failure case.
 */
static void
persistence (gconstpointer mode_ptr)
{
    guint mode = GPOINTER_TO_UINT (mode_ptr);
    gchar *root = g_dir_make_tmp ("ristretto-shaders-XXXXXX", NULL);
    GCancellable *cancel = g_cancellable_new ();
    RsttoShaderCache *cache = rstto_shader_cache_new (fingerprint, root, cancel);
    const gchar payload[] = "compiled program bytes";
    rstto_shader_cache_save (cache, 42, payload, sizeof payload);
    gchar *path = g_build_filename (root, fingerprint, "000000000000002a.bin", NULL);
    struct stat st;
    g_assert_cmpint (g_stat (path, &st), ==, 0);
    g_assert_cmpuint (st.st_mode & 0777, ==, 0600);
    gsize size;
    gchar *bytes = rstto_shader_cache_load (cache, 42, &size);
    g_assert_cmpmem (bytes, size, payload, sizeof payload);
    g_free (bytes);
    if (mode == 0)
    {
        rstto_shader_cache_free (cache);
        cache = rstto_shader_cache_new (fingerprint, root, cancel);
        bytes = rstto_shader_cache_load (cache, 42, &size);
        g_assert_cmpmem (bytes, size, payload, sizeof payload);
        g_free (bytes);
        RsttoShaderCache *other = rstto_shader_cache_new ("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", root, cancel);
        g_assert_null (rstto_shader_cache_load (other, 42, &size));
        rstto_shader_cache_free (other);
    }
    else if (mode <= 3)
    {
        g_file_get_contents (path, &bytes, &size, NULL);
        if (mode == 1)
            bytes[size - 1] ^= 1; /* checksum */
        if (mode == 2)
            memset (bytes + 16, 255, 8); /* hostile encoded allocation length */
        if (mode == 3)
            size = 20; /* truncated header */
        g_file_set_contents (path, bytes, size, NULL);
        g_free (bytes);
        g_assert_null (rstto_shader_cache_load (cache, 42, &size));
        rstto_shader_cache_save (cache, 42, payload, sizeof payload);
        bytes = rstto_shader_cache_load (cache, 42, &size);
        g_assert_nonnull (bytes);
        g_free (bytes);
    }
    else if (mode == 4)
    {
        g_cancellable_cancel (cancel);
        g_assert_null (rstto_shader_cache_load (cache, 42, &size));
        rstto_shader_cache_save (cache, 43, payload, sizeof payload);
        guint64 hits, misses, writes;
        rstto_shader_cache_stats (cache, &hits, &misses, &writes);
        g_assert_cmpuint (writes, ==, 1);
    }
    else if (mode == 5)
    {
        gchar *lockpath = g_build_filename (root, ".lock", NULL);
        gint fd = g_open (lockpath, O_RDWR, 0);
        g_assert_cmpint (flock (fd, LOCK_EX | LOCK_NB), ==, 0);
        rstto_shader_cache_save (cache, 43, payload, sizeof payload);
        guint64 hits, misses, writes;
        rstto_shader_cache_stats (cache, &hits, &misses, &writes);
        g_assert_cmpuint (writes, ==, 1);
        close (fd);
        g_free (lockpath);
    }
    else if (mode == 6)
    {
        gchar *ns = g_build_filename (root, fingerprint, NULL);
        g_chmod (ns, 0500);
        rstto_shader_cache_save (cache, 43, payload, sizeof payload);
        guint64 hits, misses, writes;
        rstto_shader_cache_stats (cache, &hits, &misses, &writes);
        g_assert_cmpuint (writes, ==, 1);
        g_chmod (ns, 0700);
        g_free (ns);
    }
    else if (mode >= 8)
    {
        g_unlink (path);
        if (mode == 8)
            g_assert_cmpint (mkfifo (path, 0600), ==, 0);
        else
            g_assert_cmpint (symlink ("/dev/zero", path), ==, 0);
        g_assert_null (rstto_shader_cache_load (cache, 42, &size));
    }
    else if (mode == 7)
    {
        pid_t child = fork ();
        g_assert_cmpint (child, >=, 0);
        for (guint i = 0; i < 20; i++)
            rstto_shader_cache_save (cache, i + 100, payload, sizeof payload);
        if (child == 0)
            _exit (0);
        gint status;
        g_assert_cmpint (waitpid (child, &status, 0), ==, child);
        g_assert_true (WIFEXITED (status));
        for (guint i = 0; i < 20; i++)
        {
            bytes = rstto_shader_cache_load (cache, i + 100, &size);
            if (bytes != NULL)
                g_assert_cmpmem (bytes, size, payload, sizeof payload);
            g_free (bytes);
        }
    }
    rstto_shader_cache_free (cache);
    g_object_unref (cancel);
    g_free (path);
    remove_tree (root);
    g_free (root);
}


/**
 * pruning:
 *
 * Require oversized objects to be rejected and retained entries to stay within
 * both disk bounds.
 */
static void
pruning (void)
{
    gchar *root = g_dir_make_tmp ("ristretto-shaders-size-XXXXXX", NULL);
    RsttoShaderCache *c = rstto_shader_cache_new (fingerprint, root, NULL);
    guchar *data = g_malloc0 (RSTTO_SHADER_OBJECT_BYTES + 1);
    rstto_shader_cache_save (c, 999, data, RSTTO_SHADER_OBJECT_BYTES + 1);
    gsize size;
    g_assert_null (rstto_shader_cache_load (c, 999, &size));
    for (guint i = 0; i < 20; i++)
        rstto_shader_cache_save (c, i, data, RSTTO_SHADER_OBJECT_BYTES);
    for (guint i = 100; i < 240; i++)
        rstto_shader_cache_save (c, i, data, 1);
    gchar *ns = g_build_filename (root, fingerprint, NULL);
    GDir *dir = g_dir_open (ns, 0, NULL);
    const gchar *name;
    guint count = 0;
    guint64 total = 0;
    while ((name = g_dir_read_name (dir)) != NULL)
    {
        gchar *p = g_build_filename (ns, name, NULL);
        struct stat st;
        g_stat (p, &st);
        total += st.st_size;
        count++;
        g_free (p);
    }
    g_assert_cmpuint (total, <=, RSTTO_SHADER_DISK_BYTES);
    g_assert_cmpuint (count, <=, RSTTO_SHADER_DISK_ENTRIES);
    g_dir_close (dir);
    g_free (ns);
    g_free (data);
    rstto_shader_cache_free (c);
    remove_tree (root);
    g_free (root);
}


/**
 * main:
 *
 * Register shader-cache persistence and bounded-pruning regression cases.
 */
int
main (int argc, char **argv)
{
    g_test_init (&argc, &argv, NULL);
    const gchar *names[] = { "roundtrip-namespace", "checksum", "oversized-length", "truncated", "cancellation", "lock-contention", "unwritable", "concurrent-writers", "fifo", "symlink" };
    for (guint i = 0; i < G_N_ELEMENTS (names); i++)
    {
        gchar *name = g_strdup_printf ("/shader-cache/%s", names[i]);
        g_test_add_data_func (name, GUINT_TO_POINTER (i), persistence);
        g_free (name);
    }
    g_test_add_func ("/shader-cache/pruning", pruning);
    return g_test_run ();
}

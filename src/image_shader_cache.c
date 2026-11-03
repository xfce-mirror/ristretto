/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "image_shader_cache.h"
#include "profile.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEADER_SIZE 56
struct _RsttoShaderCache {
    gchar *root, *directory;
    GCancellable *cancel;
    guint64 hits, misses, writes;
};
static gboolean stopped (RsttoShaderCache *c) { return g_cancellable_is_cancelled (c->cancel); }
static gboolean hex_name (const gchar *s, guint length) {
    if (strlen (s) != length) return FALSE;
    for (guint i = 0; i < length; i++) if (!g_ascii_isxdigit (s[i]) || g_ascii_isupper (s[i])) return FALSE;
    return TRUE;
}
static gboolean transfer (RsttoShaderCache *c, gint fd, gpointer data, gsize size, gboolean writing) {
    guchar *p = data;
    while (size != 0 && !stopped (c)) {
        ssize_t n = writing ? write (fd, p, MIN (size, 4096)) : read (fd, p, MIN (size, 4096));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return FALSE;
        p += n; size -= n;
    }
    return size == 0;
}
static void digest (gconstpointer data, gsize size, guchar output[32]) {
    GChecksum *sum = g_checksum_new (G_CHECKSUM_SHA256);
    g_checksum_update (sum, data, size);
    gsize length = 32; g_checksum_get_digest (sum, output, &length); g_checksum_free (sum);
}
static gboolean checksum_file (RsttoShaderCache *c, gint fd, gsize size, const guchar expected[32]) {
    GChecksum *sum = g_checksum_new (G_CHECKSUM_SHA256);
    guchar buffer[4096], hash[32]; gboolean ok = TRUE;
    while (size != 0 && ok) {
        gsize n = MIN (size, sizeof buffer);
        ok = transfer (c, fd, buffer, n, FALSE);
        if (ok) g_checksum_update (sum, buffer, n);
        size -= n;
    }
    gsize length = sizeof hash; g_checksum_get_digest (sum, hash, &length); g_checksum_free (sum);
    return ok && memcmp (hash, expected, sizeof hash) == 0;
}
static gchar *filename (RsttoShaderCache *c, guint64 key) {
    return g_strdup_printf ("%s/%016" G_GINT64_MODIFIER "x.bin", c->directory, key);
}
RsttoShaderCache *rstto_shader_cache_new (const gchar *fingerprint, const gchar *root_override, GCancellable *cancel) {
    g_return_val_if_fail (hex_name (fingerprint, 64), NULL);
    RsttoShaderCache *c = g_new0 (RsttoShaderCache, 1);
    const gchar *base = g_get_user_cache_dir ();
    gchar *fallback = NULL;
    if (!g_path_is_absolute (base)) base = fallback = g_build_filename (g_get_home_dir (), ".cache", NULL);
    c->root = root_override != NULL ? g_strdup (root_override) : g_build_filename (base, "ristretto", "shaders", NULL);
    c->directory = g_build_filename (c->root, fingerprint, NULL);
    c->cancel = cancel != NULL ? g_object_ref (cancel) : g_cancellable_new ();
    g_free (fallback);
    return c;
}
void rstto_shader_cache_free (RsttoShaderCache *c) {
    if (c == NULL) return;
    g_object_unref (c->cancel); g_free (c->directory); g_free (c->root); g_free (c);
}
gpointer rstto_shader_cache_load (RsttoShaderCache *c, guint64 key, gsize *size) {
    gint64 start = rstto_profile_now (); *size = 0;
    gchar *path = filename (c, key);
    gint fd = g_open (path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0);
    guchar header[HEADER_SIZE], *data = NULL, hash[32]; struct stat st;
    guint64 stored_key = 0, length = 0;
    if (fd < 0) goto done;
    if (fstat (fd, &st) != 0 || !S_ISREG (st.st_mode) || st.st_uid != getuid ()
        || st.st_size < HEADER_SIZE || (guint64) st.st_size > HEADER_SIZE + RSTTO_SHADER_OBJECT_BYTES
        || !transfer (c, fd, header, sizeof header, FALSE) || memcmp (header, "RSTGLC01", 8) != 0) goto done;
    memcpy (&stored_key, header + 8, 8); memcpy (&length, header + 16, 8);
    stored_key = GUINT64_FROM_LE (stored_key); length = GUINT64_FROM_LE (length);
    if (stored_key != key || length == 0 || length > RSTTO_SHADER_OBJECT_BYTES
        || length != (guint64) st.st_size - HEADER_SIZE || !checksum_file (c, fd, length, header + 24)) goto done;
    /* Validate the bounded payload before allocating it. Recheck the copied
     * bytes too, so in-place modifications cannot slip past the first pass. */
    if (lseek (fd, HEADER_SIZE, SEEK_SET) < 0) goto done;
    data = g_malloc (length);
    if (!transfer (c, fd, data, length, FALSE)) { g_clear_pointer (&data, g_free); goto done; }
    digest (data, length, hash);
    if (memcmp (hash, header + 24, 32) != 0) { g_clear_pointer (&data, g_free); goto done; }
    *size = length;
    /* Last successful use drives pruning, without rewriting payload bytes. */
    futimens (fd, NULL);
done:
    if (fd >= 0) close (fd);
    if (data != NULL) c->hits++; else c->misses++;
    rstto_profile_end (start, c, key, "shader-cache-load", "hit=%d bytes=%" G_GSIZE_FORMAT, data != NULL, *size);
    g_free (path); return data;
}

typedef struct { gchar relative[87]; guint64 size; gint64 modified; } DiskEntry;
typedef struct { DiskEntry entries[RSTTO_SHADER_DISK_ENTRIES + 1]; guint count; guint64 bytes; } Inventory;
static gboolean prune (RsttoShaderCache *c, Inventory *items, guint64 extra, const gchar *protect) {
    while (items->count + 1 > RSTTO_SHADER_DISK_ENTRIES || items->bytes + extra > RSTTO_SHADER_DISK_BYTES) {
        guint oldest = G_MAXUINT;
        for (guint i = 0; i < items->count; i++)
            if (g_strcmp0 (items->entries[i].relative, protect) != 0
                && (oldest == G_MAXUINT || items->entries[i].modified < items->entries[oldest].modified)) oldest = i;
        if (oldest == G_MAXUINT) return FALSE;
        gchar *path = g_build_filename (c->root, items->entries[oldest].relative, NULL);
        gboolean ok = g_unlink (path) == 0 || errno == ENOENT; g_free (path);
        if (!ok) return FALSE;
        items->bytes -= items->entries[oldest].size;
        items->entries[oldest] = items->entries[--items->count];
    }
    return TRUE;
}
static gboolean room (RsttoShaderCache *c, guint64 extra, const gchar *protect) {
    Inventory items = { 0 };
    GDir *root = g_dir_open (c->root, 0, NULL);
    if (root == NULL) return FALSE;
    const gchar *namespace; gboolean ok = TRUE;
    while (ok && !stopped (c) && (namespace = g_dir_read_name (root)) != NULL) {
        if (!hex_name (namespace, 64)) continue;
        gchar *path = g_build_filename (c->root, namespace, NULL); struct stat st;
        GDir *dir = g_lstat (path, &st) == 0 && S_ISDIR (st.st_mode) && st.st_uid == getuid () ? g_dir_open (path, 0, NULL) : NULL;
        if (dir != NULL) {
            const gchar *name;
            while (ok && (name = g_dir_read_name (dir)) != NULL) {
                gchar key[17];
                if (g_str_has_prefix (name, ".tmp-")) {
                    gchar *temporary = g_build_filename (path, name, NULL);
                    /* The lock excludes live writers; these are crash leftovers. */
                    if (g_lstat (temporary, &st) == 0 && S_ISREG (st.st_mode) && st.st_uid == getuid ())
                        ok = g_unlink (temporary) == 0;
                    g_free (temporary);
                    continue;
                }
                if (strlen (name) != 20 || !g_str_has_suffix (name, ".bin")) continue;
                memcpy (key, name, 16); key[16] = 0;
                if (!hex_name (key, 16)) continue;
                gchar *entry = g_build_filename (path, name, NULL);
                if (g_lstat (entry, &st) == 0 && S_ISREG (st.st_mode) && st.st_uid == getuid ()) {
                    if (st.st_size < HEADER_SIZE || (guint64) st.st_size > HEADER_SIZE + RSTTO_SHADER_OBJECT_BYTES) {
                        ok = g_unlink (entry) == 0;
                        g_free (entry);
                        continue;
                    }
                    DiskEntry *e = &items.entries[items.count++];
                    g_snprintf (e->relative, sizeof e->relative, "%s/%s", namespace, name);
                    e->size = st.st_size; e->modified = st.st_mtime; items.bytes += e->size;
                    ok = prune (c, &items, extra, protect);
                }
                g_free (entry);
            }
            g_dir_close (dir);
        }
        g_free (path);
    }
    g_dir_close (root);
    return ok && !stopped (c);
}
void rstto_shader_cache_save (RsttoShaderCache *c, guint64 key, gconstpointer data, gsize size) {
    if (size == 0 || size > RSTTO_SHADER_OBJECT_BYTES || stopped (c)) return;
    gint64 start = rstto_profile_now (); gint lock = -1, fd = -1;
    gboolean saved = FALSE;
    gchar *lock_path = NULL, *temporary = NULL, *path = NULL;
    struct stat st;
    if (g_mkdir_with_parents (c->root, 0700) != 0 || g_mkdir_with_parents (c->directory, 0700) != 0
        || g_lstat (c->directory, &st) != 0 || !S_ISDIR (st.st_mode) || st.st_uid != getuid ()) goto done;
    lock_path = g_build_filename (c->root, ".lock", NULL);
    lock = g_open (lock_path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0 || fstat (lock, &st) != 0 || !S_ISREG (st.st_mode) || st.st_uid != getuid ()
        || flock (lock, LOCK_EX | LOCK_NB) != 0) goto done;
    path = filename (c, key);
    if (!room (c, size + HEADER_SIZE, path + strlen (c->root) + 1)) goto done;
    temporary = g_build_filename (c->directory, ".tmp-XXXXXX", NULL);
    fd = g_mkstemp_full (temporary, O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0) goto done;
    guchar header[HEADER_SIZE]; memcpy (header, "RSTGLC01", 8);
    guint64 le_key = GUINT64_TO_LE (key), le_size = GUINT64_TO_LE (size);
    memcpy (header + 8, &le_key, 8); memcpy (header + 16, &le_size, 8); digest (data, size, header + 24);
    if (transfer (c, fd, header, sizeof header, TRUE) && transfer (c, fd, (gpointer) data, size, TRUE)
        && !stopped (c) && g_rename (temporary, path) == 0) { c->writes++; saved = TRUE; }
done:
    if (fd >= 0) close (fd);
    if (temporary != NULL) g_unlink (temporary);
    if (lock >= 0) close (lock);
    g_free (temporary); g_free (path); g_free (lock_path);
    rstto_profile_end (start, c, key, "shader-cache-save", "saved=%d bytes=%" G_GSIZE_FORMAT, saved, size);
}
void rstto_shader_cache_stats (RsttoShaderCache *c, guint64 *hits, guint64 *misses, guint64 *writes) {
    *hits = c->hits; *misses = c->misses; *writes = c->writes;
}

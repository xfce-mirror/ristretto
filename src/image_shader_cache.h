/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef RSTTO_IMAGE_SHADER_CACHE_H
#define RSTTO_IMAGE_SHADER_CACHE_H
#include <gio/gio.h>
#define RSTTO_SHADER_OBJECT_BYTES ((gsize) 2 * 1024 * 1024)
#define RSTTO_SHADER_PAYLOAD_BYTES ((gsize) 8 * 1024 * 1024)
#define RSTTO_SHADER_DISK_BYTES ((guint64) 32 * 1024 * 1024)
#define RSTTO_SHADER_DISK_ENTRIES 128
#define RSTTO_SHADER_HOST_BYTES ((guint64) 10 * 1024 * 1024)
typedef struct RsttoShaderCache RsttoShaderCache;
/* Private worker-owned persistence. root_override is used by isolated tests. */
RsttoShaderCache *
rstto_shader_cache_new (const gchar *fingerprint, const gchar *root_override, GCancellable *cancel);
void
rstto_shader_cache_free (RsttoShaderCache *cache);
gpointer
rstto_shader_cache_load (RsttoShaderCache *cache, guint64 key, gsize *size);
void
rstto_shader_cache_save (RsttoShaderCache *cache, guint64 key, gconstpointer data, gsize size);
void
rstto_shader_cache_stats (RsttoShaderCache *cache, guint64 *hits, guint64 *misses, guint64 *writes);
#endif

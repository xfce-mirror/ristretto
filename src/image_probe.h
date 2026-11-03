/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __RISTRETTO_IMAGE_PROBE_H__
#define __RISTRETTO_IMAGE_PROBE_H__

#include <gio/gio.h>

#define RSTTO_PROBE_LIMIT ((gsize) 1024 * 1024)
#define RSTTO_PROBE_BUFFER_SIZE 4096
#define RSTTO_PROBE_RESERVATION ((guint64) 8 * 1024 * 1024)

/* Internal metadata-only interface. No frame or pixbuf APIs belong here. */
gboolean rstto_image_probe_available (void);
gchar **rstto_image_probe_raw_mime_types (void);
gboolean rstto_image_probe_is_raw (GFile *file, GBytes *prefix, const gchar *const *raw_types);
GBytes *rstto_image_probe_read (GInputStream *stream, GCancellable *cancel);
void rstto_image_probe_async (GBytes *prefix, GCancellable *cancel,
                             GAsyncReadyCallback callback, gpointer data);
gboolean rstto_image_probe_finish (GAsyncResult *result, gint *width, gint *height);

#endif

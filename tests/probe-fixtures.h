/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <gio/gio.h>
#include <string.h>

static void
put_be32 (guchar *p, guint32 value)
{
    value = GUINT32_TO_BE (value);
    memcpy (p, &value, sizeof value);
}

static GBytes *
png_header (guint32 width, guint32 height)
{
    /* Valid IHDR and IDAT header, deliberately no compressed pixel data. A
     * frame request would fail; metadata-only loading must still succeed. */
    guchar png[41] = { 137, 'P', 'N', 'G', 13, 10, 26, 10,
                      0, 0, 0, 13, 'I', 'H', 'D', 'R' };
    put_be32 (png + 16, width);
    put_be32 (png + 20, height);
    png[24] = 8;
    png[25] = 2;
    guint32 crc = G_MAXUINT32;
    for (guint i = 12; i < 29; i++)
    {
        crc ^= png[i];
        for (guint j = 0; j < 8; j++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320 : 0);
    }
    put_be32 (png + 29, ~crc);
    put_be32 (png + 33, 100);
    memcpy (png + 37, "IDAT", 4);
    return g_bytes_new (png, sizeof png);
}

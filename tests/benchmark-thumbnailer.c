/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A no-op thumbnail service for an isolated benchmark bus. Real thumbnail
 * decoding would compete with the image jobs being compared. */
#include <gio/gio.h>
#include <stdio.h>


/**
 * method:
 *
 * Acknowledge thumbnail requests without starting competing decode work on the
 * private benchmark bus.
 */
static void
method (GDBusConnection *connection, const gchar *sender, const gchar *path, const gchar *interface, const gchar *name, GVariant *parameters, GDBusMethodInvocation *invocation, gpointer data)
{
    static guint handle;
    if (g_str_equal (name, "Queue"))
        g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", ++handle));
    else
        g_dbus_method_invocation_return_value (invocation, NULL);
}


/**
 * main:
 *
 * Own the thumbnail service name only on an explicitly isolated benchmark bus
 * and run its no-op request loop.
 */
int
main (void)
{
    if (g_strcmp0 (g_getenv ("RISTRETTO_BENCH_PRIVATE_BUS"), "1") != 0)
        return 1;
    const gchar *xml = "<node><interface name='org.freedesktop.thumbnails.Thumbnailer1'>"
                       "<method name='Queue'><arg type='as' direction='in'/><arg type='as' direction='in'/>"
                       "<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='u' direction='in'/>"
                       "<arg type='u' direction='out'/></method>"
                       "<method name='Dequeue'><arg type='u' direction='in'/></method></interface></node>";
    GError *error = NULL;
    GDBusConnection *connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
    if (connection == NULL)
        return 1;
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml (xml, &error);
    const GDBusInterfaceVTable vtable = { .method_call = method };
    guint object = g_dbus_connection_register_object (connection, "/org/freedesktop/thumbnails/Thumbnailer1",
                                                      node->interfaces[0], &vtable, NULL, NULL, &error);
    GVariant *reply = g_dbus_connection_call_sync (connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                   "org.freedesktop.DBus", "RequestName", g_variant_new ("(su)", "org.freedesktop.thumbnails.Thumbnailer1", 4),
                                                   G_VARIANT_TYPE ("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, NULL, &error);
    guint result = 0;
    if (reply != NULL)
        g_variant_get (reply, "(u)", &result);
    if (error != NULL || !object || result != 1)
        return 1;
    g_variant_unref (reply);
    puts ("READY");
    fflush (stdout);
    GMainLoop *loop = g_main_loop_new (NULL, FALSE);
    g_main_loop_run (loop);
    return 0;
}

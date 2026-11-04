/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "util.h"
#include "image_probe.h"
#include "preferences_dialog.h"

#include <glib/gstdio.h>

static RsttoSettings *settings;
static const gchar *const uint_properties[] = {
    "cache-max-images", "cache-max-mib", "cache-max-image-mib",
    "preload-count", "preload-workers"
};


/**
 * find_control:
 *
 * Find a named control recursively without exposing dialog internals.
 */
static GtkWidget *
find_control (GtkWidget *widget, const gchar *name)
{
    if (g_strcmp0 (gtk_buildable_get_name (GTK_BUILDABLE (widget)), name) == 0)
        return widget;
    if (GTK_IS_CONTAINER (widget))
    {
        GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
        GtkWidget *found = NULL;
        for (GList *child = children; child != NULL && found == NULL; child = child->next)
            found = find_control (child->data, name);
        g_list_free (children);
        return found;
    }
    return NULL;
}


/**
 * create_dialog:
 *
 * Return an owned dialog using the shared, isolated settings object.
 */
static GtkWidget *
create_dialog (void)
{
    return g_object_ref_sink (rstto_preferences_dialog_new (NULL));
}


/**
 * destroy_dialog:
 *
 * Release the dialog and verify its children and bindings can be finalized.
 */
static void
destroy_dialog (GtkWidget *dialog)
{
    GtkWidget *control = find_control (dialog, "cache-max-mib");
    g_object_add_weak_pointer (G_OBJECT (control), (gpointer *) &control);
    gtk_widget_destroy (dialog);
    g_object_unref (dialog);
    g_assert_null (control);
}


/**
 * test_initial_values:
 *
 * Opening preferences must preserve both compiled defaults and saved overrides.
 */
static void
test_initial_values (void)
{
    for (guint pass = 0; pass < 2; pass++)
    {
        const guint overrides[] = { 6, 2048, 256, 5, 5 };
        guint expected[G_N_ELEMENTS (uint_properties)];
        for (guint i = 0; i < G_N_ELEMENTS (uint_properties); i++)
        {
            GParamSpecUInt *spec = G_PARAM_SPEC_UINT (g_object_class_find_property (G_OBJECT_GET_CLASS (settings), uint_properties[i]));
            expected[i] = pass == 0 ? spec->default_value : overrides[i];
            g_object_set (settings, uint_properties[i], expected[i], NULL);
        }
        g_object_set (settings, "cache-enabled", pass == 0, NULL);
        GtkWidget *dialog = create_dialog ();
        GtkWidget *page = find_control (dialog, "advanced-page");
        GtkNotebook *notebook = GTK_NOTEBOOK (gtk_widget_get_parent (page));
        g_assert_cmpint (gtk_notebook_get_n_pages (notebook), ==, 6);
        g_assert_cmpint (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (find_control (dialog, "cache-enabled"))), ==, pass == 0);
        for (guint i = 0; i < G_N_ELEMENTS (uint_properties); i++)
        {
            GtkSpinButton *spin = GTK_SPIN_BUTTON (find_control (dialog, uint_properties[i]));
            g_assert_cmpfloat (gtk_spin_button_get_value (spin), ==, expected[i]);
            g_assert_cmpuint (rstto_settings_get_uint_property (settings, uint_properties[i]), ==, expected[i]);
        }
        destroy_dialog (dialog);
    }
}


/**
 * test_live_bindings:
 *
 * Edits and external property changes must reach every open dialog immediately.
 */
static void
test_live_bindings (void)
{
    g_object_set (settings, "cache-enabled", TRUE, NULL);
    GtkWidget *first = create_dialog ();
    GtkWidget *second = create_dialog ();
    for (guint i = 0; i < G_N_ELEMENTS (uint_properties); i++)
    {
        GtkSpinButton *spin = GTK_SPIN_BUTTON (find_control (first, uint_properties[i]));
        GtkSpinButton *other = GTK_SPIN_BUTTON (find_control (second, uint_properties[i]));
        gtk_entry_set_text (GTK_ENTRY (spin), "17");
        gtk_spin_button_update (spin);
        g_assert_cmpuint (rstto_settings_get_uint_property (settings, uint_properties[i]), ==, 17);
        g_assert_cmpfloat (gtk_spin_button_get_value (other), ==, 17);
        g_object_set (settings, uint_properties[i], 9u, NULL);
        g_assert_cmpfloat (gtk_spin_button_get_value (spin), ==, 9);
        g_assert_cmpfloat (gtk_spin_button_get_value (other), ==, 9);
    }
    GtkToggleButton *enabled = GTK_TOGGLE_BUTTON (find_control (first, "cache-enabled"));
    gtk_toggle_button_set_active (enabled, FALSE);
    g_assert_false (rstto_settings_get_boolean_property (settings, "cache-enabled"));
    g_assert_false (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (find_control (second, "cache-enabled"))));
    g_object_set (settings, "cache-enabled", TRUE, NULL);
    g_assert_true (gtk_toggle_button_get_active (enabled));
    g_assert_true (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (find_control (second, "cache-enabled"))));
    destroy_dialog (first);
    destroy_dialog (second);
}


/**
 * test_ranges:
 *
 * Controls must preserve unsigned values, zero sentinels and property bounds.
 */
static void
test_ranges (void)
{
    GtkWidget *dialog = create_dialog ();
    for (guint i = 0; i < G_N_ELEMENTS (uint_properties); i++)
    {
        GParamSpecUInt *spec = G_PARAM_SPEC_UINT (g_object_class_find_property (G_OBJECT_GET_CLASS (settings), uint_properties[i]));
        GtkSpinButton *spin = GTK_SPIN_BUTTON (find_control (dialog, uint_properties[i]));
        gdouble lower, upper;
        gtk_spin_button_get_range (spin, &lower, &upper);
        g_assert_cmpfloat (lower, ==, spec->minimum);
        g_assert_cmpfloat (upper, ==, spec->maximum);
        g_assert_false (gtk_spin_button_get_wrap (spin));
        gtk_spin_button_set_value (spin, upper);
        g_assert_cmpuint (rstto_settings_get_uint_property (settings, uint_properties[i]), ==, spec->maximum);
        gtk_spin_button_spin (spin, GTK_SPIN_STEP_FORWARD, 1);
        g_assert_cmpfloat (gtk_spin_button_get_value (spin), ==, upper);
        gtk_entry_set_text (GTK_ENTRY (spin), "999999999999");
        gtk_spin_button_update (spin);
        g_assert_cmpfloat (gtk_spin_button_get_value (spin), ==, upper);
        gtk_spin_button_set_value (spin, lower);
        g_assert_cmpuint (rstto_settings_get_uint_property (settings, uint_properties[i]), ==, spec->minimum);
        g_object_set (settings, uint_properties[i], spec->maximum, NULL);
        g_assert_cmpfloat (gtk_spin_button_get_value (spin), ==, upper);
    }
    destroy_dialog (dialog);
}


/**
 * test_sensitivity:
 *
 * Disabling caching must retain values; unavailable probing must stay disabled.
 */
static void
test_sensitivity (void)
{
    g_object_set (settings, "cache-max-mib", 2048u, "cache-enabled", TRUE, NULL);
    GtkWidget *dialog = create_dialog ();
    GtkWidget *enabled = find_control (dialog, "cache-enabled");
    g_assert_cmpint (gtk_widget_is_sensitive (enabled), ==, rstto_image_probe_available ());
    for (guint pass = 0; pass < 2; pass++)
    {
        g_object_set (settings, "cache-enabled", pass != 0, NULL);
        for (guint i = 0; i < G_N_ELEMENTS (uint_properties); i++)
            g_assert_cmpint (gtk_widget_is_sensitive (find_control (dialog, uint_properties[i])), ==,
                             pass != 0 && rstto_image_probe_available ());
        g_assert_cmpuint (rstto_settings_get_uint_property (settings, "cache-max-mib"), ==, 2048);
    }
    destroy_dialog (dialog);
}


/**
 * test_reopen:
 *
 * Destroyed bindings must not retain widgets or interfere with later changes.
 */
static void
test_reopen (void)
{
    for (guint i = 0; i < 5; i++)
    {
        g_object_set (settings, "cache-max-mib", 1024u + i, NULL);
        GtkWidget *dialog = create_dialog ();
        g_assert_cmpfloat (gtk_spin_button_get_value (GTK_SPIN_BUTTON (find_control (dialog, "cache-max-mib"))), ==, 1024 + i);
        destroy_dialog (dialog);
    }
    g_object_set (settings, "cache-max-mib", 2048u, NULL);
}


/**
 * test_renderer:
 *
 * Radio buttons must follow the saved choice without changing the startup
 * renderer, including in another dialog. Check override and unavailable-build
 * explanations independently of the persisted preference.
 */
static void
test_renderer (void)
{
    const gchar *override = g_getenv ("RISTRETTO_RENDERER");
    gboolean overridden = g_strcmp0 (override, "cpu") == 0 || g_strcmp0 (override, "placebo") == 0;
    gboolean available = FALSE;
#if defined(HAVE_PLACEBO) && defined(HAVE_GLYCIN)
    available = TRUE;
#endif
    gboolean startup = available && g_strcmp0 (override, "placebo") == 0;
    g_assert_false (rstto_settings_get_boolean_property (settings, "gpu-enabled"));
    g_assert_cmpint (rstto_settings_get_startup_gpu_enabled (settings), ==, startup);
    GtkWidget *dialog = create_dialog ();
    GtkToggleButton *cpu = GTK_TOGGLE_BUTTON (find_control (dialog, "renderer-cpu"));
    GtkToggleButton *gpu = GTK_TOGGLE_BUTTON (find_control (dialog, "renderer-gpu"));
    g_assert_true (gtk_toggle_button_get_active (cpu));
    g_assert_false (gtk_toggle_button_get_active (gpu));
    g_assert_true (gtk_widget_is_sensitive (GTK_WIDGET (cpu)));
    g_assert_cmpint (gtk_widget_is_sensitive (GTK_WIDGET (gpu)), ==, available);
    g_assert_nonnull (find_control (dialog, "renderer-restart"));
    g_assert_cmpint (find_control (dialog, "renderer-override") != NULL, ==, overridden);

    gtk_toggle_button_set_active (gpu, TRUE);
    g_assert_true (rstto_settings_get_boolean_property (settings, "gpu-enabled"));
    g_assert_false (gtk_toggle_button_get_active (cpu));
    g_assert_cmpint (rstto_settings_get_startup_gpu_enabled (settings), ==, startup);
    GtkWidget *other = create_dialog ();
    g_assert_true (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (find_control (other, "renderer-gpu"))));
    g_assert_true (rstto_settings_get_boolean_property (settings, "gpu-enabled"));
    gtk_toggle_button_set_active (cpu, TRUE);
    g_assert_false (rstto_settings_get_boolean_property (settings, "gpu-enabled"));
    g_assert_false (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (find_control (other, "renderer-gpu"))));
    g_object_set (settings, "gpu-enabled", TRUE, NULL);
    g_assert_true (gtk_toggle_button_get_active (gpu));
    g_assert_false (gtk_toggle_button_get_active (cpu));
    g_assert_cmpint (rstto_settings_get_startup_gpu_enabled (settings), ==, startup);
    destroy_dialog (other);
    destroy_dialog (dialog);
    g_object_set (settings, "gpu-enabled", FALSE, NULL);
    g_assert_cmpint (rstto_settings_get_startup_gpu_enabled (settings), ==, startup);
}


/**
 * main:
 *
 * Run GTK binding tests without connecting to the user's Xfconf channel.
 */
int
main (int argc, char **argv)
{
    GError *error = NULL;
    gchar *config = g_dir_make_tmp ("ristretto-preferences-XXXXXX", &error);
    g_assert_no_error (error);
    g_setenv ("XDG_CONFIG_HOME", config, TRUE);
    g_test_init (&argc, &argv, NULL);
    if (!gtk_init_check (&argc, &argv))
    {
        g_rmdir (config);
        g_free (config);
        return 77;
    }
    rstto_settings_set_xfconf_disabled (TRUE);
    settings = rstto_settings_new ();
    g_test_add_func ("/preferences/initial-values", test_initial_values);
    g_test_add_func ("/preferences/live-bindings", test_live_bindings);
    g_test_add_func ("/preferences/ranges", test_ranges);
    g_test_add_func ("/preferences/sensitivity", test_sensitivity);
    g_test_add_func ("/preferences/reopen", test_reopen);
    g_test_add_func ("/preferences/renderer", test_renderer);
    gint result = g_test_run ();
    g_object_unref (settings);
    gchar *accels = g_build_filename (config, "ristretto", "accels.scm", NULL);
    gchar *app_config = g_build_filename (config, "ristretto", NULL);
    g_remove (accels);
    g_rmdir (app_config);
    g_rmdir (config);
    g_free (accels);
    g_free (app_config);
    g_free (config);
    return result;
}

/*
 *  Copyright (C) Stephan Arts 2006-2008 <stephan@xfce.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <thunar-vfs/thunar-vfs.h>
#include <libxfcegui4/libxfcegui4.h>
#include <libexif/exif-data.h>
#include <dbus/dbus-glib.h>

#ifdef HAVE_XFCONF
#include <xfconf/xfconf.h>
#endif

#include "navigator.h"
#include "thumbnail_bar.h"
#include "picture_viewer.h"
#include "main_window.h"

typedef enum {
    RSTTO_DESKTOP_NONE,
    RSTTO_DESKTOP_XFCE
} RsttoDesktop;


struct _RsttoMainWindowPriv
{
    RsttoNavigator *navigator;
    GtkWidget *thumbnail_viewer;
    GtkWidget *picture_viewer;
    GtkWidget *statusbar;
    GtkRecentManager *manager;
    GtkIconTheme *icon_theme;
    ThunarVfsMimeDatabase *mime_dbase;
    GList *menu_apps_list;
    gdouble zoom_factor;

    DBusGConnection *connection;
    DBusGProxy *filemanager_proxy;

    struct {
        GtkWidget *main_vbox;
        GtkWidget *s_window;
        GtkWidget *paned;
    } containers;

    struct {
        GtkOrientation  thumbnail_viewer_orientation;
        gboolean        thumbnail_viewer_visibility;
        gboolean        toolbar_visibility;
        gint            max_cache_size;
        gdouble         slideshow_timeout;
        const GdkColor *bg_color;
        gboolean        scale_to_100;
        RsttoDesktop    desktop;
    } settings;

    struct {
        GtkWidget *menu;
        GtkWidget *menu_item_file;
        struct {
            GtkWidget *menu;
            GtkWidget *menu_item_open_file;
            GtkWidget *menu_item_open_folder;
            GtkWidget *menu_item_open_recently;
            struct {
                GtkWidget *menu;
                GtkWidget *menu_item_separator_1;
                GtkWidget *menu_item_clear;
            } recently;
            GtkWidget *menu_item_separator_1;
            GtkWidget *menu_item_file_properties;
            GtkWidget *menu_item_separator_2;
            GtkWidget *menu_item_close;
            GtkWidget *menu_item_quit;
        } file;

        GtkWidget *menu_item_edit;
        struct {
            GtkWidget *menu;
            GtkWidget *menu_item_open_with;
            struct {
                GtkWidget *menu;
                GtkWidget *menu_item_empty;
            } open_with;
            GtkWidget *menu_item_preferences;
        } edit;

        GtkWidget *menu_item_view;
        struct {
            GtkWidget *menu;
            GtkWidget *menu_item_show_toolbar;
            GtkWidget *menu_item_show_thumbnail_viewer;
            struct {
                GtkWidget *menu;
                GtkWidget *menu_item_thumbnail_viewer_horizontal;
                GtkWidget *menu_item_thumbnail_viewer_vertical;
                GtkWidget *menu_item_thumbnail_viewer_hide;
            } show_thumbnail_viewer;
            GtkWidget *menu_item_separator_1;

            GtkWidget *menu_item_zooming;
            struct {
                GtkWidget *menu;
                GtkWidget *menu_item_zoom_in;
                GtkWidget *menu_item_zoom_out;
                GtkWidget *menu_item_zoom_100;
                GtkWidget *menu_item_zoom_fit;
                GtkWidget *menu_item_zoom_box;
            } zooming;

            GtkWidget *menu_item_rotate;
            struct {
                GtkWidget *menu;
                GtkWidget *menu_item_rotate_cw;
                GtkWidget *menu_item_rotate_ccw;
            } rotate;

            GtkWidget *menu_item_separator_2;
            GtkWidget *menu_item_fullscreen;
            GtkWidget *menu_item_set_wallpaper;
        } view;

        GtkWidget *menu_item_go;
        struct {
            GtkWidget *menu;
            GtkWidget *menu_item_next;
            GtkWidget *menu_item_previous;
            GtkWidget *menu_item_first;
            GtkWidget *menu_item_last;
            GtkWidget *menu_item_separator_1;
            GtkWidget *menu_item_play;
            GtkWidget *menu_item_pause;
        } go;

        GtkWidget *menu_item_help;
        struct {
            GtkWidget *menu;
            GtkWidget *menu_item_about;
        } help;

        struct {
            GtkWidget *menu;
            GtkWidget *menu_item_open_file;
            GtkWidget *menu_item_close;
            GtkWidget *menu_item_separator_1;
            GtkWidget *menu_item_open_with;
            struct {
                GtkWidget *menu;
                GtkWidget *menu_item_empty;
            } open_with;
            GtkWidget *menu_item_separator_2;
            GtkWidget *menu_item_zoom_in;
            GtkWidget *menu_item_zoom_out;
            GtkWidget *menu_item_zoom_fit;
            GtkWidget *menu_item_zoom_100;
        } _picture_viewer;
    } menus;

    struct {
        GtkWidget *bar;

        GtkToolItem *tool_item_open;
        GtkToolItem *tool_item_separator_1;
        GtkToolItem *tool_item_next;
        GtkToolItem *tool_item_previous;
        GtkToolItem *tool_item_spacer_1;

        GtkToolItem *tool_item_zoom_fit;
        GtkToolItem *tool_item_zoom_100;
        GtkToolItem *tool_item_zoom_out;
        GtkToolItem *tool_item_zoom_in;
    } toolbar;
};

static void
cb_rstto_main_window_thumbnail_viewer_horizontal(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_thumbnail_viewer_vertical(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_thumbnail_viewer_hide(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_toggle_toolbar(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_toggle_fullscreen(GtkWidget *widget, RsttoMainWindow *window);
#ifdef WITH_DESKTOP_WALLPAPER
static void
cb_rstto_main_window_set_wallpaper(GtkWidget *widget, RsttoMainWindow *window);
#endif
static gboolean
cb_rstto_main_window_key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer user_data);
static void
cb_rstto_main_window_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data);
static void
cb_rstto_main_window_play(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_pause(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_spawn_app(GtkWidget *widget, ThunarVfsMimeApplication *app);

static void
cb_rstto_main_window_next(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_previous(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_last(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_first(GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_main_window_zoom_in(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_zoom_out(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_zoom_100(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_zoom_fit(GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_main_window_rotate_cw(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_rotate_ccw(GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_main_window_open_file(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_open_folder(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_open_recent(GtkRecentChooser *chooser, RsttoMainWindow *window);
static void
cb_rstto_main_window_clear_recent(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_close(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_file_properties(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_quit(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_about(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_preferences(GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_bg_color_override_check_toggled(GtkToggleButton *button, GtkWidget *);

static void
cb_rstto_main_window_nav_iter_changed(RsttoNavigator *navigator, gint nr, RsttoNavigatorEntry *entry, RsttoMainWindow *window);
static void
cb_rstto_main_window_nav_new_entry(RsttoNavigator *navigator, gint nr, RsttoNavigatorEntry *entry, RsttoMainWindow *window);

static void
rstto_main_window_init(RsttoMainWindow *);
static void
rstto_main_window_class_init(RsttoMainWindowClass *);
static void
rstto_main_window_dispose(GObject *object);

static GtkWidgetClass *parent_class = NULL;

GType
rstto_main_window_get_type ()
{
    static GType rstto_main_window_type = 0;

    if (!rstto_main_window_type)
    {
        static const GTypeInfo rstto_main_window_info = 
        {
            sizeof (RsttoMainWindowClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) rstto_main_window_class_init,
            (GClassFinalizeFunc) NULL,
            NULL,
            sizeof (RsttoMainWindow),
            0,
            (GInstanceInitFunc) rstto_main_window_init,
            NULL
        };

        rstto_main_window_type = g_type_register_static (GTK_TYPE_WINDOW, "RsttoMainWindow", &rstto_main_window_info, 0);
    }
    return rstto_main_window_type;
}

static void
rstto_main_window_init(RsttoMainWindow *window)
{
    window->priv = g_new0(RsttoMainWindowPriv, 1);
    window->priv->zoom_factor = 1.2;

    window->priv->mime_dbase = thunar_vfs_mime_database_get_default();
    window->priv->icon_theme = gtk_icon_theme_get_default();

    window->priv->settings.scale_to_100 = TRUE;
    

    GtkAccelGroup *accel_group = gtk_accel_group_new();

    gtk_window_set_title(GTK_WINDOW(window), PACKAGE_STRING);
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    window->priv->manager = gtk_recent_manager_get_default();
    window->priv->navigator = rstto_navigator_new(window->priv->manager);
    window->priv->thumbnail_viewer = rstto_thumbnail_bar_new(window->priv->navigator);
    window->priv->picture_viewer = rstto_picture_viewer_new(window->priv->navigator);


/* Set up default settings */
    window->priv->settings.thumbnail_viewer_orientation = GTK_ORIENTATION_HORIZONTAL; 
    window->priv->settings.thumbnail_viewer_visibility = TRUE;

/* Create menu bar */
    window->priv->menus.menu = gtk_menu_bar_new();
    
/* Create 'File' menu */
    window->priv->menus.menu_item_file = gtk_menu_item_new_with_mnemonic(_("_File"));
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.menu), window->priv->menus.menu_item_file);

    window->priv->menus.file.menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(window->priv->menus.file.menu), accel_group);

    window->priv->menus.file.menu_item_open_file = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, accel_group);
    window->priv->menus.file.menu_item_open_folder = gtk_menu_item_new_with_mnemonic(_("O_pen Folder"));
    window->priv->menus.file.menu_item_open_recently = gtk_menu_item_new_with_mnemonic(_("_Recently used"));
    window->priv->menus.file.menu_item_separator_1 = gtk_separator_menu_item_new();
    window->priv->menus.file.menu_item_file_properties = gtk_image_menu_item_new_from_stock(GTK_STOCK_PROPERTIES, accel_group);
    window->priv->menus.file.menu_item_separator_2 = gtk_separator_menu_item_new();
    window->priv->menus.file.menu_item_close = gtk_image_menu_item_new_from_stock(GTK_STOCK_CLOSE, accel_group);
    window->priv->menus.file.menu_item_quit = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, accel_group);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.menu_item_file), window->priv->menus.file.menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_open_file);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_open_folder);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_open_recently);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_separator_1);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_file_properties);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_separator_2);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_close);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_quit);

    gtk_widget_set_sensitive(window->priv->menus.file.menu_item_close, FALSE);
    gtk_widget_set_sensitive(window->priv->menus.file.menu_item_file_properties, FALSE);

    window->priv->menus.file.recently.menu = gtk_recent_chooser_menu_new_for_manager(GTK_RECENT_MANAGER(window->priv->manager));
    window->priv->menus.file.recently.menu_item_clear = gtk_image_menu_item_new_from_stock(GTK_STOCK_CLEAR, accel_group);
    window->priv->menus.file.recently.menu_item_separator_1 = gtk_separator_menu_item_new();

    GtkRecentFilter *filter = gtk_recent_filter_new();
    gtk_recent_filter_add_application(filter, "ristretto");
    gtk_recent_chooser_add_filter(GTK_RECENT_CHOOSER(window->priv->menus.file.recently.menu), filter);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.file.menu_item_open_recently), window->priv->menus.file.recently.menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.recently.menu), window->priv->menus.file.recently.menu_item_separator_1);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.recently.menu), window->priv->menus.file.recently.menu_item_clear);

/* Create 'Edit' menu */
    window->priv->menus.menu_item_edit = gtk_menu_item_new_with_mnemonic(_("_Edit"));
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.menu), window->priv->menus.menu_item_edit);

    window->priv->menus.edit.menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(window->priv->menus.edit.menu), accel_group);
    window->priv->menus.edit.menu_item_open_with = gtk_menu_item_new_with_mnemonic(_("Open with..."));
    window->priv->menus.edit.menu_item_preferences = gtk_image_menu_item_new_from_stock(GTK_STOCK_PREFERENCES, NULL);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.menu_item_edit), window->priv->menus.edit.menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.menu), window->priv->menus.edit.menu_item_open_with);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.menu), window->priv->menus.edit.menu_item_preferences);
    
    window->priv->menus.edit.open_with.menu = gtk_menu_new();
    window->priv->menus.edit.open_with.menu_item_empty = gtk_menu_item_new_with_label(_("No applications available"));

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.open_with.menu), window->priv->menus.edit.open_with.menu_item_empty);
    gtk_widget_set_sensitive(window->priv->menus.edit.open_with.menu_item_empty, FALSE);
    gtk_widget_ref(window->priv->menus.edit.open_with.menu_item_empty);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.edit.menu_item_open_with), window->priv->menus.edit.open_with.menu);

/* Create 'View' menu */
    window->priv->menus.menu_item_view = gtk_menu_item_new_with_mnemonic(_("_View"));
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.menu), window->priv->menus.menu_item_view);

    window->priv->menus.view.menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(window->priv->menus.view.menu), accel_group);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.menu_item_view), window->priv->menus.view.menu);

    window->priv->menus.view.menu_item_show_toolbar = gtk_check_menu_item_new_with_mnemonic(_("Show _Toolbar"));
    window->priv->menus.view.menu_item_show_thumbnail_viewer = gtk_menu_item_new_with_mnemonic(_("Thumbnail _Viewer"));

    window->priv->menus.view.menu_item_separator_1 = gtk_separator_menu_item_new();

    window->priv->menus.view.menu_item_zooming = gtk_menu_item_new_with_mnemonic(_("_Zooming"));
    window->priv->menus.view.menu_item_rotate = gtk_menu_item_new_with_mnemonic(_("_Rotate"));

    window->priv->menus.view.menu_item_separator_2 = gtk_separator_menu_item_new();

    window->priv->menus.view.menu_item_fullscreen = gtk_image_menu_item_new_from_stock(GTK_STOCK_FULLSCREEN, NULL);

    GtkWidget *wallpaper_image = gtk_image_new_from_icon_name("preferences-desktop-wallpaper", GTK_ICON_SIZE_MENU);
    window->priv->menus.view.menu_item_set_wallpaper = gtk_image_menu_item_new_with_mnemonic(_("_Set as wallpaper"));
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(window->priv->menus.view.menu_item_set_wallpaper), wallpaper_image);


    gtk_widget_add_accelerator(window->priv->menus.view.menu_item_fullscreen, "activate", accel_group, GDK_F11, 0,GTK_ACCEL_VISIBLE);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(window->priv->menus.view.menu_item_show_toolbar), TRUE);

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_show_toolbar);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_show_thumbnail_viewer);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_separator_1);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_zooming);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_rotate);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_separator_2);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_fullscreen);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_set_wallpaper);

/* Create 'View/Show thumbnail-bar' menu */
    window->priv->menus.view.show_thumbnail_viewer.menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(window->priv->menus.view.show_thumbnail_viewer.menu), accel_group);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.view.menu_item_show_thumbnail_viewer),
            window->priv->menus.view.show_thumbnail_viewer.menu);
    window->priv->menus.view.show_thumbnail_viewer.
            menu_item_thumbnail_viewer_horizontal = gtk_radio_menu_item_new_with_mnemonic(
                    NULL, 
                    _("Show _Horizontally"));
    window->priv->menus.view.show_thumbnail_viewer.
            menu_item_thumbnail_viewer_vertical = gtk_radio_menu_item_new_with_mnemonic_from_widget(
                    GTK_RADIO_MENU_ITEM(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_horizontal),
                    _("Show _Vertically"));
    window->priv->menus.view.show_thumbnail_viewer.
            menu_item_thumbnail_viewer_hide = gtk_radio_menu_item_new_with_mnemonic_from_widget(
                    GTK_RADIO_MENU_ITEM(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_horizontal),
                    _("H_ide"));

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.show_thumbnail_viewer.menu),
            window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_horizontal);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.show_thumbnail_viewer.menu),
            window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_vertical);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.show_thumbnail_viewer.menu),
            window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_hide);

/* Create 'view/zooming' menu */
    window->priv->menus.view.zooming.menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(window->priv->menus.view.zooming.menu), accel_group);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.view.menu_item_zooming),
            window->priv->menus.view.zooming.menu);

    window->priv->menus.view.zooming.menu_item_zoom_in = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_IN, accel_group);
    window->priv->menus.view.zooming.menu_item_zoom_out = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_OUT, accel_group);
    window->priv->menus.view.zooming.menu_item_zoom_100 = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_100, accel_group);
    window->priv->menus.view.zooming.menu_item_zoom_fit = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_FIT, accel_group);

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.zooming.menu),
            window->priv->menus.view.zooming.menu_item_zoom_in);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.zooming.menu),
            window->priv->menus.view.zooming.menu_item_zoom_out);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.zooming.menu),
            window->priv->menus.view.zooming.menu_item_zoom_100);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.zooming.menu),
            window->priv->menus.view.zooming.menu_item_zoom_fit);

/* Create 'view/rotate' menu */
    window->priv->menus.view.rotate.menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(window->priv->menus.view.rotate.menu), accel_group);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.view.menu_item_rotate),
            window->priv->menus.view.rotate.menu);

    window->priv->menus.view.rotate.menu_item_rotate_cw = gtk_image_menu_item_new_with_mnemonic(_("Rotate _Right"));
    window->priv->menus.view.rotate.menu_item_rotate_ccw = gtk_image_menu_item_new_with_mnemonic(_("Rotate _Left"));

    gtk_widget_add_accelerator(window->priv->menus.view.rotate.menu_item_rotate_cw,
                               "activate",
                               accel_group,
                               GDK_bracketright,
                               GDK_CONTROL_MASK,
                               GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(window->priv->menus.view.rotate.menu_item_rotate_ccw,
                               "activate",
                               accel_group,
                               GDK_bracketleft,
                               GDK_CONTROL_MASK,
                               GTK_ACCEL_VISIBLE);

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.rotate.menu),
            window->priv->menus.view.rotate.menu_item_rotate_cw);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.rotate.menu),
            window->priv->menus.view.rotate.menu_item_rotate_ccw);

/* Create 'Go' menu */
    window->priv->menus.menu_item_go= gtk_menu_item_new_with_mnemonic(_("_Go"));
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.menu), window->priv->menus.menu_item_go);

    window->priv->menus.go.menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(window->priv->menus.go.menu), accel_group);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.menu_item_go), window->priv->menus.go.menu);

    window->priv->menus.go.menu_item_first = gtk_image_menu_item_new_from_stock(GTK_STOCK_GOTO_FIRST, NULL);
    window->priv->menus.go.menu_item_last = gtk_image_menu_item_new_from_stock(GTK_STOCK_GOTO_LAST, NULL);
    window->priv->menus.go.menu_item_next = gtk_image_menu_item_new_from_stock(GTK_STOCK_GO_FORWARD, NULL);
    window->priv->menus.go.menu_item_previous = gtk_image_menu_item_new_from_stock(GTK_STOCK_GO_BACK, NULL);
    window->priv->menus.go.menu_item_separator_1 = gtk_separator_menu_item_new();
    window->priv->menus.go.menu_item_play = gtk_image_menu_item_new_from_stock(GTK_STOCK_MEDIA_PLAY, accel_group);
    window->priv->menus.go.menu_item_pause = gtk_image_menu_item_new_from_stock(GTK_STOCK_MEDIA_PAUSE, accel_group);

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.go.menu), window->priv->menus.go.menu_item_next);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.go.menu), window->priv->menus.go.menu_item_previous);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.go.menu), window->priv->menus.go.menu_item_first);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.go.menu), window->priv->menus.go.menu_item_last);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.go.menu), window->priv->menus.go.menu_item_separator_1);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.go.menu), window->priv->menus.go.menu_item_play);

    gtk_widget_set_sensitive(window->priv->menus.go.menu_item_first, FALSE);
    gtk_widget_set_sensitive(window->priv->menus.go.menu_item_last, FALSE);
    gtk_widget_set_sensitive(window->priv->menus.go.menu_item_next, FALSE);
    gtk_widget_set_sensitive(window->priv->menus.go.menu_item_previous, FALSE);
    gtk_widget_set_sensitive(window->priv->menus.go.menu_item_play, FALSE);

    gtk_widget_set_sensitive(window->priv->menus.view.menu_item_set_wallpaper, FALSE);

/* Create 'Help' menu */
    window->priv->menus.menu_item_help = gtk_menu_item_new_with_mnemonic(_("_Help"));
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.menu), window->priv->menus.menu_item_help);

    window->priv->menus.help.menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(window->priv->menus.help.menu), accel_group);

    window->priv->menus.help.menu_item_about = gtk_image_menu_item_new_from_stock(GTK_STOCK_ABOUT, accel_group);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.help.menu), window->priv->menus.help.menu_item_about);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.menu_item_help), window->priv->menus.help.menu);

/* Create toolbar */
    window->priv->toolbar.bar = gtk_toolbar_new();
    window->priv->toolbar.tool_item_open = gtk_tool_button_new_from_stock(GTK_STOCK_OPEN);
    window->priv->toolbar.tool_item_separator_1 = gtk_separator_tool_item_new();
    window->priv->toolbar.tool_item_next = gtk_tool_button_new_from_stock(GTK_STOCK_GO_FORWARD);
    window->priv->toolbar.tool_item_previous = gtk_tool_button_new_from_stock(GTK_STOCK_GO_BACK);
    window->priv->toolbar.tool_item_spacer_1= gtk_tool_item_new();

    gtk_tool_item_set_is_important(window->priv->toolbar.tool_item_previous, TRUE);
    gtk_tool_item_set_is_important(window->priv->toolbar.tool_item_next, TRUE);

    gtk_tool_item_set_expand(window->priv->toolbar.tool_item_spacer_1, TRUE);
    gtk_tool_item_set_homogeneous(window->priv->toolbar.tool_item_spacer_1, FALSE);

    window->priv->toolbar.tool_item_zoom_fit = gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_FIT);
    window->priv->toolbar.tool_item_zoom_100 = gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_100);
    window->priv->toolbar.tool_item_zoom_in  = gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_IN);
    window->priv->toolbar.tool_item_zoom_out = gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_OUT);

    gtk_toolbar_insert(GTK_TOOLBAR(window->priv->toolbar.bar), window->priv->toolbar.tool_item_open, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(window->priv->toolbar.bar), window->priv->toolbar.tool_item_separator_1, 1);
    gtk_toolbar_insert(GTK_TOOLBAR(window->priv->toolbar.bar), window->priv->toolbar.tool_item_previous, 2);
    gtk_toolbar_insert(GTK_TOOLBAR(window->priv->toolbar.bar), window->priv->toolbar.tool_item_next, 3);
    gtk_toolbar_insert(GTK_TOOLBAR(window->priv->toolbar.bar), window->priv->toolbar.tool_item_spacer_1, 4);
    gtk_toolbar_insert(GTK_TOOLBAR(window->priv->toolbar.bar), window->priv->toolbar.tool_item_zoom_in, 5);
    gtk_toolbar_insert(GTK_TOOLBAR(window->priv->toolbar.bar), window->priv->toolbar.tool_item_zoom_out, 6);
    gtk_toolbar_insert(GTK_TOOLBAR(window->priv->toolbar.bar), window->priv->toolbar.tool_item_zoom_fit, 7);
    gtk_toolbar_insert(GTK_TOOLBAR(window->priv->toolbar.bar), window->priv->toolbar.tool_item_zoom_100, 8);

    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_previous), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_next), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_in), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_out), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_100), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_fit), FALSE);

    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_in), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_out), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_100), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_fit), FALSE);

    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.rotate.menu_item_rotate_cw), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.rotate.menu_item_rotate_ccw), FALSE);

/* Create statusbar */
    window->priv->statusbar = gtk_statusbar_new();

/* Create containers */
    window->priv->containers.main_vbox = gtk_vbox_new(FALSE, 0);
    window->priv->containers.s_window = gtk_scrolled_window_new(NULL, NULL);
    window->priv->containers.paned = gtk_vbox_new(FALSE, 0);

    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(window->priv->containers.s_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

    gtk_container_add(GTK_CONTAINER(window), window->priv->containers.main_vbox);
    gtk_box_pack_start(GTK_BOX(window->priv->containers.main_vbox), window->priv->menus.menu, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(window->priv->containers.main_vbox), window->priv->toolbar.bar, FALSE, FALSE, 0);

    gtk_container_add(GTK_CONTAINER(window->priv->containers.s_window), window->priv->picture_viewer);

    gtk_box_pack_start(GTK_BOX(window->priv->containers.paned), window->priv->containers.s_window, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(window->priv->containers.paned), window->priv->thumbnail_viewer, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(window->priv->containers.main_vbox), window->priv->containers.paned, TRUE, TRUE, 0);

    gtk_box_pack_end(GTK_BOX(window->priv->containers.main_vbox), window->priv->statusbar, FALSE, FALSE, 0);

/* Create picture viewer menu */
    window->priv->menus._picture_viewer.menu = gtk_menu_new();
    window->priv->menus._picture_viewer.menu_item_open_file = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, NULL);
    window->priv->menus._picture_viewer.menu_item_close = gtk_image_menu_item_new_from_stock(GTK_STOCK_CLOSE, NULL);

    window->priv->menus._picture_viewer.menu_item_separator_1 = gtk_separator_menu_item_new();

    window->priv->menus._picture_viewer.menu_item_open_with = gtk_menu_item_new_with_mnemonic(_("Open with..."));

    window->priv->menus._picture_viewer.menu_item_separator_2 = gtk_separator_menu_item_new();


    window->priv->menus._picture_viewer.menu_item_zoom_in = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_IN, NULL);
    window->priv->menus._picture_viewer.menu_item_zoom_out = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_OUT, NULL);
    window->priv->menus._picture_viewer.menu_item_zoom_fit = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_FIT, NULL);
    window->priv->menus._picture_viewer.menu_item_zoom_100 = gtk_image_menu_item_new_from_stock(GTK_STOCK_ZOOM_100, NULL);

    window->priv->menus._picture_viewer.open_with.menu = gtk_menu_new();
    window->priv->menus._picture_viewer.open_with.menu_item_empty = gtk_menu_item_new_with_label(_("No applications available"));

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.open_with.menu),
                                         window->priv->menus._picture_viewer.open_with.menu_item_empty);
    gtk_widget_set_sensitive(window->priv->menus._picture_viewer.open_with.menu_item_empty, FALSE);
    gtk_widget_ref(window->priv->menus._picture_viewer.open_with.menu_item_empty);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus._picture_viewer.menu_item_open_with),
                              window->priv->menus._picture_viewer.open_with.menu);

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.menu), window->priv->menus._picture_viewer.menu_item_open_file);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.menu), window->priv->menus._picture_viewer.menu_item_close);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.menu), window->priv->menus._picture_viewer.menu_item_separator_1);

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.menu), window->priv->menus._picture_viewer.menu_item_open_with);

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.menu), window->priv->menus._picture_viewer.menu_item_separator_2);

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.menu), window->priv->menus._picture_viewer.menu_item_zoom_in);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.menu), window->priv->menus._picture_viewer.menu_item_zoom_out);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.menu), window->priv->menus._picture_viewer.menu_item_zoom_fit);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.menu), window->priv->menus._picture_viewer.menu_item_zoom_100);

    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_close), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_in), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_out), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_100), FALSE);
    gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_fit), FALSE);

    rstto_picture_viewer_set_menu(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer),
                                  GTK_MENU(window->priv->menus._picture_viewer.menu));
/* D-Bus stuff */

    window->priv->connection = dbus_g_bus_get(DBUS_BUS_SESSION, NULL);
    if (window->priv->connection)
    {
        window->priv->filemanager_proxy = dbus_g_proxy_new_for_name(window->priv->connection,
                                                                "org.xfce.FileManager",
                                                                "/org/xfce/FileManager",
                                                                "org.xfce.FileManager");
    }

/* Connect signals */
    
    /* Thumbnail-viewer */
    g_signal_connect(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_horizontal,
            "activate",
            G_CALLBACK(cb_rstto_main_window_thumbnail_viewer_horizontal), window);
    g_signal_connect(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_vertical,
            "activate",
            G_CALLBACK(cb_rstto_main_window_thumbnail_viewer_vertical), window);
    g_signal_connect(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_hide,
            "activate",
            G_CALLBACK(cb_rstto_main_window_thumbnail_viewer_hide), window);

    /* Toolbar show/hide */
    g_signal_connect(window->priv->menus.view.menu_item_show_toolbar, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_toggle_toolbar), window);

    /* Fullscreen */
    g_signal_connect(window->priv->menus.view.menu_item_fullscreen, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_toggle_fullscreen), window);

    /* Set Wallpaper */
#ifdef WITH_DESKTOP_WALLPAPER
    g_signal_connect(window->priv->menus.view.menu_item_set_wallpaper, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_set_wallpaper), window);
#endif

    /* Play / Pause */
    g_signal_connect(window->priv->menus.go.menu_item_play, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_play), window);
    g_signal_connect(window->priv->menus.go.menu_item_pause, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_pause), window);

    /* Window events */
    g_signal_connect(G_OBJECT(window),
            "window-state-event",
            G_CALLBACK(cb_rstto_main_window_state_event), NULL);
    g_signal_connect(G_OBJECT(window),
            "key-press-event",
            G_CALLBACK(cb_rstto_main_window_key_press_event), NULL);

    /* Generic menu signals */
    g_signal_connect(window->priv->menus.file.menu_item_open_file, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_open_file), window);
    g_signal_connect(window->priv->menus.file.menu_item_open_folder, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_open_folder), window);
    g_signal_connect(window->priv->menus.file.menu_item_close, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_close), window);
    g_signal_connect(G_OBJECT(window->priv->menus.file.recently.menu),
            "item-activated",
            G_CALLBACK(cb_rstto_main_window_open_recent), window);
    g_signal_connect(window->priv->menus.file.menu_item_file_properties, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_file_properties), window);
    g_signal_connect(window->priv->menus.file.menu_item_quit, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_quit), window);
    g_signal_connect(window->priv->menus.file.recently.menu_item_clear, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_clear_recent), window);
    g_signal_connect(window->priv->menus.help.menu_item_about, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_about), window);

    g_signal_connect(window->priv->menus.edit.menu_item_preferences, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_preferences), window);

/* zoom menu items */
    g_signal_connect(window->priv->menus.view.zooming.menu_item_zoom_in,
            "activate",
            G_CALLBACK(cb_rstto_main_window_zoom_in), window);
    g_signal_connect(window->priv->menus.view.zooming.menu_item_zoom_out,
            "activate",
            G_CALLBACK(cb_rstto_main_window_zoom_out), window);
    g_signal_connect(window->priv->menus.view.zooming.menu_item_zoom_100,
            "activate",
            G_CALLBACK(cb_rstto_main_window_zoom_100), window);
    g_signal_connect(window->priv->menus.view.zooming.menu_item_zoom_fit,
            "activate",
            G_CALLBACK(cb_rstto_main_window_zoom_fit), window);
/* rotate menu items */
    g_signal_connect(window->priv->menus.view.rotate.menu_item_rotate_cw,
            "activate",
            G_CALLBACK(cb_rstto_main_window_rotate_cw), window);
    g_signal_connect(window->priv->menus.view.rotate.menu_item_rotate_ccw,
            "activate",
            G_CALLBACK(cb_rstto_main_window_rotate_ccw), window);

/* go menu items */
    g_signal_connect(window->priv->menus.go.menu_item_next,
            "activate",
            G_CALLBACK(cb_rstto_main_window_next), window);
    g_signal_connect(window->priv->menus.go.menu_item_previous,
            "activate",
            G_CALLBACK(cb_rstto_main_window_previous), window);
    g_signal_connect(window->priv->menus.go.menu_item_first,
            "activate",
            G_CALLBACK(cb_rstto_main_window_first), window);
    g_signal_connect(window->priv->menus.go.menu_item_last,
            "activate",
            G_CALLBACK(cb_rstto_main_window_last), window);

    /* Toolbar signals */
    g_signal_connect(window->priv->toolbar.tool_item_open,
            "clicked",
            G_CALLBACK(cb_rstto_main_window_open_folder), window);
    g_signal_connect(window->priv->toolbar.tool_item_next,
            "clicked",
            G_CALLBACK(cb_rstto_main_window_next), window);
    g_signal_connect(window->priv->toolbar.tool_item_previous,
            "clicked",
            G_CALLBACK(cb_rstto_main_window_previous), window);

    g_signal_connect(window->priv->toolbar.tool_item_zoom_in,
            "clicked",
            G_CALLBACK(cb_rstto_main_window_zoom_in), window);
    g_signal_connect(window->priv->toolbar.tool_item_zoom_out,
            "clicked",
            G_CALLBACK(cb_rstto_main_window_zoom_out), window);
    g_signal_connect(window->priv->toolbar.tool_item_zoom_100,
            "clicked",
            G_CALLBACK(cb_rstto_main_window_zoom_100), window);
    g_signal_connect(window->priv->toolbar.tool_item_zoom_fit,
            "clicked",
            G_CALLBACK(cb_rstto_main_window_zoom_fit), window);

    /* Picture viewer menu */
    g_signal_connect(window->priv->menus._picture_viewer.menu_item_open_file,
            "activate",
            G_CALLBACK(cb_rstto_main_window_open_file), window);
    g_signal_connect(window->priv->menus._picture_viewer.menu_item_close,
            "activate",
            G_CALLBACK(cb_rstto_main_window_close), window);
            
    g_signal_connect(window->priv->menus._picture_viewer.menu_item_zoom_in,
            "activate",
            G_CALLBACK(cb_rstto_main_window_zoom_in), window);
    g_signal_connect(window->priv->menus._picture_viewer.menu_item_zoom_out,
            "activate",
            G_CALLBACK(cb_rstto_main_window_zoom_out), window);
    g_signal_connect(window->priv->menus._picture_viewer.menu_item_zoom_100,
            "activate",
            G_CALLBACK(cb_rstto_main_window_zoom_100), window);
    g_signal_connect(window->priv->menus._picture_viewer.menu_item_zoom_fit,
            "activate",
            G_CALLBACK(cb_rstto_main_window_zoom_fit), window);
    /* Misc */
    g_signal_connect(G_OBJECT(window->priv->navigator),
            "iter-changed",
            G_CALLBACK(cb_rstto_main_window_nav_iter_changed), window);
    g_signal_connect(G_OBJECT(window->priv->navigator),
            "new-entry",
            G_CALLBACK(cb_rstto_main_window_nav_new_entry), window);
}

static void
rstto_main_window_class_init(RsttoMainWindowClass *window_class)
{
    GObjectClass *object_class = (GObjectClass*)window_class;
    parent_class = g_type_class_peek_parent(window_class);

    object_class->dispose = rstto_main_window_dispose;
}

static void
rstto_main_window_dispose(GObject *object)
{
    RsttoMainWindow *window = RSTTO_MAIN_WINDOW(object);
    if (window->priv->navigator)
    {
        g_object_unref(window->priv->navigator);
        window->priv->navigator = NULL;
    }
    G_OBJECT_CLASS (parent_class)->dispose(object); 
}

static gboolean
rstto_main_window_clear_recent(RsttoMainWindow *window)
{
    GList *items = gtk_recent_manager_get_items(window->priv->manager);
    GList *iter = items;
    while(iter)
    {
        if(gtk_recent_info_has_application(iter->data, "ristretto"))
        {
            gtk_recent_manager_remove_item(window->priv->manager, gtk_recent_info_get_uri(iter->data), NULL);
        }
        iter = g_list_next(iter);
    }
    return FALSE;
}


GtkWidget *
rstto_main_window_new()
{
    GtkWidget *widget;

    widget = g_object_new(RSTTO_TYPE_MAIN_WINDOW, NULL);

    return widget;
}

void
rstto_main_window_set_thumbnail_viewer_orientation(RsttoMainWindow *window, GtkOrientation orientation)
{
    window->priv->settings.thumbnail_viewer_orientation = orientation;

    gtk_widget_ref(window->priv->thumbnail_viewer);
    gtk_widget_ref(window->priv->containers.s_window);
    gtk_container_remove(GTK_CONTAINER(window->priv->containers.paned), window->priv->thumbnail_viewer);
    gtk_container_remove(GTK_CONTAINER(window->priv->containers.paned), window->priv->containers.s_window);
    gtk_widget_destroy(window->priv->containers.paned);

    rstto_thumbnail_bar_set_orientation(RSTTO_THUMBNAIL_BAR(window->priv->thumbnail_viewer), orientation);

    switch (orientation)
    {
        case GTK_ORIENTATION_HORIZONTAL:
            window->priv->containers.paned = gtk_vbox_new(FALSE, 0);
            break;
        case GTK_ORIENTATION_VERTICAL:
            window->priv->containers.paned = gtk_hbox_new(FALSE, 0);
            break;
    }
    
    gtk_box_pack_start(GTK_BOX(window->priv->containers.paned), window->priv->containers.s_window, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(window->priv->containers.paned), window->priv->thumbnail_viewer, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(window->priv->containers.main_vbox), window->priv->containers.paned, TRUE, TRUE, 0);
    gtk_box_reorder_child(GTK_BOX(window->priv->containers.main_vbox), window->priv->containers.paned, -2);
    gtk_widget_show(window->priv->containers.paned);

}

void
rstto_main_window_set_show_thumbnail_viewer(RsttoMainWindow *window, gboolean visibility)
{
    window->priv->settings.thumbnail_viewer_visibility = visibility;
    if (visibility == TRUE)
    {
        switch(rstto_thumbnail_bar_get_orientation(RSTTO_THUMBNAIL_BAR(window->priv->thumbnail_viewer)))
        {
            case GTK_ORIENTATION_HORIZONTAL:
                gtk_check_menu_item_set_active(
                    GTK_CHECK_MENU_ITEM(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_horizontal),
                    TRUE);
                break;
            case GTK_ORIENTATION_VERTICAL:
                gtk_check_menu_item_set_active(
                    GTK_CHECK_MENU_ITEM(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_vertical),
                    TRUE);
                break;
        }
        
    }
    else
    {
        gtk_check_menu_item_set_active(
            GTK_CHECK_MENU_ITEM(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_hide),
            TRUE);
    }
}

void
rstto_main_window_set_show_toolbar (RsttoMainWindow *window, gboolean visibility)
{
    window->priv->settings.toolbar_visibility = visibility;
    gtk_check_menu_item_set_active(
            GTK_CHECK_MENU_ITEM(window->priv->menus.view.menu_item_show_toolbar),
            visibility);
}

gboolean
rstto_main_window_get_show_toolbar (RsttoMainWindow *window)
{
    return window->priv->settings.toolbar_visibility;
}

gboolean
rstto_main_window_get_show_thumbnail_viewer (RsttoMainWindow *window)
{
    return window->priv->settings.thumbnail_viewer_visibility;
}

GtkOrientation
rstto_main_window_get_thumbnail_viewer_orientation (RsttoMainWindow *window)
{
    return window->priv->settings.thumbnail_viewer_orientation;
}

RsttoNavigator *
rstto_main_window_get_navigator (RsttoMainWindow *window)
{
    return window->priv->navigator;
}

GtkRecentManager *
rstto_main_window_get_recent_manager (RsttoMainWindow *window)
{
    return window->priv->manager;
}

gdouble
rstto_main_window_get_slideshow_timeout (RsttoMainWindow *window)
{
    return window->priv->settings.slideshow_timeout;
}

gint
rstto_main_window_get_max_cache_size (RsttoMainWindow *window)
{
    return window->priv->settings.max_cache_size;
}

void
rstto_main_window_set_slideshow_timeout (RsttoMainWindow *window, gdouble timeout)
{
    window->priv->settings.slideshow_timeout = timeout;
    rstto_navigator_set_timeout(window->priv->navigator, timeout);
}

void
rstto_main_window_set_max_cache_size (RsttoMainWindow *window, gint max_cache_size)
{
    window->priv->settings.max_cache_size = max_cache_size;
    rstto_navigator_set_max_history_size(window->priv->navigator, max_cache_size * 1000000);
}

void
rstto_main_window_set_scale_to_100 (RsttoMainWindow *window, gboolean scale_to_100)
{
    window->priv->settings.scale_to_100 = scale_to_100;
}

gboolean
rstto_main_window_get_scale_to_100 (RsttoMainWindow *window)
{
    return window->priv->settings.scale_to_100;
}

/* CALLBACK FUNCTIONS */

static void
cb_rstto_main_window_thumbnail_viewer_horizontal(GtkWidget *widget, RsttoMainWindow *window)
{
    window->priv->settings.thumbnail_viewer_visibility = TRUE;
    gtk_widget_show(window->priv->thumbnail_viewer);
    rstto_main_window_set_thumbnail_viewer_orientation(window, GTK_ORIENTATION_HORIZONTAL);
}

static void
cb_rstto_main_window_thumbnail_viewer_vertical(GtkWidget *widget, RsttoMainWindow *window)
{
    window->priv->settings.thumbnail_viewer_visibility = TRUE;
    gtk_widget_show(window->priv->thumbnail_viewer);
    rstto_main_window_set_thumbnail_viewer_orientation(window, GTK_ORIENTATION_VERTICAL);
}

static void
cb_rstto_main_window_thumbnail_viewer_hide(GtkWidget *widget, RsttoMainWindow *window)
{
    window->priv->settings.thumbnail_viewer_visibility = FALSE;
    gtk_widget_hide(window->priv->thumbnail_viewer);
}

static void
cb_rstto_main_window_toggle_fullscreen(GtkWidget *widget, RsttoMainWindow *window)
{
    if(gdk_window_get_state(GTK_WIDGET(window)->window) & GDK_WINDOW_STATE_FULLSCREEN)
    {
        gtk_window_unfullscreen(GTK_WINDOW(window));
    }
    else
    {
        gtk_window_fullscreen(GTK_WINDOW(window));
    }
}

static void
cb_rstto_main_window_toggle_toolbar(GtkWidget *widget, RsttoMainWindow *window)
{
    gboolean active = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM(widget));

    window->priv->settings.toolbar_visibility = active;

    if (active == TRUE)
    {
        gtk_widget_show(window->priv->toolbar.bar);
    }
    else
    {
        gtk_widget_hide(window->priv->toolbar.bar);
    }
}

#ifdef WITH_DESKTOP_WALLPAPER
static void
cb_rstto_main_window_set_wallpaper(GtkWidget *widget, RsttoMainWindow *window)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(window->priv->navigator);
    ThunarVfsInfo *info = rstto_navigator_entry_get_info(entry);
    gchar *path = thunar_vfs_path_dup_string(info->path);


    switch (window->priv->settings.desktop)
    {
#ifdef HAVE_XFCONF
        case RSTTO_DESKTOP_XFCE:
            {
                XfconfChannel *xfdesktop_channel = xfconf_channel_new("xfdesktop");
                if(xfconf_channel_set_string(xfdesktop_channel, "image_path_0_0", path) == FALSE)
                {
                    /** FAILED */
                }
            }
            break;
#endif
        default:
            g_debug("not supported");
            break;
    }
    g_free(path);
}
#endif

static gboolean
cb_rstto_main_window_key_press_event(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW(widget);
    RsttoMainWindow *rstto_window = RSTTO_MAIN_WINDOW(widget);
    if(gtk_window_activate_key(window, event) == FALSE)
    {
        switch(event->keyval)
        {
            case GDK_F5:
                if (rstto_navigator_is_running(RSTTO_NAVIGATOR(rstto_window->priv->navigator)))
                {
                    cb_rstto_main_window_pause(rstto_window->priv->menus.go.menu_item_pause, rstto_window);
                }
                else
                {
                    cb_rstto_main_window_play(rstto_window->priv->menus.go.menu_item_play, rstto_window);
                }
                break;
            case GDK_F11:
                if(gdk_window_get_state(widget->window) & GDK_WINDOW_STATE_FULLSCREEN)
                {
                    gtk_window_unfullscreen(window);
                }
                else
                {
                    gtk_window_fullscreen(window);
                }
                break;
            case GDK_Escape:
                if(gdk_window_get_state(widget->window) & GDK_WINDOW_STATE_FULLSCREEN)
                {
                    gtk_window_unfullscreen(window);
                }
                break;
            case GDK_Home:
                rstto_navigator_jump_first(rstto_window->priv->navigator);
                break;
            case GDK_End:
                rstto_navigator_jump_last(rstto_window->priv->navigator);
                break;
            case GDK_Page_Down:
            case GDK_space:
                rstto_navigator_jump_forward(rstto_window->priv->navigator);
                break;
            case GDK_Page_Up:
            case GDK_BackSpace:
                rstto_navigator_jump_back(rstto_window->priv->navigator);
                break;
            case GDK_t:
                rstto_main_window_set_show_thumbnail_viewer(RSTTO_MAIN_WINDOW(window),
                        !(RSTTO_MAIN_WINDOW(window)->priv->settings.thumbnail_viewer_visibility));
                break;
            case GDK_bracketleft:
                cb_rstto_main_window_rotate_ccw(NULL, RSTTO_MAIN_WINDOW(window));
                break;
            case GDK_bracketright:
                cb_rstto_main_window_rotate_cw(NULL, RSTTO_MAIN_WINDOW(window));
                break;
        }
    }
    return TRUE;
}

static void
cb_rstto_main_window_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
    RsttoMainWindow *window = RSTTO_MAIN_WINDOW(widget);
    if(event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
    {
        if(event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN)
        {
            gtk_widget_hide(window->priv->menus.menu);
            gtk_widget_hide(window->priv->toolbar.bar);
            gtk_widget_hide(window->priv->statusbar);
            GdkColor *color = g_new0(GdkColor, 1);

            rstto_picture_viewer_set_bg_color(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), color);

            g_free(color);
        }
        else
        {
            gtk_widget_show(window->priv->menus.menu);
            gtk_widget_show(window->priv->statusbar);
            rstto_picture_viewer_set_bg_color(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), window->priv->settings.bg_color);

            if (window->priv->settings.toolbar_visibility == TRUE)
            {
                gtk_widget_show(window->priv->toolbar.bar);
            }
        }
    }
    if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED)
    {
        RsttoNavigatorEntry *entry = rstto_navigator_get_file(window->priv->navigator);

        if (window->priv->settings.scale_to_100 == TRUE)
        {
            rstto_picture_viewer_set_zoom_mode(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), RSTTO_ZOOM_MODE_CUSTOM);
            rstto_navigator_entry_set_scale(entry, 0);
        }

    }
}

static void
cb_rstto_main_window_play(GtkWidget *widget, RsttoMainWindow *window)
{
    gtk_widget_ref(widget);
    gtk_container_remove(GTK_CONTAINER(window->priv->menus.go.menu), widget);
    gtk_menu_shell_insert(GTK_MENU_SHELL(window->priv->menus.go.menu), window->priv->menus.go.menu_item_pause, 5);
    gtk_widget_show_all(window->priv->menus.go.menu_item_pause);
    rstto_navigator_set_running(RSTTO_NAVIGATOR(window->priv->navigator), TRUE);
}

static void
cb_rstto_main_window_pause(GtkWidget *widget, RsttoMainWindow *window)
{
    gtk_widget_ref(widget);
    gtk_container_remove(GTK_CONTAINER(window->priv->menus.go.menu), widget);
    gtk_menu_shell_insert(GTK_MENU_SHELL(window->priv->menus.go.menu), window->priv->menus.go.menu_item_play, 5);
    gtk_widget_show_all(window->priv->menus.go.menu_item_play);
    rstto_navigator_set_running(RSTTO_NAVIGATOR(window->priv->navigator), FALSE);
}

static void
cb_rstto_main_window_preferences(GtkWidget *widget, RsttoMainWindow *window)
{
    GdkColor  *color = NULL;
    if (rstto_picture_viewer_get_bg_color(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer)))
    {
        color = gdk_color_copy(rstto_main_window_get_pv_bg_color(window));
    }
    GtkWidget *slideshow_main_vbox;
    GtkWidget *slideshow_main_lbl;
    GtkWidget *display_main_vbox;
    GtkWidget *display_main_lbl;
    GtkWidget *behaviour_main_vbox;
    GtkWidget *behaviour_main_lbl;

    GtkWidget *resize_to_content_vbox, *resize_to_content_frame;
    GtkWidget *resize_on_maximize_check;

    GtkWidget *bg_color_vbox;
    GtkWidget *bg_color_hbox;
    GtkWidget *bg_color_frame;
    GtkWidget *bg_color_button;
    GtkWidget *bg_color_override_check;
    GtkWidget *cache_vbox;
    GtkWidget *cache_frame;
    GtkWidget *cache_hbox;
    GtkObject *cache_adjustment;
    GtkWidget *cache_spin_button;
    GtkWidget *cache_label;
    GtkWidget *cache_mb_label;

    cache_adjustment = gtk_adjustment_new(rstto_main_window_get_max_cache_size(window), 0, 9999, 1, 100, 100);

    GtkWidget *dialog = xfce_titled_dialog_new_with_buttons(_("Image viewer Preferences"),
                                                    GTK_WINDOW(window),
                                                    GTK_DIALOG_NO_SEPARATOR,
                                                    GTK_STOCK_CANCEL,
                                                    GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_OK,
                                                    GTK_RESPONSE_OK,
                                                    NULL);
    gtk_window_set_icon_name(GTK_WINDOW(dialog), "ristretto");
    gtk_window_set_resizable(GTK_WINDOW(dialog), FALSE);

    GtkWidget *notebook = gtk_notebook_new();
    gtk_container_set_border_width(GTK_CONTAINER(notebook), 6);

    gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), notebook,  TRUE, TRUE, 0);

/** Add notebook pages */
    behaviour_main_vbox = gtk_vbox_new(FALSE, 0);
    behaviour_main_lbl = gtk_label_new(_("Behaviour"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), behaviour_main_vbox, behaviour_main_lbl);

    slideshow_main_vbox = gtk_vbox_new(FALSE, 0);
    slideshow_main_lbl = gtk_label_new(_("Slideshow"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), slideshow_main_vbox, slideshow_main_lbl);

    display_main_vbox = gtk_vbox_new(FALSE, 0);
    display_main_lbl = gtk_label_new(_("Display"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), display_main_vbox, display_main_lbl);

/** Add content for behaviour page */
    resize_to_content_vbox = gtk_vbox_new(FALSE, 0);
    resize_to_content_frame = xfce_create_framebox_with_content(_("Scaling"), resize_to_content_vbox);

    resize_on_maximize_check = gtk_check_button_new_with_mnemonic(_("Don't scale over 100% when maximizing the window."));
    gtk_box_pack_start(GTK_BOX(resize_to_content_vbox), resize_on_maximize_check, FALSE, TRUE, 0);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(resize_on_maximize_check), window->priv->settings.scale_to_100);

    gtk_container_set_border_width (GTK_CONTAINER (resize_to_content_frame), 8);
    gtk_box_pack_start(GTK_BOX(behaviour_main_vbox), resize_to_content_frame, FALSE, TRUE, 0);

/** Add content for display page */
    bg_color_vbox = gtk_vbox_new(FALSE, 0);
    bg_color_frame = xfce_create_framebox_with_content (_("Background Color"), bg_color_vbox);

    bg_color_override_check = gtk_check_button_new_with_mnemonic(_("_Override Background Color:"));
    bg_color_hbox = gtk_hbox_new(FALSE, 4);
    bg_color_button = gtk_color_button_new();

    gtk_box_pack_start(GTK_BOX(bg_color_hbox), bg_color_override_check, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(bg_color_hbox), bg_color_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bg_color_vbox), bg_color_hbox, FALSE, FALSE, 0);

    g_signal_connect(G_OBJECT(bg_color_override_check), "toggled", (GCallback)cb_rstto_bg_color_override_check_toggled, bg_color_button);

    if (color)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bg_color_override_check), TRUE);
        gtk_color_button_set_color(GTK_COLOR_BUTTON(bg_color_button), color);
        gdk_color_free(color);
        color = NULL;
    }
    else
    {
        gtk_widget_set_sensitive(bg_color_button, FALSE);
    }

    cache_vbox = gtk_vbox_new(FALSE, 0);
    cache_frame = xfce_create_framebox_with_content (_("Image Cache"), cache_vbox);
    cache_hbox = gtk_hbox_new(FALSE, 4);
    cache_spin_button = gtk_spin_button_new(GTK_ADJUSTMENT(cache_adjustment), 1.0, 0);
    cache_label = gtk_label_new(_("Cache size:"));
    cache_mb_label = gtk_label_new(_("MB"));

    gtk_box_pack_start(GTK_BOX(cache_hbox), cache_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cache_hbox), cache_spin_button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cache_hbox), cache_mb_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(cache_vbox), cache_hbox, FALSE, FALSE, 0);


    gtk_container_set_border_width (GTK_CONTAINER (bg_color_frame), 8);
    gtk_container_set_border_width (GTK_CONTAINER (cache_frame), 8);

    gtk_box_pack_start(GTK_BOX(display_main_vbox), bg_color_frame, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(display_main_vbox), cache_frame, FALSE, TRUE, 0);

/** Add content for slideshow page */
    GtkWidget *slideshow_vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *slideshow_frame = xfce_create_framebox_with_content (_("Timeout"), slideshow_vbox);

    GtkWidget *preload_vbox = gtk_vbox_new(FALSE, 0);
    GtkWidget *preload_frame = xfce_create_framebox_with_content (_("Preload"), preload_vbox);

    gtk_container_set_border_width (GTK_CONTAINER (slideshow_frame), 8);
    gtk_container_set_border_width (GTK_CONTAINER (preload_frame), 8);

    GtkWidget *slideshow_lbl = gtk_label_new(_("The time period an individual image is displayed during a slideshow\n(in seconds)"));
    GtkWidget *slideshow_hscale = gtk_hscale_new_with_range(1, 60, 1);


    GtkWidget *preload_lbl = gtk_label_new(_("Preload images during slideshow\n(uses more memory)"));
    GtkWidget *preload_check = gtk_check_button_new_with_mnemonic(_("_Preload images"));

    gtk_misc_set_alignment(GTK_MISC(slideshow_lbl), 0, 0.5);
    gtk_misc_set_alignment(GTK_MISC(preload_lbl), 0, 0.5);

    gtk_misc_set_padding(GTK_MISC(slideshow_lbl), 2, 2);
    gtk_misc_set_padding(GTK_MISC(preload_lbl), 2, 2);
    
    gtk_range_set_value(GTK_RANGE(slideshow_hscale), window->priv->settings.slideshow_timeout / 1000);

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(preload_check), window->priv->navigator->preload);

    gtk_box_pack_start(GTK_BOX(slideshow_vbox), slideshow_lbl, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(slideshow_vbox), slideshow_hscale, FALSE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(preload_vbox), preload_lbl, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(preload_vbox), preload_check, FALSE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(slideshow_main_vbox), slideshow_frame, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(slideshow_main_vbox), preload_frame, FALSE, TRUE, 0);

    gtk_widget_show_all(notebook);

    gint result = gtk_dialog_run(GTK_DIALOG(dialog));

    switch (result)
    {
        case GTK_RESPONSE_OK:
            rstto_main_window_set_slideshow_timeout(window, gtk_range_get_value(GTK_RANGE(slideshow_hscale)) * 1000);
            window->priv->navigator->preload = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(preload_check));
            if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(bg_color_override_check)) == TRUE)
            {
                GdkColor *new_color = g_new0(GdkColor, 1);
                gtk_color_button_get_color(GTK_COLOR_BUTTON(bg_color_button), new_color);
                rstto_main_window_set_pv_bg_color(window, new_color);
                g_free(new_color);
            }
            else
            {
                rstto_main_window_set_pv_bg_color(window, NULL);
            }
            rstto_picture_viewer_redraw(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer));
            rstto_main_window_set_max_cache_size(window, GTK_ADJUSTMENT(cache_adjustment)->value);

            window->priv->settings.scale_to_100 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(resize_on_maximize_check));
        default:
            break;
    }

    gtk_widget_destroy(dialog);
}

static void
cb_rstto_main_window_about(GtkWidget *widget, RsttoMainWindow *window)
{
    const gchar *authors[] = {
      _("Developer:"),
        "Stephan Arts <stephan@xfce.org>",
        NULL};

    GtkWidget *about_dialog = gtk_about_dialog_new();

    gtk_about_dialog_set_name((GtkAboutDialog *)about_dialog, PACKAGE_NAME);
    gtk_about_dialog_set_version((GtkAboutDialog *)about_dialog, PACKAGE_VERSION);

    gtk_about_dialog_set_comments((GtkAboutDialog *)about_dialog,
        _("Ristretto is a fast and lightweight picture-viewer for the Xfce desktop environment."));
    gtk_about_dialog_set_website((GtkAboutDialog *)about_dialog,
        "http://goodies.xfce.org/projects/applications/ristretto");
    gtk_about_dialog_set_logo_icon_name((GtkAboutDialog *)about_dialog,
        "ristretto");
    gtk_about_dialog_set_authors((GtkAboutDialog *)about_dialog,
        authors);
    gtk_about_dialog_set_translator_credits((GtkAboutDialog *)about_dialog,
        _("translator-credits"));
    gtk_about_dialog_set_license((GtkAboutDialog *)about_dialog,
        xfce_get_license_text(XFCE_LICENSE_TEXT_GPL));
    gtk_about_dialog_set_copyright((GtkAboutDialog *)about_dialog,
        "Copyright \302\251 2006-2007 Stephan Arts");

    gtk_dialog_run(GTK_DIALOG(about_dialog));

    gtk_widget_destroy(about_dialog);
}

static void
cb_rstto_main_window_quit(GtkWidget *widget, RsttoMainWindow *window)
{
    gtk_widget_destroy(GTK_WIDGET(window));
}

static void
cb_rstto_main_window_open_file(GtkWidget *widget, RsttoMainWindow *window)
{
    GtkStatusbar *statusbar = GTK_STATUSBAR(window->priv->statusbar);
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&statusbar);

    gint context_id = gtk_statusbar_get_context_id(statusbar, "StatusMessages");
    gint message_id = gtk_statusbar_push(statusbar, context_id, N_("Opening file(s)..."));

    GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Open image"),
                                                    GTK_WINDOW(window),
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_OPEN, GTK_RESPONSE_OK,
                                                    NULL);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if(response == GTK_RESPONSE_OK)
    {
        const gchar *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        if (!rstto_navigator_open_file(window->priv->navigator, filename, FALSE, NULL))
        {
            gtk_widget_destroy(dialog);
            dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                            GTK_DIALOG_MODAL,
                                            GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_OK,
                                            _("Could not open file"));
            gtk_dialog_run(GTK_DIALOG(dialog));
        }
    }
    gtk_widget_destroy(dialog);

    if (statusbar)
    {
        gtk_statusbar_remove(statusbar, context_id, message_id);
        g_object_remove_weak_pointer(G_OBJECT(window), (gpointer *)&statusbar);
    }
}

static void
cb_rstto_main_window_open_folder(GtkWidget *widget, RsttoMainWindow *window)
{
    GtkStatusbar *statusbar = GTK_STATUSBAR(window->priv->statusbar);
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&statusbar);

    gint context_id = gtk_statusbar_get_context_id(statusbar, "StatusMessages");
    gint message_id = gtk_statusbar_push(statusbar, context_id, N_("Opening file(s)..."));

    GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Open folder"),
                                                    GTK_WINDOW(window),
                                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_OPEN, GTK_RESPONSE_OK,
                                                    NULL);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if(response == GTK_RESPONSE_OK)
    {
        gtk_widget_hide(dialog);
        const gchar *dir_name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));

        RsttoNavigator *navigator = window->priv->navigator;
        if(rstto_navigator_open_folder(navigator, dir_name, TRUE, NULL) == TRUE)
        {
            rstto_navigator_jump_first(navigator);
        }
    }
    gtk_widget_destroy(dialog);

    if (statusbar)
    {
        gtk_statusbar_remove(statusbar, context_id, message_id);
        g_object_remove_weak_pointer(G_OBJECT(window), (gpointer *)&statusbar);
    }
}

static void
cb_rstto_main_window_open_recent(GtkRecentChooser *chooser, RsttoMainWindow *window)
{
    GtkStatusbar *statusbar = GTK_STATUSBAR(window->priv->statusbar);
    g_object_add_weak_pointer(G_OBJECT(window), (gpointer *)&statusbar);

    gint context_id = gtk_statusbar_get_context_id(statusbar, "StatusMessages");
    gint message_id = gtk_statusbar_push(statusbar, context_id, N_("Opening file(s)..."));

    gchar *uri = gtk_recent_chooser_get_current_uri(chooser);
    ThunarVfsPath *vfs_path = thunar_vfs_path_new(uri, NULL);
    if (vfs_path)
    {
        gchar *path = thunar_vfs_path_dup_string(vfs_path);
        if(g_file_test(path, G_FILE_TEST_EXISTS))
        {
            if(g_file_test(path, G_FILE_TEST_IS_DIR))
            {
                RsttoNavigator *navigator = window->priv->navigator;
                if(rstto_navigator_open_folder(navigator, path, TRUE, NULL) == TRUE)
                {
                    rstto_navigator_jump_first(navigator);
                }
            }
            else
            {
                rstto_navigator_open_file(window->priv->navigator, path, FALSE, NULL);
            }
        }
        thunar_vfs_path_unref(vfs_path);
    }

    if (statusbar)
    {
        gtk_statusbar_remove(statusbar, context_id, message_id);
        g_object_remove_weak_pointer(G_OBJECT(window), (gpointer *)&statusbar);
    }
}

static void
cb_rstto_main_window_clear_recent(GtkWidget *widget, RsttoMainWindow *window)
{
    GtkWidget *dialog = gtk_message_dialog_new(NULL,
                                    GTK_DIALOG_MODAL,
                                    GTK_MESSAGE_ERROR,
                                    GTK_BUTTONS_OK_CANCEL,
                                    _("Are you sure you want to clear ristretto's list of recently opened documents?"));
    gint result = gtk_dialog_run(GTK_DIALOG(dialog));
    if (result == GTK_RESPONSE_OK)
    {
        g_timeout_add(150, (GSourceFunc)rstto_main_window_clear_recent, window);
    }
    gtk_widget_destroy(dialog);

}

static void
cb_rstto_main_window_close(GtkWidget *widget, RsttoMainWindow *window)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(window->priv->navigator);
    if (entry)
    {
        rstto_navigator_remove(window->priv->navigator, entry);    
        rstto_navigator_entry_free(entry);
        if (rstto_navigator_get_n_files(window->priv->navigator) == 0)
        {
            gtk_widget_set_sensitive(widget, FALSE);
        }
    }
    else
    {
        gtk_widget_set_sensitive(widget, FALSE);
    }
}

static void
cb_rstto_main_window_file_properties(GtkWidget *widget, RsttoMainWindow *window)
{
    GError *error = NULL;
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(window->priv->navigator);
    if (entry)
    {
        ThunarVfsInfo *info = rstto_navigator_entry_get_info(entry);
        if(info)
        {
            gchar *uri = thunar_vfs_path_dup_uri(info->path);
            if(dbus_g_proxy_call(window->priv->filemanager_proxy,
                                 "DisplayFileProperties",
                                 &error,
                                 G_TYPE_STRING, uri,
                                 G_TYPE_STRING, "",
                                 G_TYPE_INVALID,
                                 G_TYPE_INVALID) == FALSE)
            {
                g_warning("%s", error->message);
            }
            g_free(uri);
        }
    }
}

static void
cb_rstto_main_window_nav_iter_changed(RsttoNavigator *navigator, gint nr, RsttoNavigatorEntry *entry, RsttoMainWindow *window)
{
    ThunarVfsInfo *info = NULL;
    const gchar *filename = NULL;
    gchar *title = NULL;

    if(entry)
    {
        info = rstto_navigator_entry_get_info(entry);
        filename = info->display_name;
        gtk_widget_set_sensitive(window->priv->menus.file.menu_item_close, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.file.menu_item_file_properties, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_first, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_last, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_previous, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_next, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_play, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_pause, TRUE);

#ifdef WITH_DESKTOP_WALLPAPER 
        gtk_widget_set_sensitive(window->priv->menus.view.menu_item_set_wallpaper, TRUE);
#endif

        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_next), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_previous), TRUE);

        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_in), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_out), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_fit), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_100), TRUE);

        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_close), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_in), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_out), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_100), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_fit), TRUE);

        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_in), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_out), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_100), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_fit), TRUE);

        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.rotate.menu_item_rotate_cw), TRUE);
        gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.rotate.menu_item_rotate_ccw), TRUE);


        /* Update window title */
        if (rstto_navigator_get_n_files(navigator) > 1)
        {
            title = g_strdup_printf("%s - %s [%d/%d]", PACKAGE_NAME, filename, nr+1, rstto_navigator_get_n_files(navigator));
        }
        else
        {
            title = g_strconcat(PACKAGE_NAME, " - ", filename, NULL);
        }
        gtk_window_set_title(GTK_WINDOW(window), title);
        g_free(title);
        title = NULL;

        /* Update 'open with...' submenu */
        if(gtk_widget_get_parent(window->priv->menus.edit.open_with.menu_item_empty))
        {
            gtk_container_remove(GTK_CONTAINER(window->priv->menus.edit.open_with.menu),
                                 window->priv->menus.edit.open_with.menu_item_empty);
        }
        if(gtk_widget_get_parent(window->priv->menus._picture_viewer.open_with.menu_item_empty))
        {
            gtk_container_remove(GTK_CONTAINER(window->priv->menus._picture_viewer.open_with.menu),
                                 window->priv->menus._picture_viewer.open_with.menu_item_empty);
        }

        gtk_container_foreach(GTK_CONTAINER(window->priv->menus.edit.open_with.menu), (GtkCallback)gtk_widget_destroy, NULL);
        gtk_container_foreach(GTK_CONTAINER(window->priv->menus._picture_viewer.open_with.menu), (GtkCallback)gtk_widget_destroy, NULL);

        if (info)
        {
            window->priv->menu_apps_list = thunar_vfs_mime_database_get_applications(window->priv->mime_dbase, info->mime_info);
            GList *iter = window->priv->menu_apps_list;
            if (iter == NULL)
            {
                gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.open_with.menu), window->priv->menus.edit.open_with.menu_item_empty);
                gtk_widget_show(window->priv->menus.edit.open_with.menu_item_empty);

                gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.open_with.menu),
                                      window->priv->menus._picture_viewer.open_with.menu_item_empty);
                gtk_widget_show(window->priv->menus._picture_viewer.open_with.menu_item_empty);
            }
            while (iter != NULL)
            {
                GtkWidget *menu_item = gtk_image_menu_item_new_with_label(thunar_vfs_mime_application_get_name(iter->data));
                GtkWidget *image = gtk_image_new_from_icon_name(thunar_vfs_mime_handler_lookup_icon_name(iter->data, window->priv->icon_theme), GTK_ICON_SIZE_MENU);
                gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item), image);
                gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.open_with.menu), menu_item);
                g_object_set_data(iter->data, "entry", entry);
                g_signal_connect(menu_item, "activate", G_CALLBACK(cb_rstto_main_window_spawn_app), iter->data);
                gtk_widget_show(menu_item);

                menu_item = gtk_image_menu_item_new_with_label(thunar_vfs_mime_application_get_name(iter->data));
                image = gtk_image_new_from_icon_name(thunar_vfs_mime_handler_lookup_icon_name(iter->data, window->priv->icon_theme), GTK_ICON_SIZE_MENU);
                gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item), image);
                gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.open_with.menu), menu_item);
                g_object_set_data(iter->data, "entry", entry);
                g_signal_connect(menu_item, "activate", G_CALLBACK(cb_rstto_main_window_spawn_app), iter->data);
                gtk_widget_show(menu_item);

                iter = g_list_next(iter);
            }
        }        
        else
        {
            gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.open_with.menu), window->priv->menus.edit.open_with.menu_item_empty);
            gtk_widget_show(window->priv->menus.edit.open_with.menu_item_empty);

            gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.open_with.menu),
                                  window->priv->menus._picture_viewer.open_with.menu_item_empty);
            gtk_widget_show(window->priv->menus._picture_viewer.open_with.menu_item_empty);
        }
    }
    else
    {
        gtk_widget_set_sensitive(window->priv->menus.file.menu_item_close, FALSE);
        gtk_window_set_title(GTK_WINDOW(window), PACKAGE_STRING);
        if (rstto_navigator_get_n_files(window->priv->navigator) == 0)
        {
            gtk_widget_set_sensitive(window->priv->menus.go.menu_item_first, FALSE);
            gtk_widget_set_sensitive(window->priv->menus.go.menu_item_last, FALSE);
            gtk_widget_set_sensitive(window->priv->menus.go.menu_item_previous, FALSE);
            gtk_widget_set_sensitive(window->priv->menus.go.menu_item_next, FALSE);
            gtk_widget_set_sensitive(window->priv->menus.go.menu_item_play, FALSE);
            gtk_widget_set_sensitive(window->priv->menus.go.menu_item_pause, FALSE);
            gtk_widget_set_sensitive(window->priv->menus.file.menu_item_file_properties, FALSE);

            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_next), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_previous), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_in), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_out), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_fit), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->toolbar.tool_item_zoom_100), FALSE);

            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_close), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_in), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_out), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_100), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus._picture_viewer.menu_item_zoom_fit), FALSE);

            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_in), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_out), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_100), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.zooming.menu_item_zoom_fit), FALSE);

            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.rotate.menu_item_rotate_cw), FALSE);
            gtk_widget_set_sensitive(GTK_WIDGET(window->priv->menus.view.rotate.menu_item_rotate_ccw), FALSE);
        }

        gtk_container_foreach(GTK_CONTAINER(window->priv->menus.edit.open_with.menu), (GtkCallback)gtk_widget_destroy, NULL);
        gtk_container_foreach(GTK_CONTAINER(window->priv->menus._picture_viewer.open_with.menu), (GtkCallback)gtk_widget_destroy, NULL);
        if(!gtk_widget_get_parent(window->priv->menus.edit.open_with.menu_item_empty))
        {
            gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.open_with.menu), window->priv->menus.edit.open_with.menu_item_empty);
            gtk_widget_show(window->priv->menus.edit.open_with.menu_item_empty);
        }

        if(!gtk_widget_get_parent(window->priv->menus._picture_viewer.open_with.menu_item_empty))
        {
            gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus._picture_viewer.open_with.menu),
                                  window->priv->menus._picture_viewer.open_with.menu_item_empty);
            gtk_widget_show(window->priv->menus._picture_viewer.open_with.menu_item_empty);
        }
    }

}

static void
cb_rstto_main_window_nav_new_entry(RsttoNavigator *navigator, gint nr, RsttoNavigatorEntry *entry, RsttoMainWindow *window)
{
    RsttoNavigatorEntry *current_entry = rstto_navigator_get_file(navigator);
    ThunarVfsInfo *info = rstto_navigator_entry_get_info(entry);
    gchar *filename = info->display_name;

    gint current_nr = rstto_navigator_get_position(navigator);

    if (current_entry)
    {
        gchar *title;
        /* Update window title */
        if (rstto_navigator_get_n_files(navigator) > 1)
        {
            title = g_strdup_printf("%s - %s [%d/%d]", PACKAGE_NAME, filename, current_nr+1, rstto_navigator_get_n_files(navigator));
        }
        else
        {
            title = g_strconcat(PACKAGE_NAME, " - ", filename, NULL);
        }
        gtk_window_set_title(GTK_WINDOW(window), title);
        g_free(title);
        title = NULL;
    }

}

static void
cb_rstto_main_window_next(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_navigator_jump_forward(window->priv->navigator);
}

static void
cb_rstto_main_window_previous(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_navigator_jump_back(window->priv->navigator);
}

static void
cb_rstto_main_window_first(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_navigator_jump_first(window->priv->navigator);
}

static void
cb_rstto_main_window_last(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_navigator_jump_last(window->priv->navigator);
}

static void
cb_rstto_main_window_zoom_in(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_set_zoom_mode(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), RSTTO_ZOOM_MODE_CUSTOM);
    gdouble scale = rstto_picture_viewer_get_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer));
    rstto_picture_viewer_set_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), scale * window->priv->zoom_factor);
}

static void
cb_rstto_main_window_zoom_out(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_set_zoom_mode(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), RSTTO_ZOOM_MODE_CUSTOM);
    gdouble scale = rstto_picture_viewer_get_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer));
    rstto_picture_viewer_set_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), scale / window->priv->zoom_factor);
}

static void
cb_rstto_main_window_zoom_100(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_set_zoom_mode(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), RSTTO_ZOOM_MODE_100);
    rstto_picture_viewer_set_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), 1);
}

static void
cb_rstto_main_window_zoom_fit(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_set_zoom_mode(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), RSTTO_ZOOM_MODE_FIT);
    rstto_picture_viewer_fit_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer));
}

static void
cb_rstto_main_window_rotate_cw(GtkWidget *widget, RsttoMainWindow *window)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(window->priv->navigator);
    if (entry)
    {
        GdkPixbufRotation rotation = rstto_navigator_entry_get_rotation(entry);
        switch (rotation)
        {
            case GDK_PIXBUF_ROTATE_NONE:
                rotation = GDK_PIXBUF_ROTATE_CLOCKWISE;
                break;
            case GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE:
                rotation = GDK_PIXBUF_ROTATE_NONE;
                break;
            case GDK_PIXBUF_ROTATE_UPSIDEDOWN:
                rotation = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
                break;
            case GDK_PIXBUF_ROTATE_CLOCKWISE:
                rotation = GDK_PIXBUF_ROTATE_UPSIDEDOWN;
                break;
            default:
                g_warning("Unable to rotate, rotation unknown");
                break;
        }
        rstto_navigator_entry_set_rotation(entry, rotation);
    }
}

static void
cb_rstto_main_window_rotate_ccw(GtkWidget *widget, RsttoMainWindow *window)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(window->priv->navigator);
    if (entry)
    {
        GdkPixbufRotation rotation = rstto_navigator_entry_get_rotation(entry);
        switch (rotation)
        {
            case GDK_PIXBUF_ROTATE_NONE:
                rotation = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
                break;
            case GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE:
                rotation = GDK_PIXBUF_ROTATE_UPSIDEDOWN;
                break;
            case GDK_PIXBUF_ROTATE_UPSIDEDOWN:
                rotation = GDK_PIXBUF_ROTATE_CLOCKWISE;
                break;
            case GDK_PIXBUF_ROTATE_CLOCKWISE:
                rotation = GDK_PIXBUF_ROTATE_NONE;
                break;
            default:
                g_warning("Unable to rotate, rotation unknown");
                break;
        }
        rstto_navigator_entry_set_rotation(entry, rotation);
    }
}

static void
cb_rstto_main_window_spawn_app(GtkWidget *widget, ThunarVfsMimeApplication *app)
{
    ThunarVfsInfo *info = rstto_navigator_entry_get_info(g_object_get_data(G_OBJECT(app), "entry"));
    GList *list = g_list_prepend(NULL, info->path);
    thunar_vfs_mime_handler_exec(THUNAR_VFS_MIME_HANDLER(app), NULL, list, NULL);
}

void
rstto_main_window_set_pv_bg_color (RsttoMainWindow *window, const GdkColor *color)
{
    rstto_picture_viewer_set_bg_color(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), color);
    if (color)
    {
        window->priv->settings.bg_color = gdk_color_copy(color);
    }
    else
    {
        window->priv->settings.bg_color = NULL;
    }
}

const GdkColor *
rstto_main_window_get_pv_bg_color (RsttoMainWindow *window)
{
    return window->priv->settings.bg_color;
}

static void
cb_rstto_bg_color_override_check_toggled(GtkToggleButton *button, GtkWidget *widget)
{
    if (gtk_toggle_button_get_active(button) == TRUE)
    {
        gtk_widget_set_sensitive(widget, TRUE);
    }
    else
    {
        gtk_widget_set_sensitive(widget, FALSE);
    }
}

GtkStatusbar *
rstto_main_window_get_statusbar(RsttoMainWindow *window)
{
    return GTK_STATUSBAR(window->priv->statusbar);
}

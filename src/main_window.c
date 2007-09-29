/*
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

#include "navigator.h"
#include "thumbnail_viewer.h"
#include "picture_viewer.h"
#include "main_window.h"


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

    struct {
        GtkWidget *main_vbox;
        GtkWidget *s_window;
        GtkWidget *paned;
    } containers;

    struct {
        GtkOrientation thumbnail_viewer_orientation;
        gboolean       thumbnail_viewer_visibility;
        gboolean       toolbar_visibility;
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

            GtkWidget *menu_item_fullscreen;
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
cb_rstto_main_window_quit(GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_about(GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_main_window_nav_iter_changed(RsttoNavigator *navigator, gint nr, RsttoNavigatorEntry *entry, RsttoMainWindow *window);

static void
rstto_main_window_init(RsttoMainWindow *);
static void
rstto_main_window_class_init(RsttoMainWindowClass *);

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
    

    GtkAccelGroup *accel_group = gtk_accel_group_new();

    gtk_window_set_title(GTK_WINDOW(window), PACKAGE_STRING);
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);

    window->priv->navigator = rstto_navigator_new();
    window->priv->thumbnail_viewer = rstto_thumbnail_viewer_new(window->priv->navigator);
    window->priv->picture_viewer = rstto_picture_viewer_new(window->priv->navigator);

    window->priv->manager = gtk_recent_manager_get_default();

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
    window->priv->menus.file.menu_item_close = gtk_image_menu_item_new_from_stock(GTK_STOCK_CLOSE, accel_group);
    window->priv->menus.file.menu_item_quit = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, accel_group);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.menu_item_file), window->priv->menus.file.menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_open_file);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_open_folder);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_open_recently);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_separator_1);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_close);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.file.menu), window->priv->menus.file.menu_item_quit);

    gtk_widget_set_sensitive(window->priv->menus.file.menu_item_close, FALSE);

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

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.menu_item_edit), window->priv->menus.edit.menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.menu), window->priv->menus.edit.menu_item_open_with);
    
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

    window->priv->menus.view.menu_item_show_toolbar = gtk_check_menu_item_new_with_mnemonic(_("Show Toolbar"));
    window->priv->menus.view.menu_item_show_thumbnail_viewer = gtk_menu_item_new_with_mnemonic(_("Thumbnail Viewer"));
    window->priv->menus.view.menu_item_fullscreen = gtk_image_menu_item_new_from_stock(GTK_STOCK_FULLSCREEN, NULL);

    gtk_widget_add_accelerator(window->priv->menus.view.menu_item_fullscreen, "activate", accel_group, GDK_F11, 0,GTK_ACCEL_VISIBLE);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(window->priv->menus.view.menu_item_show_toolbar), TRUE);

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_show_toolbar);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_show_thumbnail_viewer);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.menu), window->priv->menus.view.menu_item_fullscreen);

    window->priv->menus.view.show_thumbnail_viewer.menu = gtk_menu_new();
    gtk_menu_set_accel_group(GTK_MENU(window->priv->menus.view.show_thumbnail_viewer.menu), accel_group);

    gtk_menu_item_set_submenu(GTK_MENU_ITEM(window->priv->menus.view.menu_item_show_thumbnail_viewer),
            window->priv->menus.view.show_thumbnail_viewer.menu);
    window->priv->menus.view.show_thumbnail_viewer.
            menu_item_thumbnail_viewer_horizontal = gtk_radio_menu_item_new_with_mnemonic(
                    NULL, 
                    _("Show Horizontally"));
    window->priv->menus.view.show_thumbnail_viewer.
            menu_item_thumbnail_viewer_vertical = gtk_radio_menu_item_new_with_mnemonic_from_widget(
                    GTK_RADIO_MENU_ITEM(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_horizontal),
                    _("Show Vertically"));
    window->priv->menus.view.show_thumbnail_viewer.
            menu_item_thumbnail_viewer_hide = gtk_radio_menu_item_new_with_mnemonic_from_widget(
                    GTK_RADIO_MENU_ITEM(window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_horizontal),
                    _("Hide"));

    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.show_thumbnail_viewer.menu),
            window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_horizontal);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.show_thumbnail_viewer.menu),
            window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_vertical);
    gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.view.show_thumbnail_viewer.menu),
            window->priv->menus.view.show_thumbnail_viewer.menu_item_thumbnail_viewer_hide);

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
    g_signal_connect(window->priv->menus.file.menu_item_quit, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_quit), window);
    g_signal_connect(window->priv->menus.file.recently.menu_item_clear, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_clear_recent), window);
    g_signal_connect(window->priv->menus.help.menu_item_about, 
            "activate",
            G_CALLBACK(cb_rstto_main_window_about), window);

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

    /* Misc */
    g_signal_connect(G_OBJECT(window->priv->navigator),
            "iter-changed",
            G_CALLBACK(cb_rstto_main_window_nav_iter_changed), window);
}

static void
rstto_main_window_class_init(RsttoMainWindowClass *window_class)
{
    parent_class = g_type_class_peek_parent(window_class);
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

    rstto_thumbnail_viewer_set_orientation(RSTTO_THUMBNAIL_VIEWER(window->priv->thumbnail_viewer), orientation);

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
        switch(rstto_thumbnail_viewer_get_orientation(RSTTO_THUMBNAIL_VIEWER(window->priv->thumbnail_viewer)))
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
        }
        else
        {
            gtk_widget_show(window->priv->menus.menu);
            gtk_widget_show(window->priv->toolbar.bar);
            gtk_widget_show(window->priv->statusbar);
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

        ThunarVfsPath *path = thunar_vfs_path_new(filename, NULL);
        if (path)
        {
            ThunarVfsInfo *info = thunar_vfs_info_new_for_path(path, NULL);
            gchar *file_media = thunar_vfs_mime_info_get_media(info->mime_info);
            if(!strcmp(file_media, "image"))
            {
                RsttoNavigatorEntry *entry = rstto_navigator_entry_new(info);
                rstto_navigator_add (window->priv->navigator, entry);
                gchar *uri = thunar_vfs_path_dup_uri(info->path);
                gtk_recent_manager_add_item(window->priv->manager, uri);
                g_free(uri);
            }
            g_free(file_media);
            thunar_vfs_path_unref(path);
        }
        else
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
}

static void
cb_rstto_main_window_open_folder(GtkWidget *widget, RsttoMainWindow *window)
{
    GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Open folder"),
                                                    GTK_WINDOW(window),
                                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_OPEN, GTK_RESPONSE_OK,
                                                    NULL);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if(response == GTK_RESPONSE_OK)
    {
        rstto_navigator_clear(window->priv->navigator);
        const gchar *dir_name = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        GDir *dir = g_dir_open(dir_name, 0, NULL);
        if (dir)
        {
            const gchar *filename = g_dir_read_name(dir);
            while (filename)
            {
                gchar *path_name = g_strconcat(dir_name,  "/", filename, NULL);
                ThunarVfsPath *path = thunar_vfs_path_new(path_name, NULL);
                if (path)
                {
                    ThunarVfsInfo *info = thunar_vfs_info_new_for_path(path, NULL);
                    gchar *file_media = thunar_vfs_mime_info_get_media(info->mime_info);
                    if(!strcmp(file_media, "image"))
                    {
                        RsttoNavigatorEntry *entry = rstto_navigator_entry_new(info);
                        rstto_navigator_add (window->priv->navigator, entry);
                    }
                    g_free(file_media);
                    thunar_vfs_path_unref(path);
                }
                g_free(path_name);
                filename = g_dir_read_name(dir);
            }
            rstto_navigator_jump_first(window->priv->navigator);
            gchar *uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(dialog));
            gtk_recent_manager_add_item(window->priv->manager, uri);
            g_free(uri);

        }

    }
    gtk_widget_destroy(dialog);
}

static void
cb_rstto_main_window_open_recent(GtkRecentChooser *chooser, RsttoMainWindow *window)
{
    gchar *uri = gtk_recent_chooser_get_current_uri(chooser);
    ThunarVfsPath *path = thunar_vfs_path_new(uri, NULL);
    if (path)
    {
        ThunarVfsInfo *info = thunar_vfs_info_new_for_path(path, NULL);
        if(info)
        {
            if(strcmp(thunar_vfs_mime_info_get_name(info->mime_info), "inode/directory"))
            {
                RsttoNavigatorEntry *entry = rstto_navigator_entry_new(info);
                rstto_navigator_add (window->priv->navigator, entry);
            }
            else
            {
                rstto_navigator_clear(window->priv->navigator);
                gchar *dir_path = thunar_vfs_path_dup_string(info->path);
                GDir *dir = g_dir_open(dir_path, 0, NULL);
                const gchar *filename = g_dir_read_name(dir);
                while (filename)
                {
                    gchar *path_name = g_strconcat(dir_path,  "/", filename, NULL);
                    ThunarVfsPath *file_path = thunar_vfs_path_new(path_name, NULL);
                    if (file_path)
                    {
                        ThunarVfsInfo *file_info = thunar_vfs_info_new_for_path(file_path, NULL);
                        gchar *file_media = thunar_vfs_mime_info_get_media(file_info->mime_info);
                        if(!strcmp(file_media, "image"))
                        {
                            RsttoNavigatorEntry *entry = rstto_navigator_entry_new(file_info);
                            rstto_navigator_add (window->priv->navigator, entry);
                        }
                        g_free(file_media);
                        thunar_vfs_path_unref(file_path);
                    }
                    g_free(path_name);
                    filename = g_dir_read_name(dir);
                }
                rstto_navigator_jump_first(window->priv->navigator);
                g_free(dir_path);
            }
            gchar *uri = thunar_vfs_path_dup_uri(info->path);
            gtk_recent_manager_add_item(window->priv->manager, uri);
            g_free(uri);
        }
        thunar_vfs_path_unref(path);
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
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_first, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_last, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_previous, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_next, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_play, TRUE);
        gtk_widget_set_sensitive(window->priv->menus.go.menu_item_pause, TRUE);
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

        if(gtk_widget_get_parent(window->priv->menus.edit.open_with.menu_item_empty))
        {
            gtk_container_remove(GTK_CONTAINER(window->priv->menus.edit.open_with.menu), window->priv->menus.edit.open_with.menu_item_empty);
        }

        gtk_container_foreach(GTK_CONTAINER(window->priv->menus.edit.open_with.menu), (GtkCallback)gtk_widget_destroy, NULL);
        if (info)
        {
            window->priv->menu_apps_list = thunar_vfs_mime_database_get_applications(window->priv->mime_dbase, info->mime_info);
            GList *iter = window->priv->menu_apps_list;
            if (iter == NULL)
            {
                gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.open_with.menu), window->priv->menus.edit.open_with.menu_item_empty);
                gtk_widget_show(window->priv->menus.edit.open_with.menu_item_empty);
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
                iter = g_list_next(iter);
            }
        }        
        else
        {
            gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.open_with.menu), window->priv->menus.edit.open_with.menu_item_empty);
            gtk_widget_show(window->priv->menus.edit.open_with.menu_item_empty);
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
        }

        gtk_container_foreach(GTK_CONTAINER(window->priv->menus.edit.open_with.menu), (GtkCallback)gtk_widget_destroy, NULL);
        if(!gtk_widget_get_parent(window->priv->menus.edit.open_with.menu_item_empty))
        {
            gtk_menu_shell_append(GTK_MENU_SHELL(window->priv->menus.edit.open_with.menu), window->priv->menus.edit.open_with.menu_item_empty);
            gtk_widget_show(window->priv->menus.edit.open_with.menu_item_empty);
        }
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
    gdouble scale = rstto_picture_viewer_get_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer));
    rstto_picture_viewer_set_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), scale * window->priv->zoom_factor);
}

static void
cb_rstto_main_window_zoom_out(GtkWidget *widget, RsttoMainWindow *window)
{
    gdouble scale = rstto_picture_viewer_get_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer));
    rstto_picture_viewer_set_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), scale / window->priv->zoom_factor);
}

static void
cb_rstto_main_window_zoom_100(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_set_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer), 1);
}

static void
cb_rstto_main_window_zoom_fit(GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_fit_scale(RSTTO_PICTURE_VIEWER(window->priv->picture_viewer));
}

static void
cb_rstto_main_window_spawn_app(GtkWidget *widget, ThunarVfsMimeApplication *app)
{
    ThunarVfsInfo *info = rstto_navigator_entry_get_info(g_object_get_data(G_OBJECT(app), "entry"));
    GList *list = g_list_prepend(NULL, info->path);
    thunar_vfs_mime_handler_exec(THUNAR_VFS_MIME_HANDLER(app), NULL, list, NULL);
}

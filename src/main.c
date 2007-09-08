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
#include "picture_viewer.h"
#include "thumbnail_viewer.h"

static ThunarVfsMimeDatabase *mime_dbase = NULL;

static void
cb_rstto_zoom_fit(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_zoom_100(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_zoom_in(GtkToolItem *item, RsttoPictureViewer *viewer);
static void
cb_rstto_zoom_out(GtkToolItem *item, RsttoPictureViewer *viewer);

static void
cb_rstto_fullscreen(GtkWidget *, GdkEventWindowState *event, RsttoPictureViewer *viewer);
static gboolean
cb_rstto_main_window_configure_event (GtkWidget *widget, GdkEventConfigure *event);
static void
cb_rstto_toggle_play(GtkImageMenuItem *item, RsttoNavigator *navigator);

static void
cb_rstto_previous(GtkToolItem *item, RsttoNavigator *);
static void
cb_rstto_forward(GtkToolItem *item, RsttoNavigator *);
static void
cb_rstto_first(GtkToolItem *item, RsttoNavigator *);
static void
cb_rstto_last(GtkToolItem *item, RsttoNavigator *);

static void
cb_rstto_open(GtkToolItem *item, RsttoNavigator *);
static void
cb_rstto_open_dir(GtkToolItem *item, RsttoNavigator *);
static void
cb_rstto_open_recent(GtkRecentChooser *chooser, RsttoNavigator *);
static void
cb_rstto_clear_recent(GtkWidget *widget, GtkRecentManager *manager);

static void
cb_rstto_help_about(GtkToolItem *item, GtkWindow *);
static void
cb_rstto_toggle_fullscreen(GtkToolItem *item, GtkWindow *window);

static void
cb_rstto_show_tv_v(GtkWidget *widget, RsttoThumbnailViewer *viewer);
static void
cb_rstto_show_tv_h(GtkWidget *widget, RsttoThumbnailViewer *viewer);
static void
cb_rstto_hide_tv(GtkWidget *widget, RsttoThumbnailViewer *viewer);

static void
cb_rstto_toggle_toolbar(GtkWidget *widget, RsttoThumbnailViewer *viewer);

static void
cb_rstto_rotate_cw(GtkWidget *widget, RsttoNavigator *navigator);
static void
cb_rstto_rotate_ccw(GtkWidget *widget, RsttoNavigator *navigator);
static void
cb_rstto_flip_h(GtkWidget *widget, RsttoNavigator *navigator);
static void
cb_rstto_flip_v(GtkWidget *widget, RsttoNavigator *navigator);

static gboolean
cb_rstto_key_press_event(GtkWidget *widget, GdkEventKey *event, RsttoNavigator *navigator);
static void
cb_rstto_nav_file_changed(RsttoNavigator *navigator, gint nr, RsttoNavigatorEntry *entry, GtkWindow *window);

static gboolean window_fullscreen = FALSE;
static GtkWidget *menu_bar;
static GtkWidget *app_tool_bar;
static GtkWidget *status_bar;
static gboolean playing = FALSE;
static GtkWidget *menu_item_play;
static GtkWidget *menu_item_pause;
static GtkWidget *menu_item_htv;
static GtkWidget *menu_item_vtv;
static GtkWidget *menu_item_ntv;

static GtkWidget *main_hbox;
static GtkWidget *main_vbox1;
static GtkWidget *thumbnail_viewer;
static GtkRecentManager *recent_manager;
static XfceRc *xfce_rc;
static const gchar *thumbnail_viewer_orientation;
static gint window_save_geometry_timer_id = 0;

int main(int argc, char **argv)
{
    ThunarVfsPath *path = NULL;
    GtkWidget *viewer = NULL;


    #ifdef ENABLE_NLS
    bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
    bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
    textdomain (GETTEXT_PACKAGE);
    #endif

    gtk_init(&argc, &argv);

    thunar_vfs_init();

    mime_dbase = thunar_vfs_mime_database_get_default();

    gtk_window_set_default_icon_name("ristretto");
    recent_manager = gtk_recent_manager_get_default();
    xfce_rc = xfce_rc_config_open(XFCE_RESOURCE_CONFIG, "ristretto/ristrettorc", FALSE);
    
    thumbnail_viewer_orientation = xfce_rc_read_entry(xfce_rc, "ThumbnailViewerOrientation", "horizontal");
    gboolean toolbar_open_dir = xfce_rc_read_bool_entry(xfce_rc, "ToolbarOpenDir", FALSE);
    gboolean show_toolbar = xfce_rc_read_bool_entry(xfce_rc, "ShowToolBar", TRUE);
    gint window_width = xfce_rc_read_int_entry(xfce_rc, "LastWindowWidth", 400);
    gint window_height = xfce_rc_read_int_entry(xfce_rc, "LastWindowHeight", 300);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    GtkAccelGroup *accel_group = gtk_accel_group_new();

    gtk_window_set_title(GTK_WINDOW(window), PACKAGE_STRING);
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);


    g_signal_connect(G_OBJECT(window), "destroy", G_CALLBACK(gtk_main_quit), NULL);

    RsttoNavigator *navigator = rstto_navigator_new();
    thumbnail_viewer = rstto_thumbnail_viewer_new(navigator);
    viewer = rstto_picture_viewer_new(navigator);

    g_signal_connect(window , "key-press-event", G_CALLBACK(cb_rstto_key_press_event) , navigator);
    g_signal_connect(G_OBJECT(navigator), "iter-changed", G_CALLBACK(cb_rstto_nav_file_changed), window);


    if(argc == 2)
        path = thunar_vfs_path_new(argv[1], NULL);
    if (path)
    {
        ThunarVfsInfo *info = thunar_vfs_info_new_for_path(path, NULL);
        if(info)
        {
            if(strcmp(thunar_vfs_mime_info_get_name(info->mime_info), "inode/directory"))
            {
                RsttoNavigatorEntry *entry = rstto_navigator_entry_new(info);
                rstto_navigator_add (navigator, entry);
            }
            else
            {
                GDir *dir = g_dir_open(argv[1], 0, NULL);
                const gchar *filename = g_dir_read_name(dir);
                while (filename)
                {
                    gchar *path_name = g_strconcat(argv[1],  "/", filename, NULL);
                    ThunarVfsPath *file_path = thunar_vfs_path_new(path_name, NULL);
                    if (file_path)
                    {
                        ThunarVfsInfo *file_info = thunar_vfs_info_new_for_path(file_path, NULL);
                        gchar *file_media = thunar_vfs_mime_info_get_media(file_info->mime_info);
                        if(!strcmp(file_media, "image"))
                        {
                            RsttoNavigatorEntry *entry = rstto_navigator_entry_new(file_info);
                            rstto_navigator_add (navigator, entry);
                        }
                        g_free(file_media);
                        thunar_vfs_path_unref(file_path);
                    }
                    g_free(path_name);
                    filename = g_dir_read_name(dir);
                }
                rstto_navigator_jump_first(navigator);
            }
            gchar *uri = thunar_vfs_path_dup_uri(info->path);
            gtk_recent_manager_add_item(recent_manager, uri);
            g_free(uri);
        }
        thunar_vfs_path_unref(path);
    }


    GtkWidget *s_window = gtk_scrolled_window_new(NULL,NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(s_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    GtkWidget *main_vbox = gtk_vbox_new(0, FALSE);
    main_hbox = gtk_hbox_new(0, FALSE);
    main_vbox1 = gtk_vbox_new(0, FALSE);
    menu_bar = gtk_menu_bar_new();
    app_tool_bar = gtk_toolbar_new();
    status_bar = gtk_statusbar_new();

    GtkWidget *menu_item_file = gtk_menu_item_new_with_mnemonic(_("_File"));
    GtkWidget *menu_item_open = gtk_image_menu_item_new_from_stock(GTK_STOCK_OPEN, accel_group);
    GtkWidget *menu_item_open_dir = gtk_menu_item_new_with_mnemonic(_("O_pen Folder"));
    GtkWidget *menu_item_recently = gtk_menu_item_new_with_mnemonic(_("_Recently used"));
    GtkWidget *menu_item_separator = gtk_separator_menu_item_new();
    /* GtkWidget *menu_item_close = gtk_image_menu_item_new_from_stock(GTK_STOCK_CLOSE, accel_group); */
    GtkWidget *menu_item_quit = gtk_image_menu_item_new_from_stock(GTK_STOCK_QUIT, accel_group);

    GtkWidget *menu_file = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item_file), menu_file);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_item_open);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_item_open_dir);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_item_recently);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_item_separator);
    /* gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_item_close);*/
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_file), menu_item_quit);
    
    GtkWidget *recent_chooser_menu = gtk_recent_chooser_menu_new_for_manager(GTK_RECENT_MANAGER(recent_manager));
    GtkRecentFilter *filter = gtk_recent_filter_new();
    gtk_recent_filter_add_application(filter, "ristretto");
    gtk_recent_chooser_add_filter(GTK_RECENT_CHOOSER(recent_chooser_menu), filter);
    
    GtkWidget *menu_item_clear_recent = gtk_image_menu_item_new_with_mnemonic(_("Clear"));
    GtkWidget *img_clear_recent = gtk_image_new_from_stock(GTK_STOCK_CLEAR, GTK_ICON_SIZE_MENU);
    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(menu_item_clear_recent), img_clear_recent);
    menu_item_separator = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(recent_chooser_menu), menu_item_separator);
    gtk_menu_shell_append(GTK_MENU_SHELL(recent_chooser_menu), menu_item_clear_recent);
    
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item_recently), recent_chooser_menu);

    GtkWidget *menu_item_edit = gtk_menu_item_new_with_mnemonic(_("_Edit"));
    GtkWidget *menu_item_rotate_left = gtk_menu_item_new_with_mnemonic(_("Rotate _Left"));
    GtkWidget *menu_item_rotate_right = gtk_menu_item_new_with_mnemonic(_("Rotate _Right"));
    GtkWidget *menu_item_flip_v = gtk_menu_item_new_with_mnemonic(_("Flip _Vertically"));
    GtkWidget *menu_item_flip_h = gtk_menu_item_new_with_mnemonic(_("Flip _Horizontally"));

    GtkWidget *menu_edit = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item_edit), menu_edit);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_edit), menu_item_rotate_left);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_edit), menu_item_rotate_right);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_edit), menu_item_flip_v);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_edit), menu_item_flip_h);

    GtkWidget *menu_item_view = gtk_menu_item_new_with_mnemonic(_("_View"));
    GtkWidget *menu_item_toggle_toolbar = gtk_check_menu_item_new_with_mnemonic(_("Show Toolbar"));
    GtkWidget *menu_item_tv = gtk_menu_item_new_with_mnemonic(_("Thumbnail Viewer"));
    GtkWidget *menu_item_view_fs = gtk_image_menu_item_new_from_stock(GTK_STOCK_FULLSCREEN, NULL);
    gtk_widget_add_accelerator(menu_item_view_fs, "activate", accel_group, GDK_F11, 0,GTK_ACCEL_VISIBLE);

    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item_toggle_toolbar), show_toolbar);

    GtkWidget *menu_view = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item_view), menu_view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_view), menu_item_toggle_toolbar);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_view), menu_item_tv);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_view), menu_item_view_fs);

    GtkWidget *menu_tv = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item_tv), menu_tv);
    menu_item_htv = gtk_radio_menu_item_new_with_mnemonic(NULL, _("Show Horizontally"));
    menu_item_vtv = gtk_radio_menu_item_new_with_mnemonic_from_widget(GTK_RADIO_MENU_ITEM(menu_item_htv), _("Show Vertically"));
    menu_item_ntv = gtk_radio_menu_item_new_with_mnemonic_from_widget(GTK_RADIO_MENU_ITEM(menu_item_htv), _("Hide"));

    gtk_menu_shell_append(GTK_MENU_SHELL(menu_tv), menu_item_htv);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_tv), menu_item_vtv);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_tv), menu_item_ntv);

    GtkWidget *menu_item_go = gtk_menu_item_new_with_mnemonic(_("_Go"));

    GtkWidget *menu_item_first = gtk_image_menu_item_new_from_stock(GTK_STOCK_GOTO_FIRST, NULL);
    GtkWidget *menu_item_last = gtk_image_menu_item_new_from_stock(GTK_STOCK_GOTO_LAST, NULL);
    GtkWidget *menu_item_forward = gtk_image_menu_item_new_from_stock(GTK_STOCK_GO_FORWARD, NULL);
    GtkWidget *menu_item_back = gtk_image_menu_item_new_from_stock(GTK_STOCK_GO_BACK, NULL);
    gtk_widget_add_accelerator(menu_item_first, "activate", accel_group, GDK_Home, 0,GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(menu_item_last, "activate", accel_group, GDK_End, 0,GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(menu_item_forward, "activate", accel_group, GDK_Page_Down, 0,GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(menu_item_back, "activate", accel_group, GDK_Page_Up, 0,GTK_ACCEL_VISIBLE);

    menu_item_play = gtk_image_menu_item_new_from_stock(GTK_STOCK_MEDIA_PLAY, accel_group);
    menu_item_pause = gtk_image_menu_item_new_from_stock(GTK_STOCK_MEDIA_PAUSE, accel_group);
    gtk_widget_add_accelerator(menu_item_play, "activate", accel_group, GDK_F5, 0,GTK_ACCEL_VISIBLE);
    gtk_widget_add_accelerator(menu_item_pause, "activate", accel_group, GDK_F5, 0,GTK_ACCEL_VISIBLE);

    menu_item_separator = gtk_separator_menu_item_new();

    GtkWidget *menu_go = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item_go), menu_go);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_go), menu_item_first);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_go), menu_item_last);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_go), menu_item_forward);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_go), menu_item_back);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_go), menu_item_separator);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_go), menu_item_play);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_go), menu_item_pause);

    GtkWidget *menu_item_help = gtk_menu_item_new_with_mnemonic(_("_Help"));
    GtkWidget *menu_item_help_about = gtk_image_menu_item_new_from_stock(GTK_STOCK_ABOUT, accel_group);

    GtkWidget *menu_help = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item_help), menu_help);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_help), menu_item_help_about);


    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), menu_item_file);
    /*gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), menu_item_edit);*/
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), menu_item_view);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), menu_item_go);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu_bar), menu_item_help);

    GtkToolItem *zoom_fit= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_FIT);
    GtkToolItem *zoom_100= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_100);
    GtkToolItem *zoom_out= gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_OUT);
    GtkToolItem *zoom_in = gtk_tool_button_new_from_stock(GTK_STOCK_ZOOM_IN);
    GtkToolItem *forward = gtk_tool_button_new_from_stock(GTK_STOCK_GO_FORWARD);
    GtkToolItem *previous = gtk_tool_button_new_from_stock(GTK_STOCK_GO_BACK);
    GtkToolItem *open = gtk_tool_button_new_from_stock(GTK_STOCK_OPEN);
    GtkToolItem *spacer = gtk_tool_item_new();
    GtkToolItem *separator = gtk_separator_tool_item_new();

    gtk_tool_item_set_is_important(previous, TRUE);
    gtk_tool_item_set_is_important(forward, TRUE);

    gtk_tool_item_set_expand(spacer, TRUE);
    gtk_tool_item_set_homogeneous(spacer, FALSE);

    gtk_window_set_default_size(GTK_WINDOW(window), window_width, window_height);

    gtk_container_add(GTK_CONTAINER(s_window), viewer);
    gtk_box_pack_start(GTK_BOX(main_hbox), main_vbox1, TRUE, TRUE, 0);

    gtk_box_pack_start(GTK_BOX(main_vbox1), s_window, TRUE, TRUE, 0);
    if(!strcmp(thumbnail_viewer_orientation, "horizontal"))
    {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(menu_item_htv), TRUE);
        gtk_box_pack_start(GTK_BOX(main_vbox1), thumbnail_viewer, FALSE, TRUE, 0);
        rstto_thumbnail_viewer_set_orientation(RSTTO_THUMBNAIL_VIEWER(thumbnail_viewer), GTK_ORIENTATION_HORIZONTAL);
    }
    else
    {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(menu_item_vtv), TRUE);
        gtk_box_pack_start(GTK_BOX(main_hbox), thumbnail_viewer, FALSE, TRUE, 0);
        rstto_thumbnail_viewer_set_orientation(RSTTO_THUMBNAIL_VIEWER(thumbnail_viewer), GTK_ORIENTATION_VERTICAL);
    }


    gtk_box_pack_start(GTK_BOX(main_vbox), menu_bar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), app_tool_bar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), main_hbox, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), status_bar, FALSE, TRUE, 0);

    //rstto_picture_viewer_fit_scale(RSTTO_PICTURE_VIEWER(viewer));

    gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), zoom_fit, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), zoom_100, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), zoom_out, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), zoom_in, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), spacer, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), forward, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), previous, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), separator, 0);
    gtk_toolbar_insert(GTK_TOOLBAR(app_tool_bar), open, 0);

    g_signal_connect(G_OBJECT(zoom_fit), "clicked", G_CALLBACK(cb_rstto_zoom_fit), viewer);
    g_signal_connect(G_OBJECT(zoom_100), "clicked", G_CALLBACK(cb_rstto_zoom_100), viewer);
    g_signal_connect(G_OBJECT(zoom_in), "clicked", G_CALLBACK(cb_rstto_zoom_in), viewer);
    g_signal_connect(G_OBJECT(zoom_out), "clicked", G_CALLBACK(cb_rstto_zoom_out), viewer);
    g_signal_connect(G_OBJECT(forward), "clicked", G_CALLBACK(cb_rstto_forward), navigator);
    g_signal_connect(G_OBJECT(previous), "clicked", G_CALLBACK(cb_rstto_previous), navigator);
    if(toolbar_open_dir == TRUE)
        g_signal_connect(G_OBJECT(open), "clicked", G_CALLBACK(cb_rstto_open_dir), navigator);
    else
        g_signal_connect(G_OBJECT(open), "clicked", G_CALLBACK(cb_rstto_open), navigator);

    g_signal_connect(G_OBJECT(menu_item_clear_recent), "activate", G_CALLBACK(cb_rstto_clear_recent), recent_manager);

    g_signal_connect(G_OBJECT(menu_item_quit), "activate", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(G_OBJECT(menu_item_open), "activate", G_CALLBACK(cb_rstto_open), navigator);
    g_signal_connect(G_OBJECT(menu_item_open_dir), "activate", G_CALLBACK(cb_rstto_open_dir), navigator);
    g_signal_connect(G_OBJECT(menu_item_help_about), "activate", G_CALLBACK(cb_rstto_help_about), window);

    g_signal_connect(G_OBJECT(recent_chooser_menu), "item-activated", G_CALLBACK(cb_rstto_open_recent), navigator);

    g_signal_connect(G_OBJECT(menu_item_forward), "activate", G_CALLBACK(cb_rstto_forward), navigator);
    g_signal_connect(G_OBJECT(menu_item_back), "activate", G_CALLBACK(cb_rstto_previous), navigator);
    g_signal_connect(G_OBJECT(menu_item_first), "activate", G_CALLBACK(cb_rstto_first), navigator);
    g_signal_connect(G_OBJECT(menu_item_last), "activate", G_CALLBACK(cb_rstto_last), navigator);

    g_signal_connect(G_OBJECT(menu_item_rotate_left), "activate", G_CALLBACK(cb_rstto_rotate_ccw), navigator);
    g_signal_connect(G_OBJECT(menu_item_rotate_right), "activate", G_CALLBACK(cb_rstto_rotate_cw), navigator);
    g_signal_connect(G_OBJECT(menu_item_flip_v), "activate", G_CALLBACK(cb_rstto_flip_v), navigator);
    g_signal_connect(G_OBJECT(menu_item_flip_h), "activate", G_CALLBACK(cb_rstto_flip_h), navigator);

    g_signal_connect(G_OBJECT(menu_item_toggle_toolbar), "activate", G_CALLBACK(cb_rstto_toggle_toolbar), thumbnail_viewer);

    g_signal_connect(G_OBJECT(menu_item_vtv), "activate", G_CALLBACK(cb_rstto_show_tv_v), thumbnail_viewer);
    g_signal_connect(G_OBJECT(menu_item_htv), "activate", G_CALLBACK(cb_rstto_show_tv_h), thumbnail_viewer);
    g_signal_connect(G_OBJECT(menu_item_ntv), "activate", G_CALLBACK(cb_rstto_hide_tv), thumbnail_viewer);

    g_signal_connect(G_OBJECT(menu_item_play), "activate", G_CALLBACK(cb_rstto_toggle_play), navigator);
    g_signal_connect(G_OBJECT(menu_item_pause), "activate", G_CALLBACK(cb_rstto_toggle_play), navigator);
    g_signal_connect(G_OBJECT(menu_item_view_fs), "activate", G_CALLBACK(cb_rstto_toggle_fullscreen), window);

    g_signal_connect(G_OBJECT(window), "window-state-event", G_CALLBACK(cb_rstto_fullscreen), viewer);
    g_signal_connect(G_OBJECT(window), "configure-event", G_CALLBACK(cb_rstto_main_window_configure_event), NULL);

    gtk_container_add(GTK_CONTAINER(window), main_vbox);

    gtk_widget_show_all(window);
    gtk_widget_hide(menu_item_pause);
    gtk_widget_show(viewer);

    if(!strcmp(thumbnail_viewer_orientation, "hide"))
    {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(menu_item_ntv), TRUE);
        gtk_widget_hide(GTK_WIDGET(thumbnail_viewer));   
    }
    if (show_toolbar == FALSE)
    {
        gtk_widget_hide(GTK_WIDGET(app_tool_bar));   
    }


    gtk_main();
    xfce_rc_flush(xfce_rc);
    xfce_rc_close(xfce_rc);
    return 0;
}

static void
cb_rstto_zoom_fit(GtkToolItem *item, RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_fit_scale(viewer);
}

static void
cb_rstto_zoom_100(GtkToolItem *item, RsttoPictureViewer *viewer)
{
    rstto_picture_viewer_set_scale(viewer, 1);
}

static void
cb_rstto_zoom_in(GtkToolItem *item, RsttoPictureViewer *viewer)
{
    gdouble scale = rstto_picture_viewer_get_scale(viewer);
    rstto_picture_viewer_set_scale(viewer, scale*1.2);
}

static void
cb_rstto_zoom_out(GtkToolItem *item, RsttoPictureViewer *viewer)
{
    gdouble scale = rstto_picture_viewer_get_scale(viewer);
    rstto_picture_viewer_set_scale(viewer, scale/1.2);
}


static void
cb_rstto_help_about(GtkToolItem *item, GtkWindow *window)
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
cb_rstto_first(GtkToolItem *item, RsttoNavigator *navigator)
{
    rstto_navigator_jump_first(navigator);
}

static void
cb_rstto_last(GtkToolItem *item, RsttoNavigator *navigator)
{
    rstto_navigator_jump_last(navigator);
}

static void
cb_rstto_forward(GtkToolItem *item, RsttoNavigator *navigator)
{
    rstto_navigator_jump_forward(navigator);
}

static void
cb_rstto_previous(GtkToolItem *item, RsttoNavigator *navigator)
{
    rstto_navigator_jump_back(navigator);
}

static void
cb_rstto_nav_file_changed(RsttoNavigator *navigator, gint nr, RsttoNavigatorEntry *entry, GtkWindow *window)
{
    ThunarVfsInfo *info = rstto_navigator_entry_get_info(entry);
    const gchar *filename = info->display_name;
    gchar *title;
    if(filename)
    {
        title = g_strconcat(PACKAGE_NAME, " - ", filename, NULL);
        gtk_window_set_title(window, title);
        g_free(title);
    }
    else
        gtk_window_set_title(window, PACKAGE_STRING);
}

static void
cb_rstto_fullscreen(GtkWidget *widget, GdkEventWindowState *event, RsttoPictureViewer *viewer)
{
    if(event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
    {
        if(event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN)
        {
            window_fullscreen = TRUE;
            gtk_widget_hide(menu_bar);
            gtk_widget_hide(app_tool_bar);
            gtk_widget_hide(status_bar);
        }
        else
        {
            window_fullscreen = FALSE;
            gtk_widget_show(menu_bar);
            gtk_widget_show(app_tool_bar);
            gtk_widget_show(status_bar);
        }
    }
}

static void
cb_rstto_toggle_play(GtkImageMenuItem *item, RsttoNavigator *navigator)
{
    if(playing == TRUE)
    {
        gtk_widget_show(menu_item_play);
        gtk_widget_hide(menu_item_pause);
        playing = FALSE;
    }
    else
    {
        gtk_widget_hide(menu_item_play);
        gtk_widget_show(menu_item_pause);
        playing = TRUE;
    }
    rstto_navigator_set_running(navigator, playing);
}

static void
cb_rstto_toggle_fullscreen(GtkToolItem *item, GtkWindow *window)
{
    if(window_fullscreen)
        gtk_window_unfullscreen(window);
    else
        gtk_window_fullscreen(window);
}

static void
cb_rstto_show_tv_v(GtkWidget *widget, RsttoThumbnailViewer *viewer)
{
    GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(viewer));
    gtk_widget_ref(GTK_WIDGET(viewer));
    gtk_container_remove(GTK_CONTAINER(parent), GTK_WIDGET(viewer));
    rstto_thumbnail_viewer_set_orientation(viewer, GTK_ORIENTATION_VERTICAL);
    gtk_box_pack_start(GTK_BOX(main_hbox), thumbnail_viewer, FALSE, TRUE, 0);
    gtk_widget_show(GTK_WIDGET(viewer));
    xfce_rc_write_entry(xfce_rc, "ThumbnailViewerOrientation", "vertical");
}

static void
cb_rstto_show_tv_h(GtkWidget *widget, RsttoThumbnailViewer *viewer)
{
    GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(viewer));
    gtk_widget_ref(GTK_WIDGET(viewer));
    gtk_container_remove(GTK_CONTAINER(parent), GTK_WIDGET(viewer));
    rstto_thumbnail_viewer_set_orientation(viewer, GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_vbox1), thumbnail_viewer, FALSE, TRUE, 0);
    gtk_widget_show(GTK_WIDGET(viewer));
    xfce_rc_write_entry(xfce_rc, "ThumbnailViewerOrientation", "horizontal");
}

static void
cb_rstto_hide_tv(GtkWidget *widget, RsttoThumbnailViewer *viewer)
{
    gtk_widget_hide(GTK_WIDGET(viewer));
    xfce_rc_write_entry(xfce_rc, "ThumbnailViewerOrientation", "hide");
}

static void
cb_rstto_flip_h(GtkWidget *widget, RsttoNavigator *navigator)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(navigator);
    rstto_navigator_flip_entry(navigator, entry, TRUE);
}

static void
cb_rstto_flip_v(GtkWidget *widget, RsttoNavigator *navigator)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(navigator);
    rstto_navigator_flip_entry(navigator, entry, FALSE);
}

static void
cb_rstto_rotate_cw(GtkWidget *widget, RsttoNavigator *navigator)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(navigator);
    GdkPixbufRotation rotation = rstto_navigator_entry_get_rotation(entry);
    switch (rotation)
    {
        case GDK_PIXBUF_ROTATE_NONE:
            rotation = GDK_PIXBUF_ROTATE_CLOCKWISE;
            break;
        case GDK_PIXBUF_ROTATE_CLOCKWISE:
            rotation = GDK_PIXBUF_ROTATE_UPSIDEDOWN;
            break;
        case GDK_PIXBUF_ROTATE_UPSIDEDOWN:
            rotation = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
            break;
        case GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE:
            rotation = GDK_PIXBUF_ROTATE_NONE;
            break;
    }

    //rstto_navigator_set_entry_rotation(navigator, entry, rotation);
}

static void
cb_rstto_rotate_ccw(GtkWidget *widget, RsttoNavigator *navigator)
{
    RsttoNavigatorEntry *entry = rstto_navigator_get_file(navigator);
    GdkPixbufRotation rotation = rstto_navigator_entry_get_rotation(entry);
    switch (rotation)
    {
        case GDK_PIXBUF_ROTATE_NONE:
            rotation = GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE;
            break;
        case GDK_PIXBUF_ROTATE_CLOCKWISE:
            rotation = GDK_PIXBUF_ROTATE_NONE;
            break;
        case GDK_PIXBUF_ROTATE_UPSIDEDOWN:
            rotation = GDK_PIXBUF_ROTATE_CLOCKWISE;
            break;
        case GDK_PIXBUF_ROTATE_COUNTERCLOCKWISE:
            rotation = GDK_PIXBUF_ROTATE_UPSIDEDOWN;
            break;
    }

    //rstto_navigator_set_entry_rotation(navigator, entry, rotation);
}

static gboolean
cb_rstto_key_press_event(GtkWidget *widget, GdkEventKey *event, RsttoNavigator *navigator)
{
    GtkWindow *window = GTK_WINDOW(widget);
    if(gtk_window_activate_key(window, event) == FALSE)
    {
        switch(event->keyval)
        {
            case GDK_F11:
                if(window_fullscreen)
                    gtk_window_unfullscreen(window);
                else
                    gtk_window_fullscreen(window);
                break;
            case GDK_Home:
                rstto_navigator_jump_first(navigator);
                break;
            case GDK_End:
                rstto_navigator_jump_last(navigator);
                break;
            case GDK_Page_Down:
            case GDK_space:
                rstto_navigator_jump_forward(navigator);
                break;
            case GDK_Page_Up:
            case GDK_BackSpace:
                rstto_navigator_jump_back(navigator);
                break;
            case GDK_t:
                if (event->state & GDK_CONTROL_MASK)
                {
                    if(strcmp(xfce_rc_read_entry(xfce_rc, "ThumbnailViewerOrientation", "hide"), "hide"))
                    {
                        gtk_widget_hide(GTK_WIDGET(thumbnail_viewer));
                        xfce_rc_write_entry(xfce_rc, "ThumbnailViewerOrientation", "hide");
                        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(menu_item_ntv), TRUE);
                    }
                    else
                    {
                        gtk_widget_show(GTK_WIDGET(thumbnail_viewer));
                        switch (rstto_thumbnail_viewer_get_orientation(RSTTO_THUMBNAIL_VIEWER(thumbnail_viewer)))
                        {
                            case GTK_ORIENTATION_HORIZONTAL:
                                xfce_rc_write_entry(xfce_rc, "ThumbnailViewerOrientation", "horizontal");
                                gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(menu_item_htv), TRUE);
                                break;
                            case GTK_ORIENTATION_VERTICAL:
                                xfce_rc_write_entry(xfce_rc, "ThumbnailViewerOrientation", "vertical");
                                gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(menu_item_vtv), TRUE);
                                break;
                        }

                    }
                }
                break;
        }
    }
    return TRUE;
}

static void
cb_rstto_open(GtkToolItem *item, RsttoNavigator *navigator)
{
    GtkWidget *window = gtk_widget_get_toplevel(GTK_WIDGET(item));

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
                rstto_navigator_add (navigator, entry);

                gchar *uri = thunar_vfs_path_dup_uri(info->path);
                gtk_recent_manager_add_item(recent_manager, uri);
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
cb_rstto_open_dir(GtkToolItem *item, RsttoNavigator *navigator)
{
    GtkWidget *window = gtk_widget_get_toplevel(GTK_WIDGET(item));

    GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Open folder"),
                                                    GTK_WINDOW(window),
                                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_OPEN, GTK_RESPONSE_OK,
                                                    NULL);

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if(response == GTK_RESPONSE_OK)
    {
        rstto_navigator_clear(navigator);
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
                        rstto_navigator_add (navigator, entry);
                    }
                    g_free(file_media);
                    thunar_vfs_path_unref(path);
                }
                g_free(path_name);
                filename = g_dir_read_name(dir);
            }
            rstto_navigator_jump_first(navigator);
            gchar *uri = gtk_file_chooser_get_uri(GTK_FILE_CHOOSER(dialog));
            gtk_recent_manager_add_item(recent_manager, uri);
            g_free(uri);

        }

    }
    gtk_widget_destroy(dialog);
}

static void
cb_rstto_open_recent(GtkRecentChooser *chooser, RsttoNavigator *navigator)
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
                rstto_navigator_add (navigator, entry);
            }
            else
            {
                rstto_navigator_clear(navigator);
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
                            rstto_navigator_add (navigator, entry);
                        }
                        g_free(file_media);
                        thunar_vfs_path_unref(file_path);
                    }
                    g_free(path_name);
                    filename = g_dir_read_name(dir);
                }
                rstto_navigator_jump_first(navigator);
                g_free(dir_path);
            }
            gchar *uri = thunar_vfs_path_dup_uri(info->path);
            gtk_recent_manager_add_item(recent_manager, uri);
            g_free(uri);
        }
        thunar_vfs_path_unref(path);
    }
}

static void
cb_rstto_toggle_toolbar(GtkWidget *widget, RsttoThumbnailViewer *viewer)
{
    gboolean active = gtk_check_menu_item_get_active (GTK_CHECK_MENU_ITEM(widget));
    if (active == TRUE)
    {
        gtk_widget_show(app_tool_bar);
        xfce_rc_write_bool_entry(xfce_rc, "ShowToolBar", TRUE);
    }
    else
    {
        gtk_widget_hide(app_tool_bar);
        xfce_rc_write_bool_entry(xfce_rc, "ShowToolBar", FALSE);
    }
}

static gboolean
rstto_window_save_geometry_timer (gpointer user_data)
{
    GtkWindow *window = GTK_WINDOW(user_data);
    gint width = 0;
    gint height = 0;
    /* check if the window is still visible */
    if (GTK_WIDGET_VISIBLE (window))
    {
        /* determine the current state of the window */
        gint state = gdk_window_get_state (GTK_WIDGET (window)->window);

        /* don't save geometry for maximized or fullscreen windows */
        if ((state & (GDK_WINDOW_STATE_MAXIMIZED | GDK_WINDOW_STATE_FULLSCREEN)) == 0)
        {
            /* determine the current width/height of the window... */
            gtk_window_get_size (GTK_WINDOW (window), &width, &height);

            /* ...and remember them as default for new windows */
            xfce_rc_write_int_entry (xfce_rc, "LastWindowWidth", width);
            xfce_rc_write_int_entry (xfce_rc, "LastWindowHeight", height);
        }
    }
    return FALSE;
}

static void
rstto_window_save_geometry_timer_destroy(gpointer user_data)
{
    window_save_geometry_timer_id = 0;
}

static gboolean
cb_rstto_main_window_configure_event (GtkWidget *widget, GdkEventConfigure *event)
{
    /* shamelessly copied from thunar, written by benny */
    /* check if we have a new dimension here */
    if (widget->allocation.width != event->width || widget->allocation.height != event->height)
    {
        /* drop any previous timer source */
        if (window_save_geometry_timer_id > 0)
            g_source_remove (window_save_geometry_timer_id);

        /* check if we should schedule another save timer */
        if (GTK_WIDGET_VISIBLE (widget))
        {
            /* save the geometry one second after the last configure event */
            window_save_geometry_timer_id = g_timeout_add_full (G_PRIORITY_LOW, 1000, rstto_window_save_geometry_timer,
                widget, rstto_window_save_geometry_timer_destroy);
        }
    }

    /* let Gtk+ handle the configure event */
    return FALSE;
}

static void
cb_rstto_clear_recent(GtkWidget *widget, GtkRecentManager *manager)
{
    GList *item_list = gtk_recent_manager_get_items(manager);
    GList *item_iter = item_list;
    while(item_iter != NULL)
    {
        gchar **apps = gtk_recent_info_get_applications(item_iter->data, NULL);
        gint i = 0;
        for(; apps[i] != NULL; ++i)
        {
            if(!strcmp(apps[i], "ristretto"))
            {
                gtk_recent_manager_remove_item(manager, gtk_recent_info_get_uri(item_iter->data), NULL);
                break;
            }
        }
        g_strfreev(apps);
        item_iter = g_list_next(item_iter);
    }
    g_list_foreach(item_list, (GFunc)gtk_recent_info_unref, NULL);
    g_list_free(item_list);
}

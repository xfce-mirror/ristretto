/*
 *  Copyright (C) Stephan Arts 2006-2009 <stephan@xfce.org>
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
#include <X11/Xlib.h>
#include <string.h>

#include <gio/gio.h>

#include <libxfcegui4/libxfcegui4.h>
#include <libexif/exif-data.h>

#include <cairo/cairo.h>

#include "image.h"

#include "settings.h"
#include "image_list.h"
#include "image_cache.h"
#include "picture_viewer.h"
#include "main_window.h"
#include "main_window_ui.h"
#include "thumbnail_bar.h"

#include "preferences_dialog.h"

#define XFDESKTOP_SELECTION_FMT "XFDESKTOP_SELECTION_%d"

#ifndef RISTRETTO_APP_TITLE
#define RISTRETTO_APP_TITLE "Image viewer"
#endif


#ifndef ZOOM_FACTOR
#define ZOOM_FACTOR 1.2
#endif

struct _RsttoMainWindowPriv
{
    struct {
        RsttoImageList *image_list;
        gboolean        toolbar_visible;
    } props;

    guint show_fs_toolbar_timeout_id;
    gint window_save_geometry_timer_id;
    
    gboolean fs_toolbar_sticky;

    RsttoImageListIter *iter;

    GtkActionGroup   *action_group;
    GtkUIManager     *ui_manager;
    GtkRecentManager *recent_manager;
    RsttoSettings    *settings_manager;

    GtkWidget *menubar;
    GtkWidget *toolbar;
    GtkWidget *image_list_toolbar;
    GtkWidget *image_list_toolbar_menu;
    GtkWidget *picture_viewer;
    GtkWidget *p_viewer_s_window;
    GtkWidget *hpaned;
    GtkWidget *thumbnail_bar;
    GtkWidget *statusbar;

    GtkWidget *message_bar;
    GtkWidget *message_bar_label;
    GtkWidget *message_bar_button_cancel;
    GtkWidget *message_bar_button_open;
    GFile *message_bar_file;

    guint      t_open_merge_id;
    guint      t_open_folder_merge_id;
    guint      recent_merge_id;
    guint      play_merge_id;
    guint      pause_merge_id;
    guint      toolbar_play_merge_id;
    guint      toolbar_pause_merge_id;
    guint      toolbar_fullscreen_merge_id;
    guint      toolbar_unfullscreen_merge_id;

    GtkAction *play_action;
    GtkAction *pause_action;
    GtkAction *recent_action;

    gboolean playing;
    gint play_timeout_id;
};

enum
{
    PROP_0,
    PROP_IMAGE_LIST,
};

static void
rstto_main_window_init (RsttoMainWindow *);
static void
rstto_main_window_class_init(RsttoMainWindowClass *);
static void
rstto_main_window_dispose(GObject *object);

static void
rstto_main_window_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec);
static void
rstto_main_window_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec);

static gboolean
rstto_window_save_geometry_timer (gpointer user_data);
static gboolean
cb_rstto_main_window_configure_event (GtkWidget *widget, GdkEventConfigure *event);
static void
cb_rstto_main_window_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data);
static gboolean
cb_rstto_main_window_show_fs_toolbar_timeout (RsttoMainWindow *window);
static void
cb_rstto_main_window_image_list_iter_changed (RsttoImageListIter *iter, RsttoMainWindow *window);
static void
rstto_main_window_image_list_iter_changed (RsttoMainWindow *window);

static void
cb_rstto_main_window_image_list_new_image (RsttoImageList *image_list, RsttoImage *image, RsttoMainWindow *window);

static void
cb_rstto_main_window_zoom_100 (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_zoom_fit (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_zoom_in (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_zoom_out (GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_main_window_rotate_cw (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_rotate_ccw (GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_main_window_next_image (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_previous_image (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_first_image (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_last_image (GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_main_window_open_image (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_open_folder (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_open_recent(GtkRecentChooser *chooser, RsttoMainWindow *window);
static void
cb_rstto_main_window_close (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_close_all (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_save_copy (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_delete (GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_main_window_print (GtkWidget *widget, RsttoMainWindow *window);
static void
rstto_main_window_print_draw_page (GtkPrintOperation *operation,
           GtkPrintContext   *print_context,
           gint               page_nr,
          RsttoMainWindow *window);

static void
cb_rstto_main_window_play (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_pause(GtkWidget *widget, RsttoMainWindow *window);
static gboolean
cb_rstto_main_window_play_slideshow (RsttoMainWindow *window);

static void
cb_rstto_main_window_message_bar_open (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_message_bar_cancel (GtkWidget *widget, RsttoMainWindow *window);


static void
cb_rstto_main_window_toggle_show_toolbar (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_fullscreen (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_preferences (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_about (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_contents (GtkWidget *widget, RsttoMainWindow *window);
static void
cb_rstto_main_window_quit (GtkWidget *widget, RsttoMainWindow *window);

static void
cb_rstto_main_window_settings_notify (GObject *settings, GParamSpec *spec, RsttoMainWindow *window);

static gboolean 
cb_rstto_main_window_picture_viewer_motion_notify_event (RsttoPictureViewer *viewer,
                                             GdkEventMotion *event,
                                             gpointer user_data);

static void
rstto_main_window_set_sensitive (RsttoMainWindow *window, gboolean sensitive);

static GtkWidgetClass *parent_class = NULL;

static GtkActionEntry action_entries[] =
{
/* File Menu */
  { "file-menu", NULL, N_ ("_File"), NULL, },
  { "open", "document-open", N_ ("_Open"), "<control>O", N_ ("Open an image"), G_CALLBACK (cb_rstto_main_window_open_image), },
  { "open-folder", "folder-open", N_ ("Open _Folder"), NULL, N_ ("Open a folder"), G_CALLBACK (cb_rstto_main_window_open_folder), },
  { "save-copy", GTK_STOCK_SAVE_AS, N_ ("_Save copy"), "<control>s", N_ ("Save a copy of the image"), G_CALLBACK (cb_rstto_main_window_save_copy), },
  { "print", GTK_STOCK_PRINT, N_ ("_Print"), "<control>p", N_ ("Print the image"), G_CALLBACK (cb_rstto_main_window_print), },
  { "close", GTK_STOCK_CLOSE, N_ ("_Close"), "<control>W", N_ ("Close this image"), G_CALLBACK (cb_rstto_main_window_close), },
  { "close-all", NULL, N_ ("_Close All"), NULL, N_ ("Close all images"), G_CALLBACK (cb_rstto_main_window_close_all), },
  { "quit", GTK_STOCK_QUIT, N_ ("_Quit"), "<control>Q", N_ ("Quit Ristretto"), G_CALLBACK (cb_rstto_main_window_quit), },
/* Edit Menu */
  { "edit-menu", NULL, N_ ("_Edit"), NULL, },
  { "open-with-menu", NULL, N_ ("_Open with..."), NULL, },
  { "sorting-menu", NULL, N_ ("_Sorting"), NULL, },
  { "delete", GTK_STOCK_DELETE, N_ ("_Delete"), "Delete", NULL, G_CALLBACK (cb_rstto_main_window_delete), },
  { "preferences", GTK_STOCK_PREFERENCES, N_ ("_Preferences"), NULL, NULL, G_CALLBACK (cb_rstto_main_window_preferences), },
/* View Menu */
  { "view-menu", NULL, N_ ("_View"), NULL, },
  { "fullscreen", GTK_STOCK_FULLSCREEN, N_ ("_Fullscreen"), "F11", NULL, G_CALLBACK (cb_rstto_main_window_fullscreen), },
  { "unfullscreen", GTK_STOCK_LEAVE_FULLSCREEN, N_ ("_Leave Fullscreen"), NULL, NULL, G_CALLBACK (cb_rstto_main_window_fullscreen), },
  { "set-as-wallpaper", NULL, N_ ("_Set as Wallpaper"), NULL, NULL, NULL, },
/* Thumbnailbar submenu */
  { "thumbnailbar-menu", NULL, N_ ("_Thumbnail Bar"), NULL, },
/* Zoom submenu */
  { "zoom-menu", NULL, N_ ("_Zooming"), NULL, },
  { "zoom-in", GTK_STOCK_ZOOM_IN, N_ ("Zoom _In"), "<control>plus", NULL, G_CALLBACK (cb_rstto_main_window_zoom_in),},
  { "zoom-out", GTK_STOCK_ZOOM_OUT, N_ ("Zoom _Out"), "<control>minus", NULL, G_CALLBACK (cb_rstto_main_window_zoom_out), },
  { "zoom-fit", GTK_STOCK_ZOOM_FIT, N_ ("Zoom _Fit"), "<control>equal", NULL, G_CALLBACK (cb_rstto_main_window_zoom_fit), },
  { "zoom-100", GTK_STOCK_ZOOM_100, N_ ("_Normal Size"), "<control>0", NULL, G_CALLBACK (cb_rstto_main_window_zoom_100), },
/* Rotation submenu */
  { "rotation-menu", NULL, N_ ("_Rotation"), NULL, },
  { "rotate-cw", "object-rotate-right", N_ ("Rotate _Right"), "<control>bracketright", NULL, G_CALLBACK (cb_rstto_main_window_rotate_cw), },
  { "rotate-ccw", "object-rotate-left", N_ ("Rotate _Left"), "<contron>bracketleft", NULL, G_CALLBACK (cb_rstto_main_window_rotate_ccw), },
/* Go Menu */
  { "go-menu",  NULL, N_ ("_Go"), NULL, },
  { "forward",  GTK_STOCK_GO_FORWARD, N_ ("_Forward"), "space", NULL, G_CALLBACK (cb_rstto_main_window_next_image), },
  { "back",     GTK_STOCK_GO_BACK, N_ ("_Back"), "BackSpace", NULL, G_CALLBACK (cb_rstto_main_window_previous_image), },
  { "first",    GTK_STOCK_GOTO_FIRST, N_ ("_First"), "Home", NULL, G_CALLBACK (cb_rstto_main_window_first_image), },
  { "last",     GTK_STOCK_GOTO_LAST, N_ ("_Last"), "End", NULL, G_CALLBACK (cb_rstto_main_window_last_image), },
/* Help Menu */
  { "help-menu", NULL, N_ ("_Help"), NULL, },
  { "contents", GTK_STOCK_HELP,
                N_ ("_Contents"),
                "F1",
                N_ ("Display ristretto user manual"),
                G_CALLBACK (cb_rstto_main_window_contents), },
  { "about",    GTK_STOCK_ABOUT, 
                N_ ("_About"),
                NULL,
                N_ ("Display information about ristretto"),
                G_CALLBACK (cb_rstto_main_window_about), },
/* Misc */
  { "leave-fullscreen", GTK_STOCK_LEAVE_FULLSCREEN, N_ ("Leave _Fullscreen"), NULL, NULL, G_CALLBACK (cb_rstto_main_window_fullscreen), },
  { "tb-menu", NULL, NULL, NULL, }
};

static const GtkToggleActionEntry toggle_action_entries[] =
{
    { "show-toolbar", NULL, N_ ("Show _Toolbar"), NULL, NULL, G_CALLBACK (cb_rstto_main_window_toggle_show_toolbar), TRUE, },
    { "show-thumbnailbar", NULL, N_ ("Show Thumb_nailbar"), NULL, NULL, NULL, FALSE},
};

static const GtkRadioActionEntry radio_action_sort_entries[] = 
{
    {"sort-filename", NULL, N_("sort by filename"), NULL, NULL, 0},
    {"sort-date", NULL, N_("sort by date"), NULL, NULL, 1},
};


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
rstto_main_window_init (RsttoMainWindow *window)
{
    GtkAccelGroup   *accel_group;
    GValue          show_toolbar_val = {0,}, window_width = {0, }, window_height = {0, };
    GtkWidget       *separator, *back, *forward;
    GtkWidget       *main_vbox = gtk_vbox_new (FALSE, 0);
    GtkRecentFilter *recent_filter;

    GClosure        *leave_fullscreen_closure = g_cclosure_new_swap ((GCallback)gtk_window_unfullscreen, window, NULL);
    GClosure        *next_image_closure = g_cclosure_new ((GCallback)cb_rstto_main_window_next_image, window, NULL);
    GClosure        *previous_image_closure = g_cclosure_new ((GCallback)cb_rstto_main_window_previous_image, window, NULL);

    gtk_window_set_title (GTK_WINDOW (window), RISTRETTO_APP_TITLE);

    window->priv = g_new0(RsttoMainWindowPriv, 1);

    window->priv->iter = NULL;

    window->priv->ui_manager = gtk_ui_manager_new ();
    window->priv->recent_manager = gtk_recent_manager_get_default();
    window->priv->settings_manager = rstto_settings_new();

    accel_group = gtk_ui_manager_get_accel_group (window->priv->ui_manager);
    gtk_window_add_accel_group (GTK_WINDOW (window), accel_group);

    gtk_accel_group_connect_by_path (accel_group, "<Window>/unfullscreen", leave_fullscreen_closure);
    gtk_accel_group_connect_by_path (accel_group, "<Window>/next-image", next_image_closure);
    gtk_accel_group_connect_by_path (accel_group, "<Window>/previous-image", previous_image_closure);
    /* Set default accelerators */
    gtk_accel_map_change_entry ("<Window>/unfullscreen", GDK_Escape, 0, FALSE);
    gtk_accel_map_change_entry ("<Window>/next-image", GDK_Page_Down, 0, FALSE);
    gtk_accel_map_change_entry ("<Window>/previous-image", GDK_Page_Up, 0, FALSE);

    /* Create mergeid's for adding ui-components */
    window->priv->recent_merge_id = gtk_ui_manager_new_merge_id (window->priv->ui_manager);
    window->priv->play_merge_id = gtk_ui_manager_new_merge_id (window->priv->ui_manager);
    window->priv->pause_merge_id = gtk_ui_manager_new_merge_id (window->priv->ui_manager);
    window->priv->toolbar_play_merge_id = gtk_ui_manager_new_merge_id (window->priv->ui_manager);
    window->priv->toolbar_pause_merge_id = gtk_ui_manager_new_merge_id (window->priv->ui_manager);
    window->priv->toolbar_fullscreen_merge_id = gtk_ui_manager_new_merge_id (window->priv->ui_manager);
    window->priv->toolbar_unfullscreen_merge_id = gtk_ui_manager_new_merge_id (window->priv->ui_manager);


    window->priv->play_action = gtk_action_new ("play", "_Play", "Play slideshow", GTK_STOCK_MEDIA_PLAY);
    window->priv->pause_action = gtk_action_new ("pause", "_Pause", "Pause slideshow", GTK_STOCK_MEDIA_PAUSE);
    window->priv->recent_action = gtk_recent_action_new_for_manager ("document-open-recent", "_Recently used", "Recently used", 0, GTK_RECENT_MANAGER(window->priv->recent_manager));

    gtk_recent_chooser_set_sort_type (GTK_RECENT_CHOOSER (window->priv->recent_action), GTK_RECENT_SORT_MRU);

    /**
     * Add a filter to the recent-chooser
     */
    recent_filter = gtk_recent_filter_new();
    gtk_recent_filter_add_application (recent_filter, "ristretto");
    gtk_recent_chooser_add_filter(GTK_RECENT_CHOOSER(window->priv->recent_action), recent_filter);

    /* Add the same accelerator path to play and pause, so the same kb-shortcut will be used for starting and stopping the slideshow */
    gtk_action_set_accel_path (window->priv->pause_action, "<Actions>/RsttoWindow/play");
    gtk_action_set_accel_path (window->priv->play_action, "<Actions>/RsttoWindow/play");

    /* Add the play and pause actions to the actiongroup */
    window->priv->action_group = gtk_action_group_new ("RsttoWindow");
    gtk_action_group_add_action (window->priv->action_group,
                                 window->priv->play_action);
    gtk_action_group_add_action (window->priv->action_group,
                                 window->priv->pause_action);
    gtk_action_group_add_action (window->priv->action_group,
                                 window->priv->recent_action);
    /* Connect signal-handlers */
    g_signal_connect(G_OBJECT(window->priv->play_action), "activate", G_CALLBACK(cb_rstto_main_window_play), window);
    g_signal_connect(G_OBJECT(window->priv->pause_action), "activate", G_CALLBACK(cb_rstto_main_window_pause), window);
    g_signal_connect(G_OBJECT(window->priv->recent_action), "item-activated", G_CALLBACK(cb_rstto_main_window_open_recent), window);

    gtk_ui_manager_insert_action_group (window->priv->ui_manager, window->priv->action_group, 0);

    gtk_action_group_set_translation_domain (window->priv->action_group, GETTEXT_PACKAGE);
    gtk_action_group_add_actions (window->priv->action_group, action_entries, G_N_ELEMENTS (action_entries), GTK_WIDGET (window));
    gtk_action_group_add_toggle_actions (window->priv->action_group, toggle_action_entries, G_N_ELEMENTS (toggle_action_entries), GTK_WIDGET (window));
    gtk_action_group_add_radio_actions (window->priv->action_group, radio_action_sort_entries , G_N_ELEMENTS (radio_action_sort_entries), 0, NULL, GTK_WIDGET (window));

    gtk_ui_manager_add_ui_from_string (window->priv->ui_manager,main_window_ui, main_window_ui_length, NULL);
    window->priv->menubar = gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-menu");
    window->priv->toolbar = gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-toolbar");
    window->priv->image_list_toolbar = gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar");
    window->priv->image_list_toolbar_menu = gtk_ui_manager_get_widget (window->priv->ui_manager, "/tb-menu");

    
    
    /**
     * Get the separator toolitem and tell it to expand
     */
    separator = gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar/separator-1");
    gtk_tool_item_set_expand (GTK_TOOL_ITEM (separator), TRUE);
    gtk_separator_tool_item_set_draw (GTK_SEPARATOR_TOOL_ITEM (separator), FALSE);

    /**
     * Make the back and forward toolitems important,
     * when they are, the labels are shown when the toolbar style is 'both-horizontal'
     */
    back = gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar/back");
    gtk_tool_item_set_is_important (GTK_TOOL_ITEM (back), TRUE);
    forward = gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar/forward");
    gtk_tool_item_set_is_important (GTK_TOOL_ITEM (forward), TRUE);
    
    window->priv->picture_viewer = rstto_picture_viewer_new ();
    window->priv->p_viewer_s_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (window->priv->p_viewer_s_window), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (window->priv->p_viewer_s_window), window->priv->picture_viewer);
    window->priv->thumbnail_bar = rstto_thumbnail_bar_new (NULL);
    window->priv->hpaned = gtk_hpaned_new();
    gtk_paned_pack1 (GTK_PANED (window->priv->hpaned), window->priv->p_viewer_s_window, TRUE, FALSE);
    gtk_paned_pack2 (GTK_PANED (window->priv->hpaned), window->priv->thumbnail_bar, FALSE, FALSE);

    window->priv->statusbar = gtk_statusbar_new();

    window->priv->message_bar = gtk_hbox_new (FALSE,0);
    window->priv->message_bar_label = gtk_label_new (N_("Do you want to open all the images in the folder?"));
    window->priv->message_bar_button_cancel = gtk_button_new_from_stock (GTK_STOCK_CANCEL);
    window->priv->message_bar_button_open = gtk_button_new_from_stock (GTK_STOCK_OPEN);

    g_signal_connect(G_OBJECT(window->priv->message_bar_button_cancel), "clicked", G_CALLBACK(cb_rstto_main_window_message_bar_cancel), window);
    g_signal_connect(G_OBJECT(window->priv->message_bar_button_open), "clicked", G_CALLBACK(cb_rstto_main_window_message_bar_open), window);

    gtk_container_set_border_width (GTK_CONTAINER (window->priv->message_bar), 2);
    gtk_box_pack_start (GTK_BOX (window->priv->message_bar), window->priv->message_bar_label, FALSE,FALSE, 5);
    gtk_box_pack_end (GTK_BOX (window->priv->message_bar), window->priv->message_bar_button_cancel, FALSE,FALSE, 5);
    gtk_box_pack_end (GTK_BOX (window->priv->message_bar), window->priv->message_bar_button_open, FALSE,FALSE, 5);

    gtk_container_add (GTK_CONTAINER (window), main_vbox);
    gtk_box_pack_start(GTK_BOX(main_vbox), window->priv->menubar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), window->priv->toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), window->priv->message_bar, FALSE,FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), window->priv->hpaned, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), window->priv->image_list_toolbar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(main_vbox), window->priv->statusbar, FALSE, FALSE, 0);

    rstto_main_window_set_sensitive (window, FALSE);
    gtk_widget_set_no_show_all (window->priv->toolbar, TRUE);
    gtk_widget_set_no_show_all (window->priv->message_bar, TRUE);

    /**
     * Add missing pieces to the UI
     */
    gtk_ui_manager_add_ui (window->priv->ui_manager,
                           window->priv->play_merge_id,
                           "/main-menu/go-menu/placeholder-slideshow",
                           "play",
                           "play",
                           GTK_UI_MANAGER_MENUITEM,
                           FALSE);
    gtk_ui_manager_add_ui (window->priv->ui_manager,
                           window->priv->recent_merge_id,
                           "/main-menu/file-menu/placeholder-open-recent",
                           "document-open-recent",
                           "document-open-recent",
                           GTK_UI_MANAGER_MENUITEM,
                           FALSE);
    gtk_ui_manager_add_ui (window->priv->ui_manager,
                           window->priv->toolbar_play_merge_id,
                           "/navigation-toolbar/placeholder-slideshow",
                           "play",
                           "play",
                           GTK_UI_MANAGER_TOOLITEM,
                           FALSE);
    gtk_ui_manager_add_ui (window->priv->ui_manager,
                           window->priv->toolbar_fullscreen_merge_id,
                           "/navigation-toolbar/placeholder-fullscreen",
                           "fullscreen",
                           "fullscreen",
                           GTK_UI_MANAGER_TOOLITEM,
                           FALSE);

    /**
     * Retrieve the last window-size from the settings-manager
     * and make it the default for this window
     */
    g_value_init (&window_width, G_TYPE_UINT);
    g_value_init (&window_height, G_TYPE_UINT);
    g_object_get_property (G_OBJECT(window->priv->settings_manager), "window-width", &window_width);
    g_object_get_property (G_OBJECT(window->priv->settings_manager), "window-height", &window_height);
    gtk_window_set_default_size(GTK_WINDOW(window), g_value_get_uint (&window_width), g_value_get_uint (&window_height));

    /**
     * Retrieve the toolbar state from the settings-manager
     */
    g_value_init (&show_toolbar_val, G_TYPE_BOOLEAN);
    g_object_get_property (G_OBJECT(window->priv->settings_manager), "show-toolbar", &show_toolbar_val);
    if (g_value_get_boolean (&show_toolbar_val))
    {
        gtk_check_menu_item_set_active (
                GTK_CHECK_MENU_ITEM (
                        gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-menu/view-menu/show-toolbar")),
                TRUE);
        gtk_widget_show (window->priv->toolbar);
    }
    else
    {
        gtk_check_menu_item_set_active (
                GTK_CHECK_MENU_ITEM (
                        gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-menu/view-menu/show-toolbar")),
                FALSE);
        gtk_widget_hide (window->priv->toolbar);
    }

    g_signal_connect(G_OBJECT(window->priv->picture_viewer), "motion-notify-event", G_CALLBACK(cb_rstto_main_window_picture_viewer_motion_notify_event), window);
    g_signal_connect(G_OBJECT(window), "configure-event", G_CALLBACK(cb_rstto_main_window_configure_event), NULL);
    g_signal_connect(G_OBJECT(window), "window-state-event", G_CALLBACK(cb_rstto_main_window_state_event), NULL);

    g_signal_connect(G_OBJECT(window->priv->settings_manager), "notify", G_CALLBACK(cb_rstto_main_window_settings_notify), NULL);
}

static void
rstto_main_window_class_init(RsttoMainWindowClass *window_class)
{
    GParamSpec *pspec;

    GObjectClass *object_class = (GObjectClass*)window_class;
    parent_class = g_type_class_peek_parent(window_class);

    object_class->dispose = rstto_main_window_dispose;

    object_class->set_property = rstto_main_window_set_property;
    object_class->get_property = rstto_main_window_get_property;

    pspec = g_param_spec_object ("image_list",
                                 "",
                                 "",
                                 RSTTO_TYPE_IMAGE_LIST,
                                 G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE);

    g_object_class_install_property (object_class,
                                     PROP_IMAGE_LIST,
                                     pspec);
}

static void
rstto_main_window_dispose(GObject *object)
{
    RsttoMainWindow *window = RSTTO_MAIN_WINDOW(object);
    G_OBJECT_CLASS (parent_class)->dispose(object); 

    if (window->priv->ui_manager)
    {
        g_object_unref (window->priv->ui_manager);
        window->priv->ui_manager = NULL;
    } 

    if (window->priv->settings_manager)
    {
        g_object_unref (window->priv->settings_manager);
        window->priv->settings_manager = NULL;
    }
}

/**
 * rstto_main_window_new:
 * @image_list:
 *
 * Return value:
 */
GtkWidget *
rstto_main_window_new (RsttoImageList *image_list, gboolean fullscreen)
{
    GtkWidget *widget;

    widget = g_object_new (RSTTO_TYPE_MAIN_WINDOW, "image_list", image_list, NULL);

    if (fullscreen == TRUE)
    {
        gtk_window_fullscreen (GTK_WINDOW (widget));
    }

    return widget;
}

static void
cb_rstto_main_window_image_list_iter_changed (RsttoImageListIter *iter, RsttoMainWindow *window)
{
    rstto_main_window_image_list_iter_changed (window);
}

/**
 * rstto_main_window_image_list_iter_changed:
 * @window:
 *
 */
static void
rstto_main_window_image_list_iter_changed (RsttoMainWindow *window)
{
    gchar *path, *basename, *title;
    GFile *file = NULL;
    RsttoImage *cur_image;
    gint position, count;
    RsttoImageList *image_list = window->priv->props.image_list;

    if (window->priv->props.image_list)
    {
        position = rstto_image_list_iter_get_position (window->priv->iter);
        count = rstto_image_list_get_n_images (image_list);
        cur_image = rstto_image_list_iter_get_image (window->priv->iter);
        if (cur_image)
        {
            file = rstto_image_get_file (cur_image);

            path = g_file_get_path (file);
            basename = g_path_get_basename (path);

            title = g_strdup_printf ("%s - %s [%d/%d]", RISTRETTO_APP_TITLE,  basename, position+1, count);
            rstto_main_window_set_sensitive (window, TRUE);

            g_free (basename);
            g_free (path);
        }
        else
        {
            title = g_strdup (RISTRETTO_APP_TITLE);
            rstto_main_window_set_sensitive (window, FALSE);
        }

        gtk_window_set_title (GTK_WINDOW (window), title);

        g_free (title);
    }

}


/**
 * rstto_main_window_set_sensitive:
 * @window:
 * @sensitive:
 *
 */
static void
rstto_main_window_set_sensitive (RsttoMainWindow *window, gboolean sensitive)
{

    gtk_widget_set_sensitive (
            gtk_ui_manager_get_widget (
                    window->priv->ui_manager,
                    "/main-menu/file-menu/save-copy"),
            sensitive);
    gtk_widget_set_sensitive (
            gtk_ui_manager_get_widget (
                    window->priv->ui_manager,
                    "/main-menu/file-menu/print"),
            sensitive);
    gtk_widget_set_sensitive (
            gtk_ui_manager_get_widget (
                    window->priv->ui_manager,
                    "/main-menu/file-menu/close"),
            sensitive);
    gtk_widget_set_sensitive (
            gtk_ui_manager_get_widget (
                    window->priv->ui_manager,
                    "/main-menu/file-menu/close-all"),
            sensitive);

    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-menu/edit-menu/delete"), sensitive);

    /* Go Menu */
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-menu/go-menu/forward"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-menu/go-menu/back"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-menu/go-menu/first"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-menu/go-menu/last"), sensitive); 

    gtk_action_set_sensitive (window->priv->play_action, sensitive);
    gtk_action_set_sensitive (window->priv->pause_action, sensitive);
    

    /* View Menu */
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, 
                                                         "/main-menu/view-menu/set-as-wallpaper"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, 
                                                         "/main-menu/view-menu/zoom-menu"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, 
                                                         "/main-menu/view-menu/rotation-menu"), sensitive);
    /*
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, 
                                                         "/main-menu/view-menu/zoom-menu/zoom-in"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, 
                                                         "/main-menu/view-menu/zoom-menu/zoom-out"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, 
                                                         "/main-menu/view-menu/zoom-menu/zoom-fit"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, 
                                                         "/main-menu/view-menu/zoom-menu/zoom-100"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, 
                                                         "/main-menu/view-menu/rotation-menu/rotate-cw"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, 
                                                         "/main-menu/view-menu/rotation-menu/rotate-ccw"), sensitive);
    */

    /* Toolbar */
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-toolbar/save-copy"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-toolbar/close"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/main-toolbar/delete"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar/forward"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar/back"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar/zoom-in"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar/zoom-out"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar/zoom-fit"), sensitive);
    gtk_widget_set_sensitive (gtk_ui_manager_get_widget (window->priv->ui_manager, "/navigation-toolbar/zoom-100"), sensitive);

}

/**
 * rstto_main_window_set_property:
 * @object:
 * @property_id:
 * @value:
 * @pspec:
 *
 */
static void
rstto_main_window_set_property (GObject      *object,
                                guint         property_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
    RsttoMainWindow *window = RSTTO_MAIN_WINDOW (object);

    switch (property_id)
    {
        case PROP_IMAGE_LIST:
            if (window->priv->props.image_list)
            {
                g_signal_handlers_disconnect_by_func (window->priv->props.image_list, cb_rstto_main_window_image_list_new_image, window);
                g_object_unref (window->priv->props.image_list);

                g_signal_handlers_disconnect_by_func (window->priv->iter, cb_rstto_main_window_image_list_iter_changed, window);
                g_object_unref (window->priv->iter);
                window->priv->iter = NULL;
            }

            window->priv->props.image_list = g_value_get_object (value);

            if (window->priv->props.image_list)
            {
                g_object_ref (window->priv->props.image_list);
                g_signal_connect (G_OBJECT (window->priv->props.image_list), "new-image", G_CALLBACK (cb_rstto_main_window_image_list_new_image), window);

                window->priv->iter = rstto_image_list_get_iter (window->priv->props.image_list);
                g_signal_connect (G_OBJECT (window->priv->iter), "changed", G_CALLBACK (cb_rstto_main_window_image_list_iter_changed), window);
                rstto_thumbnail_bar_set_image_list (RSTTO_THUMBNAIL_BAR (window->priv->thumbnail_bar), window->priv->props.image_list);
                rstto_thumbnail_bar_set_iter (RSTTO_THUMBNAIL_BAR (window->priv->thumbnail_bar), window->priv->iter);
                rstto_picture_viewer_set_iter (RSTTO_PICTURE_VIEWER (window->priv->picture_viewer), window->priv->iter);
            }
            break;
        default:
            break;
    }
}

/**
 * rstto_main_window_get_property:
 * @object:
 * @property_id:
 * @value:
 * @pspec:
 *
 */
static void
rstto_main_window_get_property (GObject    *object,
                                guint       property_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
    RsttoMainWindow *window = RSTTO_MAIN_WINDOW (object);

    switch (property_id)
    {
        case PROP_IMAGE_LIST:
            g_value_set_object (value, window->priv->props.image_list);
            break;
        default:
            break;
    }
}

/**
 * cb_rstto_main_window_zoom_fit:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_zoom_fit (GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_zoom_fit (RSTTO_PICTURE_VIEWER (window->priv->picture_viewer));
}

/**
 * cb_rstto_main_window_zoom_100:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_zoom_100 (GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_zoom_100 (RSTTO_PICTURE_VIEWER (window->priv->picture_viewer));
}

/**
 * cb_rstto_main_window_zoom_in:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_zoom_in (GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_zoom_in (RSTTO_PICTURE_VIEWER (window->priv->picture_viewer), ZOOM_FACTOR);
}

/**
 * cb_rstto_main_window_zoom_out:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_zoom_out (GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_picture_viewer_zoom_out (RSTTO_PICTURE_VIEWER (window->priv->picture_viewer), ZOOM_FACTOR);
}

/**********************/
/* ROTATION CALLBACKS */
/**********************/

/**
 * cb_rstto_main_window_rotate_cw:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_rotate_cw (GtkWidget *widget, RsttoMainWindow *window)
{
    RsttoImage *image = NULL;

    if (window->priv->iter)
        image = rstto_image_list_iter_get_image (window->priv->iter);

    if (image)
    {
        switch (rstto_image_get_orientation (image))
        {
            default:
            case RSTTO_IMAGE_ORIENT_NONE:
                rstto_image_set_orientation (image, RSTTO_IMAGE_ORIENT_90);
                break;
            case RSTTO_IMAGE_ORIENT_90:
                rstto_image_set_orientation (image, RSTTO_IMAGE_ORIENT_180);
                break;
            case RSTTO_IMAGE_ORIENT_180:
                rstto_image_set_orientation (image, RSTTO_IMAGE_ORIENT_270);
                break;
            case RSTTO_IMAGE_ORIENT_270:
                rstto_image_set_orientation (image, RSTTO_IMAGE_ORIENT_NONE);
                break;
        }
    }
}

/**
 * cb_rstto_main_window_rotate_ccw:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_rotate_ccw (GtkWidget *widget, RsttoMainWindow *window)
{
    RsttoImage *image = NULL;

    if (window->priv->iter)
        image = rstto_image_list_iter_get_image (window->priv->iter);

    if (image)
    {
        switch (rstto_image_get_orientation (image))
        {
            default:
            case RSTTO_IMAGE_ORIENT_NONE:
                rstto_image_set_orientation (image, RSTTO_IMAGE_ORIENT_270);
                break;
            case RSTTO_IMAGE_ORIENT_90:
                rstto_image_set_orientation (image, RSTTO_IMAGE_ORIENT_NONE);
                break;
            case RSTTO_IMAGE_ORIENT_180:
                rstto_image_set_orientation (image, RSTTO_IMAGE_ORIENT_90);
                break;
            case RSTTO_IMAGE_ORIENT_270:
                rstto_image_set_orientation (image, RSTTO_IMAGE_ORIENT_180);
                break;
        }
    }
}


/************************/
/* NAVIGATION CALLBACKS */
/************************/

/**
 * cb_rstto_main_window_first_image:
 * @widget:
 * @window:
 *
 * Move the iter to the first image;
 *
 */
static void
cb_rstto_main_window_first_image (GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_image_list_iter_set_position (window->priv->iter, 0);
}


/**
 * cb_rstto_main_window_last_image:
 * @widget:
 * @window:
 *
 * Move the iter to the last image;
 *
 */
static void
cb_rstto_main_window_last_image (GtkWidget *widget, RsttoMainWindow *window)
{
    guint n_images = rstto_image_list_get_n_images (window->priv->props.image_list);
    rstto_image_list_iter_set_position (window->priv->iter, n_images-1);
}

/**
 * cb_rstto_main_window_next_image:
 * @widget:
 * @window:
 *
 * Move the iter to the next image;
 *
 */
static void
cb_rstto_main_window_next_image (GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_image_list_iter_next (window->priv->iter);
}

/**
 * cb_rstto_main_window_previous_image:
 * @widget:
 * @window:
 *
 * Move the iter to the previous image;
 *
 */
static void
cb_rstto_main_window_previous_image (GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_image_list_iter_previous (window->priv->iter);
}

/**
 * cb_rstto_main_window_open_image:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_open_image (GtkWidget *widget, RsttoMainWindow *window)
{
    GtkWidget *dialog, *err_dialog;
    gint response;
    GFile *file;
    GSList *files = NULL, *_files_iter;
    GValue current_uri_val = {0, };
    gchar *uri = NULL;
    guint pos = 0;

    g_value_init (&current_uri_val, G_TYPE_STRING);
    g_object_get_property (G_OBJECT(window->priv->settings_manager), "current-uri", &current_uri_val);

    GtkFileFilter *filter = gtk_file_filter_new();

    dialog = gtk_file_chooser_dialog_new(_("Open image"),
                                         GTK_WINDOW(window),
                                         GTK_FILE_CHOOSER_ACTION_OPEN,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_OPEN, GTK_RESPONSE_OK,
                                         NULL);

    gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (dialog), TRUE);
    if (g_value_get_string (&current_uri_val))
        gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog), g_value_get_string (&current_uri_val));

    gtk_file_filter_add_pixbuf_formats (filter);
    gtk_file_filter_set_name (filter, _("Images"));
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);

    filter = gtk_file_filter_new();
    gtk_file_filter_add_mime_type (filter, "image/jpeg");
    gtk_file_filter_set_name (filter, _(".jp(e)g"));
    gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (dialog), filter);


    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_hide (dialog);
    if(response == GTK_RESPONSE_OK)
    {
        files = gtk_file_chooser_get_files (GTK_FILE_CHOOSER (dialog));
        _files_iter = files;
        pos = rstto_image_list_iter_get_position (window->priv->iter);
        if (g_slist_length (files) > 1)
        {
            while (_files_iter)
            {
                file = _files_iter->data;
                if (rstto_image_list_add_file (window->priv->props.image_list, file, NULL) == FALSE)
                {
                    err_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                                    GTK_DIALOG_MODAL,
                                                    GTK_MESSAGE_ERROR,
                                                    GTK_BUTTONS_OK,
                                                    _("Could not open file"));
                    gtk_dialog_run(GTK_DIALOG(dialog));
                    gtk_widget_destroy(dialog);
                }
                else
                {
                    uri = g_file_get_uri (_files_iter->data);
                    gtk_recent_manager_add_item (window->priv->recent_manager, uri);
                    g_free (uri);
                    uri = NULL;
                }
                _files_iter = g_slist_next (_files_iter);
            }
        }
        else
        {

            if (rstto_image_list_add_file (window->priv->props.image_list, files->data, NULL) == FALSE)
            {
                err_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK,
                                                _("Could not open file"));
                gtk_dialog_run(GTK_DIALOG(dialog));
                gtk_widget_destroy(dialog);
            }
            else
            {
                gtk_widget_show (window->priv->message_bar);
                gtk_widget_show (window->priv->message_bar_label);
                gtk_widget_show (window->priv->message_bar_button_cancel);
                gtk_widget_show (window->priv->message_bar_button_open);

                if (window->priv->message_bar_file)
                {
                    g_object_unref (window->priv->message_bar_file);
                    window->priv->message_bar_file = NULL;
                }
                window->priv->message_bar_file = g_file_get_parent (files->data);
            }
        }

        if (pos == -1)
            rstto_image_list_iter_set_position (window->priv->iter, 0);
        g_value_set_string (&current_uri_val, gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog)));
        g_object_set_property (G_OBJECT(window->priv->settings_manager), "current-uri", &current_uri_val);

    }

    gtk_widget_destroy(dialog);

    if (files)
    {
        g_slist_foreach (files, (GFunc)g_object_unref, NULL);
        g_slist_free (files);
    }
}

/**
 * cb_rstto_main_window_open_folder:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_open_folder (GtkWidget *widget, RsttoMainWindow *window)
{
    gint response;
    GFile *file = NULL, *child_file = NULL;
    GFileEnumerator *file_enumarator = NULL;
    GFileInfo *file_info = NULL;
    const gchar *filename = NULL;
    const gchar *content_type = NULL;
    gchar *uri = NULL;
    GValue current_uri_val = {0, };
    guint pos = 0;

    g_value_init (&current_uri_val, G_TYPE_STRING);
    g_object_get_property (G_OBJECT(window->priv->settings_manager), "current-uri", &current_uri_val);

    GtkWidget *dialog = gtk_file_chooser_dialog_new(_("Open folder"),
                                                    GTK_WINDOW(window),
                                                    GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_OPEN, GTK_RESPONSE_OK,
                                                    NULL);

    if (g_value_get_string (&current_uri_val))
        gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog), g_value_get_string (&current_uri_val));

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if(response == GTK_RESPONSE_OK)
    {
        gtk_widget_hide(dialog);
        file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));

        file_enumarator = g_file_enumerate_children (file, "standard::*", 0, NULL, NULL);
        pos = rstto_image_list_iter_get_position (window->priv->iter);
        for(file_info = g_file_enumerator_next_file (file_enumarator, NULL, NULL); file_info != NULL; file_info = g_file_enumerator_next_file (file_enumarator, NULL, NULL))
        {
            filename = g_file_info_get_name (file_info);
            content_type  = g_file_info_get_content_type (file_info);
            child_file = g_file_get_child (file, filename);

            if (strncmp (content_type, "image/", 6) == 0)
            {
                rstto_image_list_add_file (window->priv->props.image_list, child_file, NULL);
            }

            g_object_unref (child_file);
            g_object_unref (file_info);
        }

        if (pos == -1)
            rstto_image_list_iter_set_position (window->priv->iter, 0);

        uri = g_file_get_uri (file);
        gtk_recent_manager_add_item (window->priv->recent_manager, uri);
        g_free (uri);
        uri = NULL;

        g_value_set_string (&current_uri_val, gtk_file_chooser_get_current_folder_uri (GTK_FILE_CHOOSER (dialog)));
        g_object_set_property (G_OBJECT(window->priv->settings_manager), "current-uri", &current_uri_val);
    }

    gtk_widget_destroy(dialog);

    if (file)
    {
        g_object_unref (file);
    }
}

/**
 * cb_rstto_main_window_open_recent:
 * @chooser:
 * @window:
 *
 */
static void
cb_rstto_main_window_open_recent(GtkRecentChooser *chooser, RsttoMainWindow *window)
{
    GtkWidget *err_dialog;
    gchar *uri = gtk_recent_chooser_get_current_uri (chooser);
    const gchar *filename;
    GError *error = NULL;
    GFile *file = g_file_new_for_uri (uri);
    GFile *child_file;
    GFileEnumerator *file_enumarator = NULL;
    GFileInfo *child_file_info = NULL;
    GFileInfo *file_info = g_file_query_info (file, "standard::type", 0, NULL, &error);

    if (error == NULL)
    {
        if (g_file_info_get_file_type (file_info) == G_FILE_TYPE_DIRECTORY)
        {
            file_enumarator = g_file_enumerate_children (file, "standard::name", 0, NULL, NULL);
            for(child_file_info = g_file_enumerator_next_file (file_enumarator, NULL, NULL); child_file_info != NULL; child_file_info = g_file_enumerator_next_file (file_enumarator, NULL, NULL))
            {
                filename = g_file_info_get_name (child_file_info);
                child_file = g_file_get_child (file, filename);

                rstto_image_list_add_file (window->priv->props.image_list, child_file, NULL);

                g_object_unref (child_file);
                g_object_unref (child_file_info);
            }

        }
        else
        {
            if (rstto_image_list_add_file (window->priv->props.image_list, file, NULL) == FALSE)
            {
                err_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                                GTK_DIALOG_MODAL,
                                                GTK_MESSAGE_ERROR,
                                                GTK_BUTTONS_OK,
                                                _("Could not open file"));
                gtk_dialog_run( GTK_DIALOG(err_dialog));
                gtk_widget_destroy(err_dialog);
            }
            else
            {
                gtk_widget_show (window->priv->message_bar);
                gtk_widget_show (window->priv->message_bar_label);
                gtk_widget_show (window->priv->message_bar_button_cancel);
                gtk_widget_show (window->priv->message_bar_button_open);

                if (window->priv->message_bar_file)
                {
                    g_object_unref (window->priv->message_bar_file);
                    window->priv->message_bar_file = NULL;
                }
                window->priv->message_bar_file = g_file_get_parent (file);
            }
        }
    }
    else
    {
        err_dialog = gtk_message_dialog_new(GTK_WINDOW(window),
                                        GTK_DIALOG_MODAL,
                                        GTK_MESSAGE_ERROR,
                                        GTK_BUTTONS_OK,
                                        _("Could not open file"));
        gtk_dialog_run (GTK_DIALOG (err_dialog));
        gtk_widget_destroy (err_dialog);
    }

    g_object_unref (file);
    g_free (uri);
}

static void
cb_rstto_main_window_save_copy (GtkWidget *widget, RsttoMainWindow *window)
{
    GtkWidget *dialog;
    gint response;
    GFile *file, *s_file;

    dialog = gtk_file_chooser_dialog_new(_("Save copy"),
                                         GTK_WINDOW(window),
                                         GTK_FILE_CHOOSER_ACTION_SAVE,
                                         GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                         GTK_STOCK_SAVE, GTK_RESPONSE_OK,
                                         NULL);
    gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (dialog), TRUE);

    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if(response == GTK_RESPONSE_OK)
    {
        file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (dialog));
        s_file = rstto_image_get_file (rstto_image_list_iter_get_image (window->priv->iter));
        if (g_file_copy (s_file, file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL))
        {
            rstto_image_list_add_file (window->priv->props.image_list, file, NULL);
        }
    }

    gtk_widget_destroy(dialog);

}

/**
 * cb_rstto_main_window_print:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_print (GtkWidget *widget, RsttoMainWindow *window)
{

    GtkPrintSettings *print_settings = gtk_print_settings_new ();
    gtk_print_settings_set_resolution (print_settings, 300);

    GtkPrintOperation *print_operation = gtk_print_operation_new (); 
    GtkPageSetup *page_setup = gtk_page_setup_new ();
    gtk_page_setup_set_orientation (page_setup, GTK_PAGE_ORIENTATION_LANDSCAPE);

    gtk_print_operation_set_default_page_setup (print_operation, page_setup);
    gtk_print_operation_set_print_settings (print_operation, print_settings);

    g_object_set (print_operation,
                  "n-pages", 1,
                  NULL);
    
    g_signal_connect (print_operation, "draw-page", G_CALLBACK (rstto_main_window_print_draw_page), window);

    gtk_print_operation_run (print_operation, GTK_PRINT_OPERATION_ACTION_PRINT_DIALOG, GTK_WINDOW(window), NULL);
    
}

static void
rstto_main_window_print_draw_page (GtkPrintOperation *operation,
           GtkPrintContext   *print_context,
           gint               page_nr,
           RsttoMainWindow *window)
{
    RsttoImage *image = rstto_image_list_iter_get_image (window->priv->iter);
    GdkPixbuf *pixbuf = rstto_image_get_pixbuf (image);
    gdouble w = gdk_pixbuf_get_width (pixbuf);
    gdouble w1 = gtk_print_context_get_width (print_context);
    gdouble h = gdk_pixbuf_get_height (pixbuf);
    gdouble h1 = gtk_print_context_get_height (print_context);

    cairo_t *context = gtk_print_context_get_cairo_context (print_context);

    cairo_translate (context, 0, 0);
    /* Scale to page-width */
    if ((w1/w) < (h1/h))
    {
        cairo_scale (context, w1/w, w1/w);
    }
    else
    {
        cairo_scale (context, h1/h, h1/h);
    }
    //cairo_rotate (context, 90 * 3.141592/180);
    gdk_cairo_set_source_pixbuf (context, pixbuf, 0, 0);

    //cairo_rectangle (context, 0, 0, 200, 200);

    cairo_paint (context);
}


/**
 * cb_rstto_main_window_play:
 * @widget:
 * @window:
 *
 * Remove the play button from the menu, and add the pause button.
 *
 */
static void
cb_rstto_main_window_play (GtkWidget *widget, RsttoMainWindow *window)
{
    GValue timeout = {0, };

    gtk_ui_manager_add_ui (window->priv->ui_manager,
                           window->priv->pause_merge_id,
                           "/main-menu/go-menu/placeholder-slideshow",
                           "pause",
                           "pause",
                           GTK_UI_MANAGER_MENUITEM,
                           FALSE);
    gtk_ui_manager_remove_ui (window->priv->ui_manager,
                              window->priv->play_merge_id);

    gtk_ui_manager_add_ui (window->priv->ui_manager,
                           window->priv->toolbar_pause_merge_id,
                           "/navigation-toolbar/placeholder-slideshow",
                           "pause",
                           "pause",
                           GTK_UI_MANAGER_TOOLITEM,
                           FALSE);
    gtk_ui_manager_remove_ui (window->priv->ui_manager,
                              window->priv->toolbar_play_merge_id);


    g_value_init (&timeout, G_TYPE_UINT);
    g_object_get_property (G_OBJECT(window->priv->settings_manager), "slideshow-timeout", &timeout);

    window->priv->playing = TRUE;
    window->priv->play_timeout_id = g_timeout_add (g_value_get_uint (&timeout), (GSourceFunc)cb_rstto_main_window_play_slideshow, window);
}

/**
 * cb_rstto_main_window_pause:
 * @widget:
 * @window:
 *
 * Remove the pause button from the menu, and add the play button.
 *
 */
static void
cb_rstto_main_window_pause (GtkWidget *widget, RsttoMainWindow *window)
{
    gtk_ui_manager_add_ui (window->priv->ui_manager,
                           window->priv->play_merge_id,
                           "/main-menu/go-menu/placeholder-slideshow",
                           "play",
                           "play",
                           GTK_UI_MANAGER_MENUITEM,
                           FALSE);
    gtk_ui_manager_remove_ui (window->priv->ui_manager,
                              window->priv->pause_merge_id);

    gtk_ui_manager_add_ui (window->priv->ui_manager,
                           window->priv->toolbar_play_merge_id,
                           "/navigation-toolbar/placeholder-slideshow",
                           "play",
                           "play",
                           GTK_UI_MANAGER_TOOLITEM,
                           FALSE);
    gtk_ui_manager_remove_ui (window->priv->ui_manager,
                              window->priv->toolbar_pause_merge_id);

    window->priv->playing = FALSE;
}

/**
 * cb_rstto_main_window_play_slideshow:
 * @window:
 *
 */
static gboolean
cb_rstto_main_window_play_slideshow (RsttoMainWindow *window)
{
    if (window->priv->playing)
    {
        rstto_image_list_iter_next (window->priv->iter);
        rstto_main_window_image_list_iter_changed (window);
    }
    else
    {
        window->priv->play_timeout_id  = 0;
    }
    return window->priv->playing;
}

/**
 * cb_rstto_main_window_fullscreen:
 * @widget:
 * @window:
 *
 * Toggle the fullscreen mode of this window.
 *
 */
static void
cb_rstto_main_window_fullscreen (GtkWidget *widget, RsttoMainWindow *window)
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

/**
 * cb_rstto_main_window_preferences:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_preferences (GtkWidget *widget, RsttoMainWindow *window)
{
    GValue val1 = {0,};
    g_value_init (&val1, G_TYPE_UINT);
    GValue val2 = {0,};
    g_value_init (&val2, G_TYPE_UINT);

    GtkWidget *dialog = rstto_preferences_dialog_new (GTK_WINDOW (window));

    g_object_get_property (G_OBJECT (window->priv->settings_manager), "image-quality", &val1);

    gtk_dialog_run (GTK_DIALOG (dialog));

    g_object_get_property (G_OBJECT (window->priv->settings_manager), "image-quality", &val2);

    if (g_value_get_uint (&val1) != g_value_get_uint (&val2))
    {
        rstto_image_cache_clear (rstto_image_cache_new());
    }

    gtk_widget_destroy (dialog);
}

/**
 * cb_rstto_main_window_about:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_about (GtkWidget *widget, RsttoMainWindow *window)
{
    const gchar *authors[] = {
      _("Developer:"),
        "Stephan Arts <stephan@xfce.org>",
        NULL};

    GtkWidget *about_dialog = gtk_about_dialog_new();

    gtk_about_dialog_set_name((GtkAboutDialog *)about_dialog, PACKAGE_NAME);
    gtk_about_dialog_set_version((GtkAboutDialog *)about_dialog, PACKAGE_VERSION);

    gtk_about_dialog_set_comments((GtkAboutDialog *)about_dialog,
        _("Ristretto is an imageviewer for the Xfce desktop environment."));
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
        "Copyright \302\251 2006-2009 Stephan Arts");

    gtk_dialog_run(GTK_DIALOG(about_dialog));

    gtk_widget_destroy(about_dialog);
}

/**
 * cb_rstto_main_window_contents:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_contents (GtkWidget *widget, RsttoMainWindow *window)
{
    g_debug ("%s", __FUNCTION__);
}


/**
 * cb_rstto_main_window_quit:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_quit (GtkWidget *widget, RsttoMainWindow *window)
{
    gtk_widget_destroy (GTK_WIDGET (window));
}

/**
 * cb_rstto_main_window_close:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_close (GtkWidget *widget, RsttoMainWindow *window)
{
    RsttoImage *image = rstto_image_list_iter_get_image (window->priv->iter);
    rstto_image_list_remove_image (window->priv->props.image_list, image);
}

/**
 * cb_rstto_main_window_close_all:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_close_all (GtkWidget *widget, RsttoMainWindow *window)
{
    rstto_image_list_remove_all (window->priv->props.image_list);
    rstto_main_window_image_list_iter_changed (window);
}

/**
 * cb_rstto_main_window_delete:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_delete (GtkWidget *widget, RsttoMainWindow *window)
{
    RsttoImage *image = rstto_image_list_iter_get_image (window->priv->iter);
    GFile *file = rstto_image_get_file (image);
    gchar *path = g_file_get_path (file);
    gchar *basename = g_path_get_basename (path);
    g_object_ref (image);
    GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (window),
                                                GTK_DIALOG_DESTROY_WITH_PARENT,
                                                GTK_MESSAGE_WARNING,
                                                GTK_BUTTONS_OK_CANCEL,
                                                N_("Are you sure you want to delete image '%s' from disk?"),
                                                basename);
    if (gtk_dialog_run (GTK_DIALOG (dialog)) == GTK_RESPONSE_OK)
    {
        if (g_file_trash (file, NULL, NULL) == TRUE)
        {
            rstto_image_list_remove_image (window->priv->props.image_list, image);
        }
        else
        {
            
        }
    }
    gtk_widget_destroy (dialog);
    g_free (basename);
    g_free (path);
    g_object_unref (image);
}

/**
 * cb_rstto_main_window_toggle_show_toolbar:
 * @widget:
 * @window:
 *
 *
 */
static void
cb_rstto_main_window_toggle_show_toolbar (GtkWidget *widget, RsttoMainWindow *window)
{
    GValue val = {0,};
    g_value_init (&val, G_TYPE_BOOLEAN);

    if (gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (widget)))
    {
        gtk_widget_show (window->priv->toolbar);
        g_value_set_boolean (&val, TRUE);
    }
    else
    {
        gtk_widget_hide (window->priv->toolbar);
        g_value_set_boolean (&val, FALSE);
    }
    g_object_set_property (G_OBJECT (window->priv->settings_manager), "show-toolbar", &val);
}

/**
 * cb_rstto_main_window_image_list_new_image:
 * @image_list:
 * @image:
 * @window:
 *
 */
static void
cb_rstto_main_window_image_list_new_image (RsttoImageList *image_list, RsttoImage *image, RsttoMainWindow *window)
{
    if (rstto_image_list_iter_get_position (window->priv->iter) == -1)
        rstto_image_list_iter_set_position (window->priv->iter, 0);
    rstto_main_window_image_list_iter_changed (window);
}

static gboolean
cb_rstto_main_window_configure_event (GtkWidget *widget, GdkEventConfigure *event)
{
    RsttoMainWindow *window = RSTTO_MAIN_WINDOW(widget);
    /* shamelessly copied from thunar, written by benny */
    /* check if we have a new dimension here */
    if (widget->allocation.width != event->width || widget->allocation.height != event->height)
    {
        /* drop any previous timer source */
        if (window->priv->window_save_geometry_timer_id > 0)
        {
            g_source_remove (window->priv->window_save_geometry_timer_id);
        }
        window->priv->window_save_geometry_timer_id = 0;

        /* check if we should schedule another save timer */
        if (GTK_WIDGET_VISIBLE (widget))
        {
            /* save the geometry one second after the last configure event */
            window->priv->window_save_geometry_timer_id = g_timeout_add (
                    1000, rstto_window_save_geometry_timer,
                    widget);
        }
    }

    /* let Gtk+ handle the configure event */
    return FALSE;
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
            g_object_set (G_OBJECT (RSTTO_MAIN_WINDOW(window)->priv->settings_manager), 
                          "window-width", width,
                          "window-height", height,
                          NULL);
        }
    }
    return FALSE;
}

static void
cb_rstto_main_window_state_event(GtkWidget *widget, GdkEventWindowState *event, gpointer user_data)
{
    RsttoMainWindow *window = RSTTO_MAIN_WINDOW(widget);
    GValue           show_toolbar_val = {0,};

    if(event->changed_mask & GDK_WINDOW_STATE_FULLSCREEN)
    {
        if(event->new_window_state & GDK_WINDOW_STATE_FULLSCREEN)
        {
            gtk_widget_hide (window->priv->menubar);
            gtk_widget_hide (window->priv->toolbar);
            gtk_widget_hide (window->priv->statusbar);
            if (window->priv->fs_toolbar_sticky)
            {
                if (window->priv->show_fs_toolbar_timeout_id > 0)
                {
                    g_source_remove (window->priv->show_fs_toolbar_timeout_id);
                    window->priv->show_fs_toolbar_timeout_id = 0;
                }
                window->priv->show_fs_toolbar_timeout_id = g_timeout_add (3000, (GSourceFunc)cb_rstto_main_window_show_fs_toolbar_timeout, window);
            }
            else
            {
                gtk_widget_hide (window->priv->image_list_toolbar);
            }

            rstto_picture_viewer_zoom_fit (RSTTO_PICTURE_VIEWER (window->priv->picture_viewer));

            gtk_ui_manager_add_ui (window->priv->ui_manager,
                                   window->priv->toolbar_unfullscreen_merge_id,
                                   "/navigation-toolbar/placeholder-fullscreen",
                                   "unfullscreen",
                                   "unfullscreen",
                                   GTK_UI_MANAGER_TOOLITEM,
                                   FALSE);
            gtk_ui_manager_remove_ui (window->priv->ui_manager,
                                   window->priv->toolbar_fullscreen_merge_id);
        }
        else
        {
            gtk_ui_manager_add_ui (window->priv->ui_manager,
                                   window->priv->toolbar_fullscreen_merge_id,
                                   "/navigation-toolbar/placeholder-fullscreen",
                                   "fullscreen",
                                   "fullscreen",
                                   GTK_UI_MANAGER_TOOLITEM,
                                   FALSE);
            gtk_ui_manager_remove_ui (window->priv->ui_manager,
                                   window->priv->toolbar_unfullscreen_merge_id);
            if (window->priv->show_fs_toolbar_timeout_id > 0)
            {
                g_source_remove (window->priv->show_fs_toolbar_timeout_id);
                window->priv->show_fs_toolbar_timeout_id = 0;
            }
            gtk_widget_show (window->priv->image_list_toolbar);

            g_value_init (&show_toolbar_val, G_TYPE_BOOLEAN);
            g_object_get_property (G_OBJECT(window->priv->settings_manager), "show-toolbar", &show_toolbar_val);

            gtk_widget_show (window->priv->menubar);
            gtk_widget_show (window->priv->statusbar);

            if (g_value_get_boolean (&show_toolbar_val))
                gtk_widget_show (window->priv->toolbar);
            
            g_value_reset (&show_toolbar_val);
        }
    }
    if (event->changed_mask & GDK_WINDOW_STATE_MAXIMIZED)
    {
    }
}

static gboolean 
cb_rstto_main_window_picture_viewer_motion_notify_event (RsttoPictureViewer *viewer,
                                             GdkEventMotion *event,
                                             gpointer user_data)
{
    RsttoMainWindow *window = RSTTO_MAIN_WINDOW (user_data);
    if(gdk_window_get_state(GTK_WIDGET(window)->window) & GDK_WINDOW_STATE_FULLSCREEN)
    {
        if (event->state == 0)
        {
            gtk_widget_show (window->priv->image_list_toolbar);

            if (window->priv->fs_toolbar_sticky == FALSE)
            {
                if (window->priv->show_fs_toolbar_timeout_id > 0)
                {
                    g_source_remove (window->priv->show_fs_toolbar_timeout_id);
                    window->priv->show_fs_toolbar_timeout_id = 0;
                }
                window->priv->show_fs_toolbar_timeout_id = g_timeout_add (3000, (GSourceFunc)cb_rstto_main_window_show_fs_toolbar_timeout, window);
            }
        }
    }
    return TRUE;
}

static gboolean
cb_rstto_main_window_show_fs_toolbar_timeout (RsttoMainWindow *window)
{
    gtk_widget_hide (window->priv->image_list_toolbar);
    return FALSE;
}

static void
cb_rstto_main_window_settings_notify (GObject *settings, GParamSpec *spec, RsttoMainWindow *window)
{
    GValue val = {0,};
    g_return_if_fail (RSTTO_IS_SETTINGS (settings));
    g_return_if_fail (RSTTO_IS_MAIN_WINDOW (window));
    g_return_if_fail (G_PARAM_SPEC_VALUE_TYPE (spec) == G_TYPE_STRING);

    g_value_init (&val, spec->value_type);
    g_object_get_property (settings, spec->name, &val);


    g_value_unset (&val);
}

/**
 * cb_rstto_main_window_message_bar_cancel:
 * @widget:
 * @window:
 *
 */
static void
cb_rstto_main_window_message_bar_cancel (GtkWidget *widget, RsttoMainWindow *window)
{
    gtk_widget_hide (window->priv->message_bar);
    if (window->priv->message_bar_file)
    {
        g_object_unref (window->priv->message_bar_file);
        window->priv->message_bar_file = NULL;
    }
}

/**
 * cb_rstto_main_window_message_bar_open:
 * @widget:
 * @window:
 *
 */
static void
cb_rstto_main_window_message_bar_open (GtkWidget *widget, RsttoMainWindow *window)
{
    gtk_widget_hide (window->priv->message_bar);

    GFile *child_file = NULL;
    GFileEnumerator *file_enumarator = NULL;
    GFileInfo *file_info = NULL;
    const gchar *filename = NULL;
    const gchar *content_type = NULL;

    file_enumarator = g_file_enumerate_children (window->priv->message_bar_file, "standard::*", 0, NULL, NULL);
    for(file_info = g_file_enumerator_next_file (file_enumarator, NULL, NULL); file_info != NULL; file_info = g_file_enumerator_next_file (file_enumarator, NULL, NULL))
    {
        filename = g_file_info_get_name (file_info);
        content_type  = g_file_info_get_content_type (file_info);
        child_file = g_file_get_child (window->priv->message_bar_file, filename);

        if (strncmp (content_type, "image/", 6) == 0)
        {
            rstto_image_list_add_file (window->priv->props.image_list, child_file, NULL);
        }

        g_object_unref (child_file);
        g_object_unref (file_info);
    }

    
    if (window->priv->message_bar_file)
    {
        g_object_unref (window->priv->message_bar_file);
        window->priv->message_bar_file = NULL;
    }
}

/*
static gboolean
cb_rstto_main_window_image_list_toolbar_popup_context_menu (GtkToolbar *toolbar,
                                                        gint        x,
                                                        gint        y,
                                                        gint        button,
                                                        gpointer    user_data)
{
    RsttoMainWindow *window = RSTTO_MAIN_WINDOW (user_data);

    gtk_menu_popup (window->priv->image_list_toolbar_menu,
                    NULL,
                    NULL,
                    NULL,
                    NULL,
                    3,
                    gtk_get_current_event_time ());
}
*/

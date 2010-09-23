bin_PROGRAMS = ristretto

ristretto_SOURCES = \
	image_list.c image_list.h \
	image.c image.h \
	image_cache.c image_cache.h \
	picture_viewer.c picture_viewer.h \
	settings.c settings.h \
	preferences_dialog.h preferences_dialog.c \
	main_window_ui.h \
	main_window.c main_window.h \
	wallpaper_manager.c wallpaper_manager.h \
	xfce_wallpaper_manager.c xfce_wallpaper_manager.h \
	gnome_wallpaper_manager.c gnome_wallpaper_manager.h \
	meego_wallpaper_manager.c meego_wallpaper_manager.h \
	app_menu_item.c app_menu_item.h \
	thumbnail_bar.c thumbnail_bar.h \
	thumbnail.c thumbnail.h \
	thumbnailer.c thumbnailer.h \
	main.c

ristretto_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(GIO_CFLAGS) \
	$(LIBEXIF_CFLAGS) \
	$(XFCONF_CFLAGS) \
	$(CAIRO_CFLAGS) \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(LIBXFCEGUI4_CFLAGS) \
	-DDATADIR=\"$(datadir)\" \
	-DSRCDIR=\"$(top_srcdir)\" \
	-DLOCALEDIR=\"$(localedir)\"
 
ristretto_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS) \
	$(GTHREAD_LIBS) \
	$(GIO_LIBS) \
	$(LIBEXIF_LIBS) \
	$(CAIRO_LIBS) \
	$(XFCONF_LIBS) \
	$(LIBXFCE4UTIL_LIBS) \
	$(LIBXFCEGUI4_LIBS) -lX11

INCLUDES = \
	-I${top_srcdir}

if MAINTAINER_MODE

BUILT_SOURCES = \
	main_window_ui.h

main_window_ui.h: main_window_ui.xml
	exo-csource --strip-comments --strip-content --static --name=main_window_ui $< > $@

endif

EXTRA_DIST = \
	main_window_ui.xml

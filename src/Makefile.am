bin_PROGRAMS = ristretto

ristretto_SOURCES = \
	navigator.c navigator.h \
	image.c image.h \
	image_cache.c image_cache.h \
	image_transformation.c image_transformation.h \
	image_transform_orientation.c image_transform_orientation.h \
	picture_viewer.c picture_viewer.h \
	settings.c settings.h \
	preferences_dialog.h preferences_dialog.c \
	main_window_ui.h \
	main_window.c main_window.h \
	main.c
#	save_dialog.h save_dialog.c
#	thumbnail.c thumbnail.h \
#	thumbnail_bar.c thumbnail_bar.h

ristretto_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GTHREAD_CFLAGS) \
	$(GIO_CFLAGS) \
	$(LIBEXIF_CFLAGS) \
	$(XFCONF_CFLAGS) \
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
	$(XFCONF_LIBS) \
	$(LIBXFCE4UTIL_LIBS) \
	$(LIBXFCEGUI4_LIBS)

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

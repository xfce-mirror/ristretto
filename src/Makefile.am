bin_PROGRAMS = ristretto

ristretto_SOURCES = \
	main.c \
    main_window.c main_window.h \
	picture_viewer.c picture_viewer.h \
	thumbnail_viewer.c thumbnail_viewer.h \
	navigator.c navigator.h

ristretto_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(LIBEXIF_CFLAGS) \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(THUNAR_VFS_CFLAGS) \
	-DDATADIR=\"$(datadir)\" \
	-DSRCDIR=\"$(top_srcdir)\" \
	-DLOCALEDIR=\"$(localedir)\"
 
ristretto_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS) \
	$(LIBEXIF_LIBS) \
	$(LIBXFCE4UTIL_LIBS) \
	$(THUNAR_VFS_LIBS)

INCLUDES = \
	-I${top_srcdir}

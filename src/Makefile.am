bin_PROGRAMS = ristretto

ristretto_SOURCES = \
	main.c \
    main_window.c main_window.h \
	navigator.c navigator.h \
	picture_viewer.c picture_viewer.h \
	thumbnail.c thumbnail.h \
	thumbnail_bar.c thumbnail_bar.h

ristretto_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(DBUS_GLIB_CFLAGS) \
	$(LIBEXIF_CFLAGS) \
	$(LIBXFCEGUI4_CFLAGS) \
	$(THUNAR_VFS_CFLAGS) \
	-DDATADIR=\"$(datadir)\" \
	-DSRCDIR=\"$(top_srcdir)\" \
	-DLOCALEDIR=\"$(localedir)\"
 
ristretto_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS) \
	$(DBUS_GLIB_LIBS) \
	$(LIBEXIF_LIBS) \
	$(LIBXFCEGUI4_LIBS) \
	$(THUNAR_VFS_LIBS)

INCLUDES = \
	-I${top_srcdir}

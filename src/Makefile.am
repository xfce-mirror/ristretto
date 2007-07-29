bin_PROGRAMS = ristretto

ristretto_SOURCES = \
	main.c \
	picture_viewer.c picture_viewer.h \
	navigator.c navigator.h

ristretto_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(XFCE4UTIL_CFLAGS) \
	$(THUNAR_VFS_CFLAGS) \
	-DDATADIR=\"$(datadir)\" \
	-DSRCDIR=\"$(top_srcdir)\" \
	-DLOCALEDIR=\"$(localedir)\"
 
ristretto_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS) \
	$(XFCE4UTIL_LIBS) \
	$(THUNAR_VFS_LIBS)

INCLUDES = \
	-I${top_srcdir}

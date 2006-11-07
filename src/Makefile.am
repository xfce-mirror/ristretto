bin_PROGRAMS = ristretto

ristretto_SOURCES = \
	main.c

ristretto_CFLAGS = \
	@GTK_CFLAGS@ \
	@GLIB_CFLAGS@ \
	$(XFCE4UTIL_CFLAGS) \
	-DDATADIR=\"$(datadir)\" \
	-DSRCDIR=\"$(top_srcdir)\" \
	-DLOCALEDIR=\"$(localedir)\"
 
ristretto_LDADD = \
	@GTK_LIBS@ \
	@GLIB_LIBS@ \
	$(XFCE4UTIL_LIBS)

INCLUDES = \
	-I${top_srcdir}

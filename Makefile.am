## Process this file with automake to produce Makefile.in

SUBDIRS = src po icons

desktopdir = $(datadir)/applications
desktop_in_files = ristretto.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

EXTRA_DIST = \
	autogen.sh \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	$(desktop_in_files) \
	$(pixmaps_DATA) \
	$(wrapper_DATA)

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update \
	$(desktop_DATA)

## Process this file with automake to produce Makefile.in

SUBDIRS = src po icons

desktopdir = $(datadir)/applications
desktop_in_files = org.xfce.ristretto.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
@INTLTOOL_DESKTOP_RULE@

#
# .appdata
#
@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/metainfo
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files = org.xfce.ristretto.appdata.xml.in

EXTRA_DIST = \
	README.md \
	autogen.sh \
	intltool-extract.in \
	intltool-merge.in \
	intltool-update.in \
	$(desktop_in_files) \
	$(pixmaps_DATA) \
	$(wrapper_DATA) \
	$(appdata_in_files)

DISTCLEANFILES = \
	intltool-extract \
	intltool-merge \
	intltool-update \
	po/.intltool-merge-cache.lock \
	$(desktop_DATA) \
	$(appdata_DATA)

DISTCHECK_CONFIGURE_FLAGS=--enable-gen-doc

.PHONY: ChangeLog

ChangeLog: Makefile
	(GIT_DIR=$(top_srcdir)/.git git log > .changelog.tmp \
	&& mv .changelog.tmp ChangeLog; rm -f .changelog.tmp) \
	|| (touch ChangeLog; echo 'Git directory not found: installing possibly empty changelog.' >&2)

dist-hook: ChangeLog

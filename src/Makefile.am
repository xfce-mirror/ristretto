bin_PROGRAMS = ristretto

ristretto_SOURCES = \
	app_menu_item.c app_menu_item.h \
	file.c file.h \
	gnome_wallpaper_manager.c gnome_wallpaper_manager.h \
	icon_bar.c icon_bar.h \
	image_list.c image_list.h \
	image_viewer.c image_viewer.h \
	main.c \
	main_window.c main_window.h \
	marshal.c marshal.h \
	mime_db.c mime_db.h \
	monitor_chooser.c monitor_chooser.h \
	preferences_dialog.h preferences_dialog.c \
	print.c print.h \
	privacy_dialog.h privacy_dialog.c \
	properties_dialog.h properties_dialog.c \
	ristretto-resources.c \
	settings.c settings.h \
	thumbnailer.c thumbnailer.h \
	tumbler.c tumbler.h \
	util.c util.h \
	wallpaper_manager.c wallpaper_manager.h \
	xfce_wallpaper_manager.c xfce_wallpaper_manager.h

ristretto_CFLAGS = \
	$(GTK_CFLAGS) \
	$(GLIB_CFLAGS) \
	$(GIO_CFLAGS) \
	$(GIO_UNIX_CFLAGS) \
	$(LIBEXIF_CFLAGS) \
	$(XFCONF_CFLAGS) \
	$(EXO_CFLAGS) \
	$(CAIRO_CFLAGS) \
	$(LIBXFCE4UTIL_CFLAGS) \
	$(LIBXFCE4UI_CFLAGS) \
	$(LIBX11_CFLAGS) \
	-DDOCDIR=\"$(DESTDIR)$(docdir)\" \
	-DDATADIR=\"$(datadir)\" \
	-DSRCDIR=\"$(top_srcdir)\" \
	-DLOCALEDIR=\"$(localedir)\"

ristretto_LDFLAGS = \
	$(LIBX11_LDFLAGS)

ristretto_LDADD = \
	$(GTK_LIBS) \
	$(GLIB_LIBS) \
	$(GIO_LIBS) \
	$(GIO_UNIX_LIBS) \
	$(LIBEXIF_LIBS) \
	$(XFCONF_LIBS) \
	$(EXO_LIBS) \
	$(CAIRO_LIBS) \
	$(LIBXFCE4UTIL_LIBS) \
	$(LIBXFCE4UI_LIBS) \
	$(LIBX11_LIBS) \
	$(LIBM) \
	$(MAGIC_LIBS)

AM_CPPFLAGS = \
	-I${top_srcdir}

ristretto_built_sources = \
	marshal.h \
	marshal.c \
	ristretto-resources.c \
	tumbler.c \
	tumbler.h \
	$(NULL)

ristretto_resources_deps = $(shell $(GLIB_COMPILE_RESOURCES) --generate-dependencies --sourcedir $(srcdir) $(srcdir)/ristretto.gresource.xml)

ristretto-resources.c: $(srcdir)/ristretto.gresource.xml $(ristretto_resources_deps) Makefile
	$(AM_V_GEN) $(GLIB_COMPILE_RESOURCES) --target=$@ --sourcedir=$(srcdir) --generate-source $<

marshal.h: stamp-marshal.h
	@true

stamp-marshal.h: $(srcdir)/marshal.list Makefile
	$(AM_V_GEN) ( \
		cd $(builddir) \
		&& echo "#ifndef __RSTTO_MARSHAL_H__" > xgen-tmh \
		&& echo "#define __RSTTO_MARSHAL_H__" >> xgen-tmh \
		&& ( glib-genmarshal \
			--prefix=_rstto_marshal \
			--header $(srcdir)/marshal.list ) >> xgen-tmh \
		&& echo "#endif /* !__RSTTO_MARSHAL_H__ */" >> xgen-tmh \
		&& ( cmp -s xgen-tmh marshal.h || cp xgen-tmh marshal.h ) \
		&& rm -f xgen-tmh \
		&& echo timestamp > $(@F) \
	)

marshal.c: $(srcdir)/marshal.list Makefile
	$(AM_V_GEN) ( \
		cd $(builddir) \
		&& echo "#include \"marshal.h\"" > xgen-tmc \
		&& ( glib-genmarshal \
			--prefix=_rstto_marshal \
			--body $(srcdir)/marshal.list ) >> xgen-tmc \
		&& cp xgen-tmc marshal.c \
		&& rm -f xgen-tmc \
	)

BUILT_SOURCES = \
	$(ristretto_built_sources)

tumbler-service-dbus.xml: tumbler-service-dbus.xml.in
	$(SED) -e "s,\@TUMBLER_SERVICE_NAME_PREFIX\@,$(TUMBLER_SERVICE_NAME_PREFIX),g" \
	       -e "s,\@TUMBLER_SERVICE_PATH_PREFIX\@,$(TUMBLER_SERVICE_PATH_PREFIX),g" < $< > $@

tumbler.c tumbler.h : tumbler-service-dbus.xml Makefile
	gdbus-codegen \
		--c-namespace=Tumbler \
		--interface-prefix=@TUMBLER_SERVICE_NAME_PREFIX@. \
		--generate-c-code=tumbler \
		tumbler-service-dbus.xml

DISTCLEANFILES = \
	$(ristretto_built_sources) \
	stamp-marshal.h \
	tumbler-service-dbus.xml

EXTRA_DIST = \
	marshal.list \
	main_window_ui.xml \
	tumbler-service-dbus.xml.in \
	meson.build \
	ristretto.gresource.xml \
	$(NULL)

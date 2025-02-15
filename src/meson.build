ristretto_sources = [
  'app_menu_item.c',
  'app_menu_item.h',
  'file.c',
  'file.h',
  'gnome_wallpaper_manager.c',
  'gnome_wallpaper_manager.h',
  'icon_bar.c',
  'icon_bar.h',
  'image_list.c',
  'image_list.h',
  'image_viewer.c',
  'image_viewer.h',
  'main.c',
  'main_window.c',
  'main_window.h',
  'mime_db.c',
  'mime_db.h',
  'monitor_chooser.c',
  'monitor_chooser.h',
  'preferences_dialog.c',
  'preferences_dialog.h',
  'print.c',
  'print.h',
  'privacy_dialog.c',
  'privacy_dialog.h',
  'properties_dialog.c',
  'properties_dialog.h',
  'settings.c',
  'settings.h',
  'thumbnailer.c',
  'thumbnailer.h',
  'util.c',
  'util.h',
  'wallpaper_manager.c',
  'wallpaper_manager.h',
  'xfce_wallpaper_manager.c',
  'xfce_wallpaper_manager.h',
]

service_file = configure_file(
  configuration: configuration_data({
    'TUMBLER_SERVICE_NAME_PREFIX': tumbler_service_name_prefix,
    'TUMBLER_SERVICE_PATH_PREFIX': tumbler_service_path_prefix,
  }),
  input: 'tumbler-service-dbus.xml.in',
  output: 'tumbler-service-dbus.xml',
  install: false,
)

ristretto_sources += gnome.gdbus_codegen(
  'tumbler',
  sources: service_file,
  interface_prefix: tumbler_service_name_prefix,
  namespace: 'Tumbler',
  install_header: false,
)

ristretto_sources += gnome.genmarshal(
  'marshal',
  sources: 'marshal.list',
  prefix: '_rstto_marshal',
  internal: true,
  install_header: false,
)

ristretto_sources += custom_target(
  'main_window_ui.h',
  input: 'main_window_ui.xml',
  output: 'main_window_ui.h',
  command: [xdt_csource, '--static', '--strip-comments', '--strip-content', '--name=main_window_ui', '--output=@OUTPUT@', '@INPUT@'],
)

executable(
  'ristretto',
  ristretto_sources,
  sources: xfce_revision_h,
  c_args: [
    '-DG_LOG_DOMAIN="@0@"'.format('ristretto'),
    '-DTUMBLER_SERVICE_NAME_PREFIX="@0@"'.format(tumbler_service_name_prefix),
    '-DTUMBLER_SERVICE_PATH_PREFIX="@0@"'.format(tumbler_service_path_prefix),
  ],
  include_directories: [
    include_directories('..'),
  ],
  dependencies: [
    glib,
    gio,
    gio_unix,
    gtk,
    cairo,
    libexif,
    exo,
    libxfce4ui,
    libxfce4util,
    xfconf,
    libx11,
    libmagic,
    libm,
  ],
  install: true,
  install_dir: get_option('prefix') / get_option('bindir'),
)

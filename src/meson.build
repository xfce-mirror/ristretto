ristretto_sources = [
  'app_menu_item.c',
  'app_menu_item.h',
  'file.c',
  'file.h',
  'file_manager_integration.h',
  'file_manager_integration.c',
  'file_manager_integration_thunar.h',
  'file_manager_integration_thunar.c',
  'gnome_wallpaper_manager.c',
  'gnome_wallpaper_manager.h',
  'icon_bar.c',
  'icon_bar.h',
  'image_cache.c',
  'image_cache.h',
  'image_render.c',
  'image_render.h',
  'image_probe.c',
  'image_probe.h',
  'image_list.c',
  'image_list.h',
  'image_viewer.c',
  'image_viewer.h',
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
  'profile.h',
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

ristretto_sources += gnome.compile_resources(
  'ristretto-resources',
  'ristretto.gresource.xml',
)

ristretto_dependencies = [
  glib,
  gio,
  gio_unix,
  gtk,
  cairo,
  glycin,
  libexif,
  exo,
  libxfce4ui,
  libxfce4util,
  xfconf,
  libxfce4windowing,
  libx11,
  libmagic,
  libm,
]

executable(
  'ristretto',
  ristretto_sources + ['main.c'],
  sources: xfce_revision_h,
  c_args: [
    '-DG_LOG_DOMAIN="@0@"'.format('ristretto'),
    '-DTUMBLER_SERVICE_NAME_PREFIX="@0@"'.format(tumbler_service_name_prefix),
    '-DTUMBLER_SERVICE_PATH_PREFIX="@0@"'.format(tumbler_service_path_prefix),
  ],
  include_directories: [
    include_directories('..'),
  ],
  dependencies: ristretto_dependencies,
  install: true,
  install_dir: get_option('prefix') / get_option('bindir'),
)

viewer_test = executable(
  'test-image-viewer',
  ristretto_sources + ['../tests/test-image-viewer.c'],
  sources: xfce_revision_h,
  c_args: [
    '-DRSTTO_VIEWER_TEST',
    '-DTUMBLER_SERVICE_NAME_PREFIX="@0@"'.format(tumbler_service_name_prefix),
    '-DTUMBLER_SERVICE_PATH_PREFIX="@0@"'.format(tumbler_service_path_prefix),
  ],
  include_directories: [include_directories('..')],
  dependencies: ristretto_dependencies,
)
test('image-viewer', viewer_test, timeout: 60)

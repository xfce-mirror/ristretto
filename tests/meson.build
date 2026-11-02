cache_test = executable(
  'test-image-cache',
  'test-image-cache.c',
  dependencies: [glib, gio, gio_unix, gtk, cairo],
)
test('image-cache', cache_test, timeout: 60)

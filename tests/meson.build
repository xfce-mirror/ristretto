cache_test = executable(
  'test-image-cache',
  ['test-image-cache.c', '../src/image_probe.c', '../src/image_render.c'],
  dependencies: [glib, gio, gio_unix, gtk, cairo, glycin, libm],
)
test('image-cache', cache_test, timeout: 60)

probe_test = executable(
  'test-image-probe',
  'test-image-probe.c',
  dependencies: [glib, gio, glycin],
)
test('image-probe', probe_test, timeout: 30)

render_test = executable(
  'test-image-render',
  ['test-image-render.c', '../src/image_render.c'],
  dependencies: [glib, gio, gtk, cairo, libm],
)
test('image-render', render_test, timeout: 30)

# Exercise the portable conversion path even on hosts with SSE2.
render_scalar_test = executable(
  'test-image-render-scalar',
  ['test-image-render.c', '../src/image_render.c'],
  c_args: ['-DRSTTO_RENDER_SCALAR'],
  dependencies: [glib, gio, gtk, cairo, libm],
)
test('image-render-scalar', render_scalar_test, timeout: 30)

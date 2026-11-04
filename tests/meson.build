cache_test = executable(
  'test-image-cache',
  ['test-image-cache.c', '../src/image_probe.c', '../src/image_render.c', '../src/image_gpu.c', '../src/image_shader_cache.c'],
  c_args: test_c_args,
  dependencies: [glib, gio, gio_unix, gtk, cairo, glycin, placebo, epoxy, simde, libm],
)
test('image-cache', cache_test, timeout: 60)

probe_test = executable(
  'test-image-probe',
  'test-image-probe.c',
  c_args: test_c_args,
  dependencies: [glib, gio, glycin],
)
test('image-probe', probe_test, timeout: 30)

render_test = executable(
  'test-image-render',
  ['test-image-render.c', '../src/image_render.c'],
  c_args: test_c_args,
  dependencies: [glib, gio, gtk, cairo, simde, libm],
)
test('image-render', render_test, timeout: 30)

# Exercise Ristretto's C fallback independently of SIMDe and native SIMD.
render_scalar_test = executable(
  'test-image-render-scalar',
  ['test-image-render.c', '../src/image_render.c'],
  c_args: test_c_args + ['-DRSTTO_RENDER_SCALAR'],
  dependencies: [glib, gio, gtk, cairo, libm],
)
test('image-render-scalar', render_scalar_test, timeout: 30)

if simde.found()
  # Exercise SIMDe's portable implementation without native intrinsics.
  render_portable_test = executable('test-image-render-portable',
    ['test-image-render.c', '../src/image_render.c'],
    c_args: test_c_args + ['-DSIMDE_NO_NATIVE'],
    dependencies: [glib, gio, gtk, cairo, simde, libm])
  test('image-render-portable', render_portable_test, timeout: 30)
endif

executable('benchmark-thumbnailer', 'benchmark-thumbnailer.c', dependencies: [glib, gio])

if placebo.found() and epoxy.found()
  gpu_test = executable('test-image-gpu',
    ['test-image-gpu.c', '../src/image_gpu.c', '../src/image_shader_cache.c', '../src/image_render.c'],
    c_args: test_c_args + ['-DRSTTO_GPU_TEST'],
    dependencies: [glib, gio, gtk, cairo, placebo, epoxy, simde, libm])
  test('image-gpu', gpu_test, timeout: 90)
  test('shader-process', find_program('python3'),
    args: [files('test-shader-process.py'), gpu_test], timeout: 60)
  gpu_cache_test = executable('test-image-cache-gpu',
    ['test-image-cache.c', '../src/image_probe.c', '../src/image_gpu.c', '../src/image_shader_cache.c', '../src/image_render.c'],
    c_args: test_c_args + ['-DRSTTO_GPU_TEST', '-DRSTTO_CACHE_GPU'],
    dependencies: [glib, gio, gio_unix, gtk, cairo, glycin, placebo, epoxy, simde, libm])
  test('image-cache-gpu', gpu_cache_test, timeout: 90)
endif

shader_cache_test = executable('test-shader-cache',
  ['test-shader-cache.c', '../src/image_shader_cache.c'],
  c_args: test_c_args,
  dependencies: [glib, gio])
test('shader-cache', shader_cache_test, timeout: 30)

preferences_test = executable('test-preferences',
  ['test-preferences.c', '../src/preferences_dialog.c', '../src/settings.c',
   '../src/file_manager_integration.c', '../src/file_manager_integration_thunar.c',
   '../src/image_probe.c'],
  sources: xfce_revision_h,
  c_args: test_c_args,
  include_directories: [include_directories('..', '../src')],
  dependencies: [glib, gio, gtk, glycin, libxfce4ui, libxfce4util, xfconf])
test('preferences', preferences_test, timeout: 30, env: ['RISTRETTO_RENDERER='])
foreach renderer : ['cpu', 'placebo', 'invalid']
  test('preferences-' + renderer + '-override', preferences_test,
    args: ['-p', '/preferences/renderer'],
    env: ['RISTRETTO_RENDERER=' + renderer], timeout: 30)
endforeach

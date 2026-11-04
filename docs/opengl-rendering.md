# Experimental libplacebo OpenGL renderer

The optional backend requires system libplacebo >= 7.360.0 and libepoxy.
CPU rendering remains the compiled default. Choose **CPU** or **GPU (libplacebo)**
under **Edit → Preferences → Advanced → Rendering**. The choice is stored as
the boolean `/image/gpu-enabled` in the `ristretto` Xfconf channel; its default is
`false`. **Restart Ristretto for a renderer change to take effect.** Existing
processes keep their startup choice, including newly opened viewer windows.

The GPU choice is unavailable in builds without libplacebo or safe Glycin probing.
Unsupported images and GPU failures still fall back to CPU preparation, and all
existing memory, slot and task limits apply.

To select GPU rendering from the command line for subsequent launches:

```sh
xfconf-query -c ristretto -p /image/gpu-enabled --create --type bool --set true
```

Use `false` to select CPU rendering. The `run-placebo` and `run-placebo-profile`
local launchers respect this saved choice and retain their one-GPU-worker default.

```sh
meson setup build-gpu --buildtype=debugoptimized -Dplacebo=enabled -Dglycin-probe=enabled -Dprofiling=true
meson compile -C build-gpu
RISTRETTO_RENDERER=placebo RISTRETTO_GL_WORKERS=2 build-gpu/src/ristretto picture.jpg
```

`RISTRETTO_RENDERER=cpu|placebo` overrides the saved choice for that process;
Preferences displays a note when this override is present. An unset, empty or
unrecognized value uses the saved choice. The override never changes that saved
value. `RISTRETTO_GL_WORKERS`
accepts 1 through 5, defaulting to 2. `-Dplacebo=disabled` builds without either
GPU dependency. Missing Glycin still disables safe caching/preloading, as before.

## Rendering and lifetime

Decoded Cairo surfaces remain canonical. Printing exports these surfaces and
animations keep their existing path. A worker processes one whole image in an
independent shared GdkGLContext, with private libplacebo dispatch, textures and
filter state. Contexts are created and realized on GTK's thread. There are no GPU
stripes. One submitted image occupies one worker and one shared cache task permit
until its fence signals and temporary resources are released. The existing task
ceiling, image slots, speculative capacity and demand priority still apply.
GPU preparation operates on admitted cached still-image sources. Uncached
foreground exceptions, including the existing unsafe-RAW-probe exclusion, retain
CPU presentation; this backend does not bypass safe-probing or admission rules.

Before upload, the backend checks source, output and intermediate dimensions
against the context's maximum 2D texture size, format support, checked byte
sizes and finite transform ranges. Unsupported requests use CPU preparation.
There is no tiling or partial-source upload workaround. Failed GPU variants are
remembered until geometry, source or capabilities change. Context failures disable
that renderer. Submitted obsolete work drains without publishing stale results.

The source is uploaded without an application pixel-copy buffer. Resampling uses
a fixed runtime-loop shader through libplacebo custom-shader and dispatch APIs into an explicit unrotated viewport texture;
a second nearest-sampled pass applies exact EXIF orientation/cropping. Filtering
only covers the visible image rectangle. Smoothing off uses nearest; aligned 1:1
uses direct sampling; enlargement uses EWA LanczosSharp; reduction uses EWA
Lanczos with antialiasing widening. EWA antiringing is 0.8. Scrolling never lowers
quality. Encoded RGB and premultiplied alpha match the existing color pipeline;
this experiment does not add linear-light scaling, HDR or color management.

The EWA weights come from the installed libplacebo filter generator, using its
256-entry LUT, 1e-3 cutoff, antialias widening and weighted antiring envelope.
Radius, loop bounds and geometry are dynamic uniforms. Nearest/direct sampling
shares the program with EWA through a uniform branch. Filter implementation
version 2 is part of derivative identity. Each context keeps its dispatch cache;
zoom and viewport changes do not generate radius-specific programs.

After GTK realizes the contexts, the cache scheduler warms one worker at a time
with tiny synthetic uploads and actual nearest/up/down dispatches. Three program
signatures cover the full/clipped sampling and orientation passes. Completion is
fence-driven, with no readback or GTK wait. The primary worker warms first;
additional workers use spare permits while leaving room for foreground work.
A worker accepts image requests only after successful initialization. Warmup owns
one shared task permit and a 64 KiB host reservation, but no image/cache slot.
Unavailable admission defers it, cancellation drains before release, and a failed
initialization selects CPU preparation. No compiler threads are added. The application binary cache described below is
separate from any cache maintained by the GPU driver.

The viewer's desired view is separate from each immutable render snapshot.
Successive zoom input uses the latest desired scale; verification rechecks file
identity and current geometry before delivery. Revisions cover zoom, pan,
orientation, viewport, monitor scale and smoothing. A reversal to identical
geometry may reuse the same pixels with the latest revision. The last completed
texture remains visible during preparation; it is not scaled synchronously by
Cairo. GPU failures and unsupported sizes use admitted CPU preparation and keep
the last completed CPU derivative during subsequent view changes. Uncached
foreground exceptions and animations retain their existing CPU path.

One viewport-sized GPU result is cached per image. Backend, filter and context
generation are part of variant identity. Compatible hits reuse it without
decoding, uploading or scaling. Current/previous history protection applies to
both source and derivative. Presentation calls `gdk_cairo_draw_from_gl()` on the
existing widget, then fences the GTK paint context. Texture deletion runs in its
owning worker after that fence. Worker fences are polled without blocking GTK.
Outstanding jobs/results keep their contexts alive after widget destruction.
If old presentation fences stall, that worker holds its next image's permit and
staging reservation without uploading until its retired textures drain. This
prevents a stalled consumer from accumulating a stream of replacement textures.

## Accounting

CPU sources, CPU fallback results and explicit host storage remain charged to
the cache. On an upload miss, a GPU request reserves source stride times height
plus 64 KiB as conservative host staging. A pinned source hit reserves only
64 KiB of host scratch. A hit cannot become an upload without readmission.
The configured 2 GiB RAM total, 256 MiB per-image limit, six image slots,
four speculative slots and shared five-task ceiling are unchanged.

Application-owned GPU allocations have a separate 1 GiB ceiling
(`RSTTO_GPU_MAX_BYTES`). Admission reserves the full upload, intermediate and
output texture footprint before submission, including queued work. Retained
viewport results, resident sources, resources awaiting cleanup and the 1 KiB
worker LUTs count toward this ceiling. Failed admission changes nothing and
uses admitted asynchronous CPU preparation; it does not permanently reject a
GPU-compatible variant. No protected history is evicted to fund GPU work.

The currently selected source may remain resident, up to 256 MiB
(`RSTTO_GPU_MAX_SOURCE_BYTES`). At most two resident source allocations exist:
the current source and its retiring predecessor. Further uploads can run within
the total budget, but cannot become a third resident source. A 7728 by 5152 RGBA8
source occupies 151.88 MiB; a 5003 by 2664 output occupies 50.84 MiB. GPU-only
bytes are outside the RAM budget. Driver allocations, shader programs and GTK
backing surfaces remain additional overhead, not a hard total-device cap.

Source identity is an immutable decoded-entry serial scoped to the renderer's
context generation, not a CPU address or a pathname. Freshness verification
still runs before cache delivery. Invalidation, replacement, quality changes,
cache removal, low-memory cleanup and forced limit reductions retire affected
sources. Leases pin textures until all submitted work and fences finish; bytes
remain charged until their owning worker actually deletes them. Destruction
does not introduce a renderer/source reference cycle.

Pan, zoom, orientation, smoothing and viewport changes reuse the source on its
owning worker. Other workers can prepare different images. The owner retains
foreground priority, with no duplicate source upload onto an idle worker.
Sources become resident only after successful rendering for the latest selected
image; obsolete or cancelled uploads cannot populate residency. Selecting an
already-prepared viewport does not proactively upload a full source.

Before admission and again before submission, source, output and intermediate
dimensions must fit the current context's maximum 2D texture size. Rotation
only swaps the intermediate's checked axes. Format, stride, context generation
and checked size arithmetic are also validated. Oversized sources or outputs
use CPU preparation, with zero image uploads or GPU scaling submissions; there
is no tiling or cropped-source-upload workaround. Existing compatible viewport
hits continue to avoid decoding, uploading and scaling entirely.

Profiling adds `gpu-source` transitions and hit/miss, source-byte, source-count
and reserved-byte counters. GPU timestamp intervals on a source hit exclude
full-image upload; small filter-LUT/uniform updates remain. Accounting is not a
total-process cap; see [image-cache.md](image-cache.md).

## Tests and repeatable comparison

GPU tests require an actual GL display. Use `dbus-run-session -- meson test -C
build-gpu --print-errorlogs` with `RISTRETTO_RENDERER=placebo` to exercise GPU viewer
integration. Unit tests cover format/size rejection, injected allocation failures,
all orientations, scaling geometry, premultiplied transparency, CPU EWA reference
output, delayed completion, destruction, invalidation, promotion, cache reuse and
live limits. The normal CPU suites remain registered. ASan/UBSan builds use the
existing Fontconfig leak suppression; driver code is not instrumented.

`tests/benchmark-gpu.py` compares the committed CPU baseline and this branch's CPU
mode with GPU worker counts 1 through 5. Build the same `test-image-viewer.c`
harness against both source trees, leaving baseline application sources unchanged.
Each scenario runs in a fresh process on a private D-Bus session, with a no-op
thumbnail service so unrelated thumbnail decoding cannot compete for resources.
The helper requires `RISTRETTO_BENCH_PRIVATE_BUS=1` and is not installed.
Benchmark windows do not take keyboard focus and reject external input; navigation
and zoom are driven programmatically. Each scenario verifies its initial selection.

```sh
python3 tests/benchmark-gpu.py \
  --baseline ../baseline/build/src/test-image-viewer \
  --experiment build-gpu/src/test-image-viewer \
  --output ../comparison --fixtures ../fixtures
```

For a fresh fixture directory, first run the same command with a different output
directory and `--repetitions 1 --only gpu2`. Keep that untimed warmup log: it creates
all fixtures and records first-use shader preparation. Then run the full matrix
with the reused fixtures and the naturally warmed driver shader cache. Do not
interpret the main matrix as a measurement of an empty driver shader cache.
`python3 tests/analyze-gpu.py ../comparison` emits per-run statistics, a table and
the lowest worker count within 5% of the best median scrolling p95. Percentiles
use nearest rank; no successful slow samples are removed.

The fixed schedule interleaves five repetitions of every configuration (seed
20261103). Reused 7728 by 5152 JPEG fixtures and a 5003 by 2664 viewport are
identical. Scenarios are cold-image-cache navigation, warmed two-image alternation,
48 selections at four per second with reversal, and twelve zoom changes. Cold and
zoom cases disable speculation to isolate foreground preparation; warm and scroll
use six slots, five targets/workers, 2 GiB and 256 MiB. Cold does not mean cold OS
page cache. Binary/fixture hashes, failures and all logs are retained. Parent and
Glycin descendant RSS and DRM fdinfo memory are sampled every 50 ms, separately
from application accounting. Short-lived helpers can be missed by sampling.

Profiling spans separate conversion, preparation, upload, submission, GPU fence
completion and paint. `gpu-submit new_programs` marks first-use shader variants;
driver disk caches can still make their compilation warm. GPU timestamp intervals
can include scheduling, work from other contexts and idle time while compiling;
they are not exclusive kernel timings. `gpu-prepare` includes queue/callback delay.
`gpu-paint` measures GTK submission, and frame-clock presentation is not a physical
scanout measurement. Compare maximum-quality GPU filters with Cairo GOOD as
different quality policies, not identical kernels.

`tests/gl-readback-audit.c` can be built as an LD_PRELOAD audit library with GTK,
epoxy and libdl. It counts `glReadPixels` during `gdk_cairo_draw_from_gl()` calls.
`GDK_GL=software-draw-gl` is its positive control; ordinary hardware presentation
must show zero readbacks. Tests alone download textures to compare pixels.

## Rendering-fix comparison

`--render-fix` compares an exact saved pre-fix experimental build with the new
build, both using one GPU worker. Copy only the benchmark harness into the saved
source tree; leave its application sources unchanged. This adds 48 zoom inputs
at 166 ms intervals from 12% to about 118%, reversing after 24, to the original four scenarios. Every
repetition has a fresh isolated Mesa shader-cache directory: the first (`cold`)
scenario measures cold-driver startup; subsequent fresh processes reuse that
cache. The warmed first-image measurement is reported separately from the cold
one, including differences in speculative scheduling. The image fixtures and OS
page cache are reused. No installed application settings are modified.

```sh
python3 tests/benchmark-gpu.py --render-fix --repetitions 5 \
  --baseline ../render-fix-baseline-src/build/src/test-image-viewer \
  --experiment build-gpu/src/test-image-viewer \
  --fixtures ../placebo-fixtures --output ../render-fix-comparison \
  --audit build-gpu/gl-readback-audit.so
```

`gpu-warmup-*` events separate startup work from image uploads and dispatches.
`gpu-program` records each new signature once; `gpu-submit new_programs=0` after
warmup is the shader-reuse assertion. `view-request`, `view-committed` and
`render-obsolete` identify requested revisions, accepted geometry and discarded
stale results. The profiler does not log per-pixel work.

## Bounded persistent shader programs

The placebo backend defaults to an application program cache in
`$XDG_CACHE_HOME/ristretto/shaders/`, normally `~/.cache/ristretto/shaders/`.
This follows the [XDG cache convention](https://specifications.freedesktop.org/basedir/latest/).
An unavailable directory is a cache miss; no system directory is used.
`RISTRETTO_SHADER_CACHE=0` disables persistence for diagnosis.

A SHA-256 namespace covers the fixed shader bodies, filter implementation,
runtime libplacebo version, GL vendor/renderer/version, GLSL version, context
profile and program-binary formats. Each libplacebo object key names a file in
that namespace. The versioned envelope holds the key, length and SHA-256 payload
checksum. Loading checks a regular owned file, bounded size, header and checksum
before allocating the payload, and validates the copied bytes again. The generic
libplacebo deserializer is deliberately not used. Invalid files are misses;
libplacebo checks the GL link status and recompiles rejected binaries.

Objects are limited to 2 MiB, libplacebo host-cache payloads to 8 MiB, and disk
objects across namespaces to 32 MiB / 128 entries. Writers take a nonblocking
interprocess lock, prune oldest successfully used entries, and atomically rename
unique private temporary files. Contention skips a write. New directories are
0700 and files 0600. Crash-leftover temporary files are cleaned under the lock.

All disk I/O happens in the existing warmup worker. The scheduler reserves
10 MiB plus its existing 64 KiB allowance before attaching `pl_cache`, covering
8 MiB payloads and one bounded transient object. It detaches and destroys the
host cache after warmup/fence cleanup; GPU programs remain in the worker's
dispatch cache. Cancellation keeps the task permit and reservation until cleanup.
Insufficient RAM uses ordinary nonpersistent warmup without evicting history.
Driver/compiler-private allocations remain estimated overhead.

`shader-cache` tests the envelope, corruption, hostile lengths, cancellation,
permissions, concurrent writers and pruning. `shader-process` disables Mesa's
disk cache and runs fresh cold/warm/rejected/warm processes with test-only
`glCompileShader` / `glProgramBinary` hooks. Upscale and downscale use distinct
dispatch signatures but share one fixed EWA program binary; normal rendering
keeps the three warmed dispatch signatures and needs no new zoom-specific programs.

For the scrolling recovery comparison, add `--render-fix --zoom-baseline PATH`
to the benchmark command. `--baseline` is the preserved pre-regression build;
`--zoom-baseline` is the preserved zoom-fixed build. Five seeded, interleaved
repetitions retain all runs. Scenarios cover cold navigation, warmed alternation,
four selections/second with reversals, individual zooms and sustained zoom.
Application cache state and Mesa cache state live in separate per-run directories.

### Continuous full-quality dragging

Pan input updates desired scroll positions immediately. One admitted foreground
view render runs to completion while mouse motion replaces a single pending
view snapshot. A completed frame of the same verified source may be presented
when only horizontal/vertical pan differs; its actual key and revision remain
attached to the frame. Zoom, orientation, viewport, device scale, smoothing,
backend and context changes still require exact compatibility. Ordinary cache
hits always require exact keys. Presenting a pan snapshot never restores old
scrollbar or file-view state, and presentation schedules the latest pending view
without waiting for another mouse event or button release.

The last complete texture remains visible between updates. No translated
preview, blank edge strips, reduced-quality sampling or extra image buffers are
used. The same rule applies to admitted asynchronous CPU fallback renders;
direct CPU viewing and Ctrl+LMB box zoom retain their existing behavior. Pending
navigation takes precedence over further renders of the old image. Existing
RAM reservations, slot/task limits, GPU retirement and preload deferral apply
until the latest view is ready.

If GPU presentation fails or a texture exceeds the GPU limits, CPU preparation
still requires normal admission. If that preparation is refused or fails, the
viewer accepts the canonical source without the obsolete output and draws the
latest view directly through full-quality Cairo. This last resort adds no
retained output buffer, but may block GTK during an expensive redraw. Unchanged
redraws do not keep retrying; later view/settings changes or Refresh can retry.
See [CPU fallback accounting](image-cache.md) for ownership and memory details.

`tests/benchmark-gpu.py --render-fix --drag ...` adds an eight-second held-LMB
sequence at 60 Hz, including diagonal movement and reversals. It records complete
presentations, frame gaps, revision ordering, decoding count and final settling
with the mouse button still held. The deterministic viewer tests hold rendering
or identity verification while sending newer input, and also exercise fallback,
all orientations, transparency, incompatible view changes and navigation.

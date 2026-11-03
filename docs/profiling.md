# Profiling image navigation

Build an optimized binary with debug information, frame pointers (where the
compiler supports them), and optional wall-clock timing spans:

```sh
meson setup build-profile --buildtype=debugoptimized -Dprofiling=true -Dglycin-probe=enabled
meson compile -C build-profile
RISTRETTO_PROFILE=1 build-profile/src/ristretto /path/to/first.jpg /path/to/second.jpg 2> navigation.log
```

The ordinary build has profiling disabled. The profiling binary emits timings
only when `RISTRETTO_PROFILE=1` is set at startup. No new dependency is needed.
Profiling does not change cache limits, worker limits, scheduling or decoding.
Use a regular file for stderr rather than a slow terminal or pipe. Traces add
clock/formatting/I/O overhead and should be compared with tracing disabled when
measuring very short operations. There is no logging in pixel or input loops.
The application trace includes object/thread identifiers, dimensions and cache
statistics, but no image paths or contents. Other libraries can still print
messages to stderr.

Each `RSTTO_PROFILE` line contains a monotonic `start_us`, `duration_us`, thread,
object, request `id` and event. Durations overlap; do not add nested spans.
Viewer IDs are per viewer; cache IDs are per cache. Worker spans use a job
pointer with id zero; pointers may be reused after a job ends.

- `navigation-handler`: the requested-position handler and thumbnail selection.
- `image-commit-prepare`, `image-commit-handler`: title, menus and metadata/status
  updates for the image actually committed to the viewing pane.
- `cache-request`, `cache-hit`: cache events (near-zero duration).
- `navigation-coalesced`: input replaced the single pending target while an active frame finishes.
- `render-prepare`: worker resampling into an accounted viewport surface.
- `render-hit`: reuse of a compatible retained derivative.
- `frame-presented`: the committed frame crossed GTK's after-paint boundary,
  or the unmapped viewer advanced without a paint. This is not a compositor timestamp.
- `worker-queue`: task dispatch delay, before execution starts.
- `worker`: a worker phase including cleanup. Phases: 0 identity, 1 prefix
  reading/RAW exclusion, 3 identity revalidation, 4 decoding, 5 uncached reopen
  and decoding, 10 post-render identity verification. Derivative rendering is reported as one `render-prepare` batch. `demand=0` means a speculative job at span completion.
- `glycin-metadata`: asynchronous dimension initialization; no frame requested.
- `pixel-conversion`: conversion to the retained Cairo surface, nested inside
  the decode worker span.
- `request-to-ready`: request to cache delivery, with cumulative hits/decodes
  and current resident/reserved bytes, entries and active jobs.
- `viewer-setup`: delivery to the viewer, orientation, geometry and ready signal.
- `cairo-paint`: GTK-thread painting of the image itself. `prepared=1` composites
  a viewport derivative; `prepared=0` uses the original source/resampling fallback.
- `draw`: the complete viewer draw callback, including background/overlays.
- `ready-to-draw`, `request-to-draw`: delivery/request to the first completed
  viewer draw of that result. Draws of an old image during loading do not end
  this span. Superseded requests have no completed draw span.

Draw completion measures application work. It is **not** the compositor's
presentation timestamp or physical screen latency. It also excludes input
queued before the application processes navigation. For cached transitions,
compare hit and decode counters, request-to-ready, navigation-handler and
cairo-paint/draw. A decoded-image hit may still need worker preparation if its viewport derivative
is absent/incompatible. Compare render-hit and prepared-paint events as well.

A repeatable manual GUI benchmark creates eight synthetic 7728 x 5152 JPEGs,
warms the cache, then alternates between two images eight times. Every selection
must be a cache hit; background decoding of other list images is allowed.
It uses isolated settings: 5 workers, 6 slots, 2048 MiB total, 256 MiB per image,
full-resolution decoding, wraparound, and a requested 1280 x 900 window. The logged actual
viewport can differ with display scaling/window manager constraints. Each
sample waits for foreground delivery and a GTK after-paint boundary (possibly
one extra redraw); background dwell happens outside that interval; the trace's first request-to-draw span is the more precise latency.
It does not change your Xfconf settings. A real graphical session is required.

```sh
RISTRETTO_PROFILE=1 build-profile/src/test-image-viewer --benchmark-navigation=good > good.out 2> good.log
RISTRETTO_PROFILE=1 build-profile/src/test-image-viewer --benchmark-navigation=nearest > nearest.out 2> nearest.log
```

For stack sampling, use `perf record --call-graph dwarf` with the profiling
binary when the operating system allows perf events. DWARF unwinding also
covers installed libraries that were built without frame pointers. Timing
traces work without perf privileges. Glycin helper work is in separate processes
and is not attributed to the parent process's CPU samples. Parent worker spans
include time waiting for those helpers. This instrumentation does not measure
helper CPU/memory or total process memory.

## Sustained scrolling benchmark

```sh
RISTRETTO_PROFILE=1 build-profile/src/test-image-viewer --benchmark-scrolling > scrolling.out 2> scrolling.log
```

This creates sixteen synthetic 7728 x 5152 JPEGs before installing file monitors,
uses a requested 2800 x 1900 window, warms the initial neighborhood and sends 48
selections at 250 ms intervals with wraparound and reversal. It does not wait for
loading between inputs. The summary reports completed distinct frames, longest
application presentation gap, final-target latency and sampled peak accounted
storage/reservations. The same 6-slot, 5-target/worker, 2048/256 MiB settings apply.
The actual viewport is printed. More source images can be skipped when decoding
cannot sustain the input rate; the final target must still be displayed.

Run this separately from compilation, sanitizers and other benchmarks. Texture/GPU
acceleration is not used. Report worker preparation separately from GTK painting:
moving work off the UI thread does not make first-use decoding/resampling free.

## CPU preparation and target interest

`render-prepare` reports batch wall time, viewport dimensions, peak parallel stripe
count and cancellation status. It includes waiting for worker permits. The shared
task ceiling does not count codec-helper threads. `work-interest` records when
navigation makes a job useful/obsolete; `work-discarded` records cooperative stops,
and `work-cancelled` records hard cancellation with its originating policy function.
Decode `worker` spans include an `obsolete` flag, so discarded decode time can be
separated from completed work. Partial conversion time remains in `pixel-conversion`.

Set `RSTTO_BENCH_LARGE=1` for either viewer benchmark to request a 5120x2792 window
(5003x2664 image viewport on the development desktop). Confirm the logged viewport
before comparing runs. Keep smoothing, fixture generation and navigation sequence
identical, and run baseline/optimized builds sequentially without compilation or
sanitizers. Measure parent and pooled-helper RSS separately; neither RSS nor summed
stage wall times represent accounted cache bytes or pure codec CPU time.

For isolated cold preparation costs, also set `RSTTO_BENCH_COLD=1` with
`--benchmark-navigation=good`. This disables speculative targets only for the
benchmark and visits eight images with six cache slots, forcing every timed
selection to decode and prepare a new image. The five-worker setting and byte
limits remain identical. Compare per-image conversion plus render spans separately
from sustained scrolling with five targets, where most preparation is speculative.

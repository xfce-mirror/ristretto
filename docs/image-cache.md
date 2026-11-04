# Image cache and preloading

Ristretto caches decoded local still images using GLib, GIO, GdkPixbuf and
Cairo. Safe dimension probing additionally uses the optional libglycin-2
dependency. Each viewer has its own cache. The GLib 2.66 and GTK 3.24 minimum
versions are unchanged.

The optional [OpenGL renderer](opengl-rendering.md) retains this CPU accounting
and adds separately tracked GPU results. Select it in Advanced preferences or
with the renderer environment override; CPU rendering remains the default.

## Configuration

These settings control reuse of decoded images and loading neighboring images
in advance. They are stored per user in the `ristretto` Xfconf channel and apply
to running viewers when changed. The normal and profiling binaries, including
the experimental OpenGL variants, use the same channel. Each viewer has its own
cache budget; the budget is not shared across all open windows or processes.

The table lists **compiled defaults**, not values saved in a particular user's
configuration. Defaults also apply when running with Xfconf disabled.

Open **Edit → Preferences → Advanced** to enable caching and adjust the image,
memory and preload limits. Changes take effect immediately; closing the dialog
does not undo them. Opening the dialog preserves existing values, and changes
made through Xfconf also update an open dialog. The separate **Rendering** radio
buttons save the CPU/GPU choice for the next launch and require a restart; see
[renderer configuration](opengl-rendering.md). Disabling caching leaves the
numeric values intact for later use. Without Glycin support, the tab explains
that caching is unavailable and disables its controls.

The command-line examples below offer another way to change the same settings.
For other options, see Ristretto's
[Preferences manual](https://docs.xfce.org/apps/ristretto/preferences).

| Property | Type | Default | Valid values | Meaning |
| --- | --- | --- | --- | --- |
| `/image/cache-enabled` | bool | true | true or false | Enable decoded-image caching and preloading. False keeps normal foreground viewing available. |
| `/image/cache-max-images` | uint | 5 | At least 1 | Maximum image slots, including displayed images and reserved loads. Two slots are reserved for current/previous viewing history; values of 1 or 2 disable preloading. |
| `/image/cache-max-mib` | uint | 256 | 0 or greater | Total CPU image storage and loading/rendering reservations per viewer, in MiB. Zero disables caching and preloading. |
| `/image/cache-max-image-mib` | uint | 128 | 0 or greater | Per-image CPU admission limit in MiB. Applies to full-source and decoded-output estimates, and to retained source plus CPU rendered copy. Zero disables caching and preloading. |
| `/image/preload-count` | uint | 2 | 0 or greater | Number of directional neighbors targeted after navigation. Zero disables all preloading while retaining foreground caching. |
| `/image/preload-workers` | uint | 2 | 1–32 | Maximum concurrent preload jobs before image-slot, task and byte limits are applied. One makes preload jobs serial. |

`uint` means an unsigned integer. **1 MiB = 1,048,576 bytes; 2 GiB = 2048 MiB.**
The limits concern decoded pixels and working reservations, not compressed file
sizes or total process memory. GPU textures have separate bounds; see
[OpenGL accounting](opengl-rendering.md#accounting).

### Inspect and change settings

List stored settings and their values, or query a single property:

```sh
xfconf-query -c ristretto --list --verbose
xfconf-query -c ristretto -p /image/cache-max-mib
```

If a property has not been stored, the compiled default above applies. The
`--create --type` options in the following examples allow a setting to be written
whether or not it already exists. Set only the properties you want to override.

Retain caching but disable loading neighboring images in advance:

```sh
xfconf-query -c ristretto -p /image/preload-count --create --type uint --set 0
```

Disable both caching and preloading, while retaining normal foreground viewing:

```sh
xfconf-query -c ristretto -p /image/cache-enabled --create --type bool --set false
```

Use up to four preload workers:

```sh
xfconf-query -c ristretto -p /image/preload-workers --create --type uint --set 4
```

Set a smaller total budget:

```sh
xfconf-query -c ristretto -p /image/cache-max-mib --create --type uint --set 128
```

### Example for large images

This example enables six image slots, a 2 GiB CPU budget, a 256 MiB per-image
limit, and five configured preload targets and workers. These are overrides,
not changes to the defaults:

```sh
xfconf-query -c ristretto -p /image/cache-max-mib --create --type uint --set 2048
xfconf-query -c ristretto -p /image/cache-max-image-mib --create --type uint --set 256
xfconf-query -c ristretto -p /image/cache-max-images --create --type uint --set 6
xfconf-query -c ristretto -p /image/preload-count --create --type uint --set 5
xfconf-query -c ristretto -p /image/preload-workers --create --type uint --set 5
xfconf-query -c ristretto -p /image/cache-enabled --create --type bool --set true
```

A 7728×5152 image needs about 152 MiB for one four-byte-per-pixel decoded
surface, before any rendered copy or loading temporaries. Raising the per-image
limit allows that source to be considered for caching; it does not guarantee
that every load can fit alongside the images already retained.

`preload-count` controls how many neighbors are targeted; `preload-workers`
controls how many preload jobs can run concurrently. Neither setting enlarges
the memory or image-slot budgets. Six slots allow at most **four speculative
images**, including completed preloads and reserved loads. The shared task
ceiling is `1 + min(preload-workers, max(0, cache-max-images - 2))`: five in this
example, including foreground work. Byte reservations can reduce concurrency
further. Initial population and adaptive lookahead are described below.

Worker changes apply without reloading the displayed image. Lowering limits
cancels excess work and evicts eligible entries; cancelled work retains its
reservation until cleanup. The displayed image remains available even if it
exceeds a newly lowered limit. An image that cannot be cached can still be viewed
through the normal foreground path. Builds without safe Glycin probing cannot
enable caching or preloading through these settings.

GPU worker selection is separate: `RISTRETTO_GL_WORKERS` is an experimental
environment variable, not an Xfconf property, and does not increase the shared
task ceiling. See the [OpenGL renderer configuration](opengl-rendering.md).

### Restore defaults

To remove one saved override, for example the total memory budget:

```sh
xfconf-query -c ristretto -p /image/cache-max-mib --reset
```

Repeat with each property you want to reset, then restart Ristretto to initialize
the compiled defaults. Reset individual properties rather than the entire
`/image` subtree, which also contains other image preferences. Alternatively,
write the table's default values explicitly to apply them to running viewers.

## Navigation and lifetime

Initial targets are `+1, +2, -1, +3`, subject to capacity, deduplication and the
wrap setting. After navigation, targets follow the direction of travel, while
previously viewed images remain in a weighted LRU. Explicit next/previous actions
preserve direction across wraparound. Sorting updates positions without reversing
direction; directory replacement clears the cache. A zero preload count disables
initial as well as subsequent speculation.

A hash table indexes completed images, another coalesces active requests, and
queues hold recency links and pending work. Demand hits move existing queue links;
inspection of a candidate does not refresh recency. Prefetched entries start below
demand-used entries. The displayed image and higher-priority targets are protected
from speculative eviction. A candidate which cannot fit is skipped, without an
automatic eviction/reload retry cycle.

There is one foreground worker slot and a configurable number of speculative
worker slots. GLib's shared worker pool runs the admitted decodes concurrently;
the setting is a concurrency ceiling, not a request for dedicated threads.
Dimension probes and admission proceed in candidate priority order so a later
target cannot reserve memory ahead of an earlier one. A candidate waits if another
decoder is temporarily holding the memory it needs, including probe-buffer memory.
It is skipped if it still cannot fit once those reservations have drained.

Cancelled workers retain their slots and reservations until cleanup. Lowering the
worker limit cancels lower-priority preloads and prevents replacements from
starting while those jobs drain. A promoted preload is protected and retains its
original worker slot until completion; it is not decoded a second time. Foreground
loading can overlap existing preloads. New speculation may start once the
foreground decode or render has obtained its reservation; admission gives the
foreground priority over new speculative work. Workers own streams, loaders and cancellation objects, with
snapshots of decode options; they never access GTK widgets. Main-thread delivery
uses weak object signal connections and selection generations to reject obsolete
results. Probe, decode and derivative-render phases share the same worker slots;
rendering does not create an additional pool outside these limits.

## Memory accounting and exceptions

Retained still-image surfaces are charged by actual Cairo stride times height.
The viewer references the surface without making a full pixel copy. The previous
displayed surface remains charged until the viewer replaces it.

Before speculative decoding, a cancellable metadata probe reads at most 1 MiB
from the identity-verified open descriptor, in 4 KiB chunks. It supplies only
that immutable prefix to `gly_loader_new_for_bytes()` and calls
`gly_loader_load_async()` with transformations disabled. It reads dimensions
without requesting any Glycin frame and never constructs a GdkPixbuf loader.
Using `gdk_pixbuf_get_file_info()` or setting a pixbuf loader's size to zero is
not a safe substitute: GdkPixbuf's Glycin bridge can still request pixel data.

Each probe reserves one slot and **8 MiB** across prefix reading, asynchronous
metadata loading, cancellation and the final descriptor/path identity check.
This allowance includes the 1 MiB input allocation and estimates metadata/input
copies within Glycin. It is not an allocator-enforced cap on library internals.
The reservation is released only after the callback and local resource cleanup;
Glycin manages pooled helpers and asynchronous remote-object cleanup itself.
An unsuccessful or cancelled probe never falls back to a pixel decoder.

Missing dimensions, unsupported or incomplete prefixes, unsafe identity and
invalid/overflowing dimensions prevent speculation. Formats whose metadata needs
more than the first 1 MiB therefore remain foreground-only. Both full-resolution
source RGBA size and requested output RGBA size must satisfy the per-image limit.
A successful probe is followed by identity revalidation; admitted decodes also
recheck identity immediately before reading pixels.

Camera RAW probes are temporarily excluded because `glycin-raw` decompresses RAW
data during loader initialization. Checks use the GIO `image/x-dcraw` hierarchy,
Ristretto's existing RAW classifications, and loader MIME mappings whose executable
is `glycin-raw`. Content, extension-only and combined filename/content guesses are
checked independently, including TIFF-based RAW files and renamed RAW content.
Configured mappings are collected at cache creation from `GLYCIN_DATA_DIR` when
set, otherwise the XDG data directories under `glycin-loaders/2+/conf.d`.
Normal foreground loading and its existing RAW fallbacks remain available.

Meson option `-Dglycin-probe=auto` detects `glycin-2 >= 2.0.0`. Use `enabled` to
require it or `disabled` to test the fallback build. Without Glycin probing,
caching and speculation are disabled even when Xfconf enables them; normal
foreground viewing remains available. The Advanced preferences controls are
disabled in that configuration.

A decode reserves `source RGBA + 3 * output RGBA + 1 MiB`. This conservatively
covers source/output pixels, conversion temporaries and the foreground input
buffer. Speculative reads use smaller chunks for cancellation responsiveness.
Reservations become actual surface weights only after decoder cleanup. Consequently
a cache can hold fewer than five images even when five finished surfaces would
fit: loading the next one also needs budget.

**This bounds accounted image storage and reservations, not total process RSS.**
Codec-private allocations beyond the estimates, metadata internals, thumbnailing, GTK and allocator
retention are outside this accounting. Codec peaks remain estimates. Animations,
remote files, streams, files without trustworthy identity and foreground images
that cannot fit are loaded normally without cache admission. Speculation remains
suspended until a subsequent eligible foreground load. An animation is abandoned
as a speculative target as soon as the loader identifies it.

On a GLib low-memory warning, speculative jobs are cancelled and non-displayed
entries evicted. Speculation may resume only after another navigation and at least
30 seconds without a further warning.

## Viewing history and speculative admission

Two slots are reserved for current/previous viewing history. Initial and
subsequent target lists, speculative occupancy and the worker ceiling are all
limited to `max(0, cache-max-images - 2)`. Occupancy includes completed images
that have never been displayed and speculative jobs with a reserved slot.
Cancelled jobs remain charged until cleanup; promotion transfers a reservation
to foreground use but retains its original worker slot until completion.
With six slots and five configured workers/targets, at most four speculative
images can occupy the cache. Capacities of zero, one or two disable speculation;
foreground caching remains available where the normal limits permit it.

Speculation cannot evict the displayed image or the most recently viewed cached
history entry. These entries still consume their actual bytes and slots. The
byte budget may further reduce concurrency or stop preloading: three full-size
7728x5152 decode reservations plus two retained images need about 2129 MiB,
exceeding a 2048 MiB budget. No extra owning references pin history indefinitely;
foreground demand, invalidation, low-memory handling and forced limit reductions
can still discard it.

Admission first checks whether the entire request can fit after eligible LRU
evictions. If it cannot, no entries are removed. A speculative job waits only
when active-job cleanup could make admission possible, and is reconsidered on
completion rather than repeatedly retried. Forced limit reductions separately
trim what can be released, while displayed images and cancelled workers retain
their existing accounting until ownership ends.

## Freshness and rendering

Keys include URI, device, inode, size, modification/change timestamps at native
nanosecond precision where supported, and effective decode options. Each hit
requires a fresh opened-descriptor identity matching the path. Decode completion
rechecks both descriptor and path; a changed file is discarded. A foreground
change during decoding reports an error and can be retried with Refresh.

File-monitor events and explicit Refresh invalidate pixels and file metadata.
A missed notification is caught by identity checking, including same-size edits,
restored modification times, inode replacement and symlink retargeting. Quality
or effective monitor-size changes discard incompatible variants. Rotation, zoom,
scrolling and smoothing remain viewer state and form a separate render key;
changing them does not invalidate decoded pixels. Animation playback and printing
retain their existing paths.
The existing MIME hints and RAW fallback exclusions remain in use; a stale MIME
hint is discarded when identity reveals replacement.

## Sustained navigation and CPU rendering

The viewer tracks a committed image, one active request and at most one pending
navigation target. New next/previous steps accumulate relative to the latest
requested position, including wraparound; direct selections replace that target.
An ordinary selection does not cancel the active decode/render. Its frame is
committed and passes a GTK after-paint boundary before the latest pending target
is dispatched. Intermediate selections may be skipped, without waiting for input
to stop or lowering image quality. An unmapped viewer advances through an idle
callback instead of waiting for a paint that cannot occur.

The thumbnail selection follows the requested position. The title, metadata,
menus and file operations follow the committed image, whose pixels and file
identity change together. The old committed image stays available during loading.
Errors also release the navigation gate. Removal, directory replacement,
Refresh/invalidation, incompatible options and destruction retain hard cancellation
and generation checks; late results cannot replace the new directory or selection.

For downscaled still images, workers prepare a viewport-bounded Cairo image
surface with the same transforms and GOOD/NEAREST smoothing as the normal path.
Conversion reads immutable pixbuf storage without requesting a writable full-image
copy on GdkPixbuf 2.32 and later; older headers retain the compatible accessor.
Opaque RGB conversion packs pixels directly using the optional
[SIMDe](https://github.com/simd-everywhere/simde) headers (version 0.8.2 or newer).
The explicit `simde_*` SSE2 interface selects native instructions where available,
including SSE2 on x86 and NEON on ARM64. The byte-packing implementation is enabled
only on little-endian targets. Big-endian targets, builds without SIMDe, and
explicitly disabled builds retain the scalar implementation. No native-CPU
build flags or new runtime library are required. RGBA conversion preserves the
original premultiplication rounding and directly packs fully opaque pixels. Conversion checks interest/cancellation every
64 rows and allocates one destination surface.

Meson defaults to `-Dsimde=auto`. Use `-Dsimde=enabled` to require the headers or
`-Dsimde=disabled` to build and test the scalar fallback. On Debian/Ubuntu the
development package is `libsimde-dev`; the installed package must meet the version
requirement. SIMDe changes conversion instructions, not image allocations,
reservations, cache limits or worker counts.

Smoothed scaling uses 128-device-pixel row stripes with independent Cairo wrappers
and contexts, disjoint writes, and one shared output allocation. Main-thread
completion publishes only a finished image. Nearest-neighbor scaling retains a
single whole-image operation: clipping changes Pixman's sample rounding for some
transforms, so splitting it would change legacy pixels.

All worker tasks share a ceiling of `1 + min(preload-workers, max(0, cache-max-images - 2))`.
Foreground scaling can borrow idle permits; a speculative image uses at most one
stripe task at a time. Active demand has priority, followed by the latest pending
selection and ranked neighbours. Lowered limits stop dispatch until existing tasks
drain. Stripes do not create additional image slots or full-image temporary buffers.
This ceiling covers Ristretto tasks, not threads inside codec helper processes.

The viewer supplies the pending selection and neighbour candidates in one scheduling
snapshot. Pending work consumes speculative capacity until promotion and cannot
create a second foreground request. Obsolete targets can regain interest until a
worker commits to stopping. Workers check interest before conversion and between
conversion blocks/scaling stripes; library calls already in progress may finish.
Hard invalidation/cancellation is irreversible and retains reservations until cleanup.

The immutable render key includes viewport dimensions, output/source device
scales, orientation, zoom and pan through its transform matrix. The decoded
source identity/options remain the cache lookup key. A completed image retains
at most one derivative; preloads also prepare derivatives using main-thread
snapshots of each candidate's saved view. Compatible returns reuse both pixels
and rendering, while GTK only composites the prepared image and draws overlays.
In CPU mode, native-size/enlarged images and animations use the existing drawing path.

A derivative shares the source image's slot. Its actual stride times height is
added to the entry's weight, and combined source/derivative retained storage must
fit the per-image admission limit. Rendering reserves three output-surface
weights for output and estimated Cairo temporaries; pinned source storage remains
charged separately. Cancelled render jobs keep reservations and their original
worker slots until cleanup. Old displayed derivatives stay charged through delivery.

For byte pressure, admission plans derivative removal before complete-entry eviction. Derivatives
of eligible non-displayed, unpinned entries may be discarded. Speculation protects
both the source and derivative of current/previous viewing history, and higher-priority
targets. Foreground demand and forced limit reductions may reclaim previous history. Slot pressure alone does not discard derivatives:
doing so cannot free an image slot. Failed admission changes neither derivatives nor
entries. If preparation cannot fit, viewing falls back to the original full-quality
Cairo path, which can be slower. Forced limit reductions and memory pressure keep
the existing displayed-image exception and cancellation-drain accounting.

No shared cached surface has mutable viewer device-scale state: workers and the
viewer create independent wrappers over pinned, immutable source bytes. All GTK
access, container changes and presentation decisions stay on the main thread.
CPU preparation remains available without the optional OpenGL backend. The byte
budget still bounds accounted storage and reservations, not allocator, codec or
total process memory.

## Tests and benchmark

```sh
meson setup build
meson compile -C build -j4
meson test -C build --print-errorlogs

meson setup build-sanitize -Db_sanitize=address,undefined -Db_lundef=false -Dbuildtype=debug
meson compile -C build-sanitize -j4
LSAN_OPTIONS="suppressions=$PWD/tests/lsan.supp" meson test -C build-sanitize --print-errorlogs
```

The cache suite uses real PNG/JPEG/BMP/GIF fixtures and deterministic worker
barriers. It checks actual concurrent decodes, configurable worker limits, live
worker-limit changes, memory-constrained admission, recency, reservations,
cancellation, promotion, freshness,
pressure, limits, conversion and lifetime. The viewer integration test requires a
graphical display (otherwise Meson reports it skipped); it uses isolated settings
and exercises real navigation, EXIF, rotation, printing's pixel export, sorting,
file monitoring, Refresh and destruction during loading. Sustained tests send
40 selections at 250 ms intervals through sixteen fixtures, hold decoding, conversion,
or rendering across three input ticks, and require ongoing correctly identified
frames, no navigation-induced demand cancellation, final-target delivery, and
bounded byte/slot/worker accounting. Additional tests cover unmapping, pending
selection removal, directory replacement and destruction with rendering in flight.
Raster tests compare all eight EXIF orientations, transparency, cropping, quality
limits and device scaling against the original transform path (exact nearest-neighbor pixels;
at most two channel levels of rounding tolerance for smoothed output). Installed decoder
backends may also require the session bus or a functioning codec sandbox.

Conversion tests compare native SIMDe, SIMDe with native intrinsics disabled,
and the scalar fallback against the same exact integer reference. They cover
RGB/RGBA, alpha boundaries, cancellation and immutable pixbufs. On systems with
anonymous memory mappings, read-only input rows end immediately before an
inaccessible page to detect writes or overreads across short rows, unaligned
starts, vector tails and padded strides, including an unpadded final row.

The Preferences suite also requires a graphical display. It uses isolated
settings to check that opening the Advanced tab preserves values, edits and
external property changes stay synchronized, unsigned ranges and zero values
are retained, and closing dialogs releases their bindings. It covers disabled
caching and builds without Glycin, without changing the user's Xfconf channel.
Renderer tests check the radio group, environment overrides, unavailable builds
and the immutable startup choice while the saved preference changes.

The sanitizer suppression covers only allocations whose stacks originate in
Fontconfig's process-global font caches. Application and decoder leak detection
remain enabled. The cache suite also runs without that suppression.

Run performance comparisons sequentially, without sanitizers:

```sh
build/tests/test-image-cache --benchmark=off
build/tests/test-image-cache --benchmark=on
# Compare serial preloading with two concurrent workers:
build/tests/test-image-cache --benchmark=1
build/tests/test-image-cache --benchmark=2
```

Both runs generate the same eight 1920x1280 JPEG fixtures and follow the same
29-selection forward/backward sequence. Preloading finishes between selections,
so these are warm navigation latencies, not continuous-scroll throughput. The
output includes total decodes (including unused speculation), demand hits, median
and p95 latency, initial preload completion time, total time waiting for preloads,
peak accounted storage/reservations, parent peak RSS and maximum
child RSS. RSS includes codec/toolkit overhead and allocator retention; subtracting
independent peaks does not isolate codec-private memory. Child peak RSS is not a
sum of simultaneously live codec processes.

The headless `image-probe` test links GIO and Glycin without GdkPixbuf. Its PNG
fixtures have valid dimension headers but no compressed pixels, including a
50000 x 50000 image (about 9.3 GiB RGBA). Successful dimension extraction from
these fixtures verifies that no frame is required. Cache tests separately check
that oversized probes never enter the GdkPixbuf decoding path, and exercise
cancellation while metadata is outstanding, identity changes before admission,
RAW exclusion and foreground fallback. The build without Glycin has its own
foreground-only cache regression.

For a synthetic X-T50-size workload with the 2 GiB/256 MiB limits:

```sh
build/tests/test-image-cache --benchmark-x-t50=off
build/tests/test-image-cache --benchmark-x-t50=2
```

This uses 7728 x 5152 JPEGs and the same navigation sequence as the smaller
benchmark. The `render_hash` covers every output pixel across the sequence and must match
between enabled and disabled runs. Hashing is outside the timed navigation
interval. `probe_mean_ms` includes prefix reading, metadata loading and identity
revalidation. Measure helper RSS externally: the `getrusage()` child figure in
the benchmark does not represent the peak of a still-running pooled decoder.

### Input quiet period and atomic scheduling

Selection promotion, pending selection and ranked neighbours commit together
before the scheduler pumps new tasks. During input, the pending image and nearest
two neighbours are eligible immediately. Two adjacent navigation events in the
same direction no more than 300 ms apart enable the full available speculative
capacity. Explicit next/previous wraparound counts as adjacent. Reversal, jumps,
ordering/directory changes and view changes reset this burst state. Otherwise,
farther neighbours wait for `SPECULATION_QUIET_US` (300 ms) without a meaningful selection, direction/order or
view change. Identical refreshes, rank renumbering, promotion and completions do
not restart that timer. The timer gates new admission, never useful admitted
decoding/conversion/cleanup. Preparing work blocks only equal/lower priority
probe admission, so a deferred far target cannot hold up a pending selection.
A same-image view update defers new neighbour work until its compatible result
is ready. Running decoders keep the existing safe-checkpoint/renewed-interest
policy; obsolete queued tasks are removed before upload or decoding.

Startup GPU warmup uses the same shared task ceiling and reserves 64 KiB of
explicit host scratch per active worker warmup. Persistent shader caching adds
a 10 MiB host reservation before I/O/allocation; when that cannot fit, warmup
proceeds without persistence and without evicting protected history. It has no image slot and cannot
evict images to make room. Cancelled warmups retain both permit and reservation
until their fences and resources drain. GPU temporary textures and the retained
1 KiB LUT per initialized worker are tracked outside the RAM budget. Current and
previous source/derivative history protection and transactional admission remain
unchanged.

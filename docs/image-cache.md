# Image cache and preloading

Ristretto caches decoded local still images using its existing GLib, GIO,
GdkPixbuf and Cairo dependencies. No additional library is required. Each viewer
has its own cache. The GLib 2.66 and GTK 3.24 minimum versions are unchanged.

## Configuration

These properties are live-bound to the `ristretto` Xfconf channel. Defaults also
apply when running with Xfconf disabled. No preferences-dialog controls are added.

| Property | Type | Default | Meaning |
| --- | --- | --- | --- |
| `/image/cache-enabled` | bool | true | Enable caching and speculation |
| `/image/cache-max-images` | uint | 5 | Image slots, including displayed surfaces and reserved loads |
| `/image/cache-max-mib` | uint | 256 | Total retained-storage and reservation budget in MiB |
| `/image/cache-max-image-mib` | uint | 128 | Per-image full-source and output admission limit in MiB |
| `/image/preload-count` | uint | 2 | Directional targets after the first selection |

For example, retain caching but disable speculative loads:

```sh
xfconf-query -c ristretto -p /image/preload-count --create --type uint --set 0
```

Set a smaller total budget:

```sh
xfconf-query -c ristretto -p /image/cache-max-mib --create --type uint --set 128
```

A zero value for either memory limit disables caching. `cache-max-images` must
be at least one. Lowering limits cancels excess work and evicts eligible entries;
cancellation does not release a reservation until the worker has cleaned up.
The displayed image remains available even if it exceeds a newly lowered limit.

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

There is one foreground worker slot and one speculative worker slot. Cancelled
workers retain their slots until cleanup. Selecting an active preload promotes
that job rather than decoding it twice. Speculation starts only after foreground
completion. Workers own streams, loaders and cancellation objects, with snapshots
of decode options; they never access GTK widgets. Main-thread delivery uses weak
object signal connections and selection generations to reject obsolete results.

## Memory accounting and exceptions

Retained still-image surfaces are charged by actual Cairo stride times height.
The viewer references the surface without making a full pixel copy. The previous
displayed surface remains charged until the viewer replaces it.

Before speculative decoding, a cancellable GdkPixbuf probe reads at most 1 MiB,
in 4 KiB chunks. The probe reserves a slot and its input buffer. Missing or
invalid dimensions, unsafe identity, unsupported streams and overflowing size
calculations prevent speculation. Both full-resolution source RGBA size and
requested output RGBA size must satisfy the per-image limit.

A decode reserves `source RGBA + 3 * output RGBA + 1 MiB`. This conservatively
covers source/output pixels, conversion temporaries and the foreground input
buffer. Speculative reads use smaller chunks for cancellation responsiveness.
Reservations become actual surface weights only after decoder cleanup. Consequently
a cache can hold fewer than five images even when five finished surfaces would
fit: loading the next one also needs budget.

**This bounds accounted image storage and reservations, not total process RSS.**
Codec-private allocations, probe internals, thumbnailing, GTK and allocator
retention are outside this accounting. Codec peaks remain estimates. Animations,
remote files, streams, files without trustworthy identity and foreground images
that cannot fit are loaded normally without cache admission. Speculation remains
suspended until a subsequent eligible foreground load. An animation is abandoned
as a speculative target as soon as the loader identifies it.

On a GLib low-memory warning, speculative jobs are cancelled and non-displayed
entries evicted. Speculation may resume only after another navigation and at least
30 seconds without a further warning.

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
scrolling, smoothing, animation playback and printing remain viewer operations.
The existing MIME hints and RAW fallback exclusions remain in use; a stale MIME
hint is discarded when identity reveals replacement.

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
barriers. It checks recency, reservations, cancellation, promotion, freshness,
pressure, limits, conversion and lifetime. The viewer integration test requires a
graphical display (otherwise Meson reports it skipped); it uses isolated settings
and exercises real navigation, EXIF, rotation, printing's pixel export, sorting,
file monitoring, Refresh and destruction during loading. Installed decoder
backends may also require the session bus or a functioning codec sandbox.

The sanitizer suppression covers only allocations whose stacks originate in
Fontconfig's process-global font caches. Application and decoder leak detection
remain enabled. The cache suite also runs without that suppression.

Run performance comparisons sequentially, without sanitizers:

```sh
build/tests/test-image-cache --benchmark=off
build/tests/test-image-cache --benchmark=on
```

Both runs generate the same eight 1920x1280 JPEG fixtures and follow the same
29-selection forward/backward sequence. Preloading finishes between selections,
so these are warm navigation latencies, not continuous-scroll throughput. The
output includes total decodes (including unused speculation), demand hits, median
and p95 latency, peak accounted storage/reservations, parent peak RSS and maximum
child RSS. RSS includes codec/toolkit overhead and allocator retention; subtracting
independent peaks does not isolate codec-private memory. Child peak RSS is not a
sum of simultaneously live codec processes.

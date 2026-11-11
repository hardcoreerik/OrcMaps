# Ordered tile-rendering candidate — October 20, 2026

This follows the reproduced boundary overdraw in
[the worldwide checkpoint](2026-10-20-world-boundary-paint-order.md).
It is a source/host candidate, not an installed device fix or a release.

## Implementation

- `renderer.cpp/.hpp`: batch rendering paints polygons before strokes/points,
  retaining source order within each group. The immediate `RenderFeatureAt`
  API keeps its existing caller-order contract.
- New explicit ordered streaming helpers draw polygons immediately and retain
  projected strokes/points in flat, reusable coordinate and path buffers.
  Flush once per successfully decoded source tile, including its wrapped copies.
  No extra archive read/decompression pass or whole decoded tile is required.
- Hard limits are 65,536 points and 4,096 paths per source tile across placements.
  An oversized feature is rejected before enqueue; the tile is poisoned and must
  not be published. No silent truncation. Allocations can still fail below these
  limits; failure poisons the queue and propagates for the worker to catch.
- Flush checks the optional service callback every 128 segments/points, restores
  clip state on all exits, and discards pending counts. Reset retains bounded
  stroke capacity but releases polygon workspace without allocating. The latter
  preserves its previous per-tile lifetime and resolves the review finding.
- Four built-in style versions advance from 2 to 3 so rendered caches cannot
  reuse pixels from the former composition order.

`OrcSDR` has a locally prepared, **uncommitted** adapter using one queue per
Pipeline, reset on tiles, failed decode, cancellation and caught bad_alloc.
Pipeline destruction/reclaim releases retained capacity. Flush time is counted
as rendering; queue peak/retained bytes and capacity failures are reported.
The dependency pin is still `722d0d3`: it cannot build this adapter until the
reviewed engine revision is fetchable and the pin/lock are updated together.

Other immediate/banded example consumers do not automatically acquire ordering.
`examples/hardware-benchmark/bench.cpp`, `tools/embedded-world-check/main.cpp`
and CYD cached drawing need deliberate migration/verification before claiming
engine-wide consumer coverage. This candidate does not silently change their API.

## Executed tests

Invocation directory: `F:/AI/OrcSDR-TEMP/orcmaps-quality-delivery`.

```sh
cmake -S tests/host -B .local/boundary-host-20261019
cmake --build .local/boundary-host-20261019 --target orcmap_paint_order_tests orcmap_tile_composition_tests -j2
ctest --test-dir .local/boundary-host-20261019 -R '^orcmap_(paint_order|tile_composition)_tests$' --output-on-failure
```

The new regression first failed for boundary-before-land and road-before-water.
After correction: **2/2 focused CTest targets pass**. The initial target-only
build was attempted before CMake regeneration and correctly reported no target;
the configured build then produced the expected red regression.

Coverage includes mixed extents, three wrapped copies, source stroke order,
points, nested clips, empty/degenerate/hidden features, point/path capacity
rejection without partial enqueue, cancellation inside a long path, discard,
reuse, polygon workspace release, and actual failure injection into each of
the two queue allocations followed by successful recovery.

The same paint-order test was compiled/run with ASan and UBSan:

```sh
c++ -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -g -Iinclude -Iadapters/host -Itests/host tests/host/test_paint_order.cpp src/core/geo.cpp src/core/viewport.cpp src/render/renderer.cpp src/render/style.cpp src/render/clip.cpp adapters/host/framebuffer_target.cpp -o .local/boundary-preview-20261020/paint-order-sanitized
.local/boundary-preview-20261020/paint-order-sanitized
```

Result: **PASS**, no sanitizer findings. The focused existing style harness also
passes, including version 3/cache invalidation and hierarchy gates. Relevant logs:

- `paint-order-configure-20261020.log`
- `paint-order-red-build-configured-20261020.log`, `paint-order-red-test-20261020.log`
- `paint-order-reviewed-build-20261020.log`, `paint-order-reviewed-tests-20261020.log`
- `paint-order-sanitized-tests-20261020.log`, `paint-order-style-tests-20261020.log`

## Real global archive

The actual OrcSDR Pipeline host target, built against this local engine, rendered
the eight reference views at zoom 8 / 1280×720. All found 24/24 tiles, with no
capacity failures. Each geometry frame is byte-identical to the corrected batch
inspector frame. Country/region/local pixels are now visible where geometry
falls inside the viewport. Eugene's regional fragments lie outside its viewport;
that frame has 7,973 local-boundary pixels and no regional-color pixels.

| View | Queue peak bytes | Retained queue bytes after view |
| --- | --- | --- |
| Eugene | 76,056 | 88,704 |
| London | 212,736 | 261,120 |
| Tokyo | 333,456 | 522,240 |
| Nairobi | 45,328 | 522,240 |
| São Paulo | 151,360 | 522,240 |
| Delhi | 108,552 | 522,240 |
| Sydney | 93,304 | 522,240 |
| Suva | 7,880 | 522,240 |

These are **64-bit host** byte counts, not P4 measurements. The persistent
capacity is intentional and reclaimable, but its P4 placement/pressure is open.
The existing label hide/show/zoom gates, allocation-source failure recovery and
pipeline cancellation checks passed in the same bounded executable.

OrcSDR invocation-root logs: `paint-order-pipeline-reviewed-build-20261020.log`,
`paint-order-pipeline-reviewed-tests-20261020.log`,
`paint-order-world-parity-20261020.log` (eight hashes/comparisons), and
`paint-order-adsb-p4-syntax-20261020.log` (4/4 actual P4 syntax checks: engine,
pipeline, service and ADS-B). Batch command log is
`paint-order-batch-world-20261020.log` in this engine directory.

## Open gates

Complete configured host regression, migration of other ordered consumers,
engine development-branch push, pin/lock regeneration, full native link,
P4 memory/performance under RF, hardware failure/reclaim checks, and physical
boundary contrast/quality are not closed. No main merge, asset publication,
native firmware build/flash or device access occurred in this checkpoint.

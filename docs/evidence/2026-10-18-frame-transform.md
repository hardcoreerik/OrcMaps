# Completed-frame reprojection — focused host evidence

Phase 3 prerequisite. `frame_transform.hpp` provides a graphics-independent
Mercator camera transform from an immutable source frame to a destination
viewport: scale plus x/y translation. Source center is projected into the new
camera using existing engine projection and nearest wrapped world placement.
Consumers must clip to the map rectangle and paint uncovered areas; this does
not synthesize new geography, wrap-copy pixels, or provide a tile cache.
It transforms the whole raster, so labels baked into it follow geography;
application overlays still need projection using the requested camera.

Focused test began with a missing-header compile failure, then passed **1/1**:
`cmake --build .local/screen-bounds --target orcmap_frame_transform_tests -j4`
and `ctest --test-dir .local/screen-bounds -R '^orcmap_frame_transform_tests$' --output-on-failure`.
Tests identity, exact pan sign/delta, zoom in/out geographic point agreement,
antimeridian crossing and invalid input preserving output. Logs in invoked
worktree: `frame-transform-red-20261018.log`, `frame-transform-config-20261018.log`,
`frame-transform-build-20261018.log`, `frame-transform-green-20261018.log`.

No native build, pin change, push or flash. This does not prove motion latency.
OrcSDR integration must provide separate completed-frame ownership; its worker
currently holds the frame lock throughout decode and writes the only canvas.
No UI read of that canvas while rendering is safe.

Proposed narrow adapter implementation: a completed-frame PSRAM snapshot with
its own short-held publication lock, separate from worker state. A picker-size
RGB565 snapshot costs another 1,417,728 bytes; include it in memory-reclaim
accounting and fail gracefully if unavailable. Publish only a complete current
generation, atomically copy camera/archive identity with the image, and suppress
reuse across map changes. UI owns all display writes and clips transformed
images to the map viewport. A failed allocation retains the existing delayed
renderer; it must not consume the internal DMA reserve.

Pan should use an integer translation/cropped image path; zoom may use native
sprite scaling, whose hardware cost is unproven. Do not promise 33 ms: current
full-frame copies cost about 77 ms. First measure feedback versus transfer cost,
then optimize the measured bottleneck. Tests must cover publication/cancellation,
reclaim, allocation failure, map identity, stale generation, exposed edges,
source/destination cameras and active-reception counters.

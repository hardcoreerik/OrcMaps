# Buffered-tile composition — October 18, 2026

Base `5572ed6`, branch `codex/map-quality-delivery`. Fix currently under validation; not pinned, flashed or released.

## Reproduced cause

The real global-candidate Eugene z8 host render showed a regular four-pixel grid. Sampled grid pixels were base land RGB(16,19,22), replacing park RGB(22,32,26). A synthetic two-tile scene reproduces the same overwrite: each tile has a base polygon buffered beyond the nominal tile and a park polygon ending at the tile edge. Rendering the next tile's buffered base erases the previous tile's cover.

The focused regression fails in all six cases before the fix: batch, streamed feature and projected-cache draw, each in both tile orders. Each case has 120 incorrect seam pixels in the sampled region. This is a composition defect, not a missing county border or an input-format conversion requirement.

## Change

Added optional native half-open scissor support to RenderTarget, implemented by the host framebuffer and M5GFX display/canvas adapters. Core immediate and cached paths intersect the existing caller clip with the nominal tile rectangle, then restore it. The rectangle derives from the engine-made screen map and viewport tile size; no hand-filled TileScreenMap coefficients, per-feature heap buffer, or renderer workaround in OrcSDR.

Legacy/custom targets retain source compatibility and caller-owned clipping until they implement the optional methods. In particular, the CYD demo's custom BandTarget is not silently claimed to have native clipping. M5GFX uses its existing getClipRect/setClipRect methods; device compilation/runtime remain separate gates.

## Focused tests and actual render

From `F:/AI/OrcSDR-TEMP/orcmaps-quality-delivery`, under Ubuntu WSL:

```sh
cmake -S tests/host -B .local/screen-bounds -DCMAKE_BUILD_TYPE=Debug
cmake --build .local/screen-bounds --target orcmap_tile_composition_tests orcmap_screen_bounds_tests -j4
ctest --test-dir .local/screen-bounds -R '^orcmap_(tile_composition|screen_bounds)_tests$' --output-on-failure
```

Red: 0/1 pass, all six composition cases fail. Green: 2/2 pass, including the earlier thick-stroke bounds regression. Additional focused composition checks pass for intersection/restoration of a preexisting dashboard clip, empty clip, offscreen normalization and rejected negative width. Logs saved at invocation root: `tile-composition-config-20261018.log`, `tile-composition-build-red-20261018.log`, `tile-composition-red-20261018.log`, `tile-composition-build-green-20261018.log`, `tile-composition-green-20261018.log`, `tile-composition-expanded-build-20261018.log`, `tile-composition-expanded-20261018.log`.

Rebuilt host inspector and rendered the same 1280×720 Eugene z8 viewport: 24/24 visible tiles found. The regular grid is absent on visual comparison. There are 12,423 changed pixels of 921,600; this is an intentional correction, not a pixel-identical engine-upgrade claim. Example pixel (500,200) changes from base land to park. Output PPM SHA-256 `c23b2660f2626cca79927457d3ea219833be27c7ae5111550107843b891dcf41`.

Actual images: `.local/global-assessment/eugene-z8-classified.png` before, `.local/global-assessment/eugene-z8-clipped.png` after. Image comparison record `.local/global-assessment/tile-seam-comparison.json`. Logs: `tile-composition-inspector-build-20261018.log`, `global-eugene-clipped-preview-20261018.log`. Host frame timing includes file output and cannot establish P4 performance.

## Open gates

User approved one full host engine regression run and adapter compile checks. Full host build exits 0; `ctest --test-dir .local/screen-bounds --output-on-failure` passes **4/4** entries (profile, stroke bounds, tile composition and the existing whole engine executable). Logs: `tile-seam-full-host-build-20261018.log`, `tile-seam-full-host-test-20261018.log`. This count is CTest entries, not an invented number of assertions.

Ran `.local/check_tile_seam_adapters.py` with the real cached SDK compiler flags, prepending this worktree's new core/adapter headers: P4 `map_service.cpp`, P4 core `renderer.cpp`, and the current CYD main consumer all pass syntax-only compilation. Exact script retained; log `tile-seam-adapter-syntax-20261018.log`. No SDK sources or managed components edited, dependency-output flags stripped, no link/firmware rebuild/flash. CYD custom BandTarget remains responsible for its own tile clipping; compile success does not extend the seam-fix claim to an adapter that has not implemented the optional scissor interface.

Text, hierarchy-specific styles, source provenance for release, full-world view acceptance, cached-motion performance and safe delivery remain open. No device access in this change.

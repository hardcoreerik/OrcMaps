# Explicit Protomaps v4 classification — October 18, 2026

Based on `70dcb47` on `codex/map-quality-delivery`. Scope: engine-owned, experimental input classification for the measured global candidate. No public signature change, pack conversion, managed-component edit or Tab5 pin/flash.

The candidate uses Tilezen-derived `kind`/`kind_detail`, plural `places`/`boundaries`, and `roads`. It must not pass through the OpenMapTiles class/subclass heuristic. Added the explicit `protomaps-v4` branch: road hierarchy, rail/aeroway, polygon land/cover/parks/buildings, water geometry/label distinction, administrative lines, city/town and village/hamlet label categories. Unknown layers/kinds, unsupported ferry/aerialway and inappropriate geometry remain unassigned. Original names and numeric administrative metadata are retained for layout and hierarchy styling.

Reference: https://docs.protomaps.com/basemaps/layers (checked October 18). This is independently written mapping from documented and measured fields, not copied upstream implementation.

Focused executable `orcmap_profile_tests` covers 30 mapping/skip cases plus numeric hierarchy preservation, wrong property types, null/empty input, unknown-profile no-mutation, and existing OpenMapTiles/overview profiles. Before implementation it fails because the new profile is rejected. After implementation it passes. Commands from the worktree root under WSL:

```sh
cmake -S tests/host -B .local/screen-bounds -DCMAKE_BUILD_TYPE=Debug
cmake --build .local/screen-bounds --target orcmap_profile_tests -j4
ctest --test-dir .local/screen-bounds -R '^orcmap_profile_tests$' --output-on-failure
```

Logs: `profile-config-20261018.log`, `profile-build-red-20261018.log`, `profile-red-20261018.log`, `profile-build-green-20261018.log`, `profile-green-20261018.log`; final expanded checks in `profile-final-20261018.log`. No full engine/native suite ran.

Rebuilt host pack inspector with this classifier, then rendered the actual 1280×720 Eugene z8 viewport, 24/24 visible tiles found. Log `global-eugene-classified-preview-20261018.log`; actual render `.local/global-assessment/eugene-z8-classified.ppm` and lossless `.png`. Host frame timing includes file output and is not an ESP32 result.

Visual inspection: real regional roads, water and cover render, but text is absent as expected. Dark contrast still needs adjustment, and a regular grid at tile edges is visible. **Not accepted map quality.** Investigate tile-buffer overlap/draw ordering with a failing multi-tile regression before changing rendering; do not assume the grid is legitimate county geometry. Labels, per-feature min_zoom, hierarchy styling, full-view/worldwide correctness, glyph policy and P4 operation remain open gates.

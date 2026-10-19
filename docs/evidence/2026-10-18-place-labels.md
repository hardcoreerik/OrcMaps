# Bounded place labels — focused implementation evidence

User design approval: October 18, 2026. Source implementation and host checks;
not device acceptance. No device, partition, SD file, hosted firmware or
remote branch was changed for these checks.

## Implementation

`include/orcmap/labels.hpp` and `src/render/labels.cpp` provide copied UTF-8
names, deterministic bounded candidate selection, measured collision checks,
English-first naming, place-only offering, zoom filtering and rejection
counters. Pool: 128 candidates; draw limit: 64; name limit: 95 bytes. Core
offering/layout tests verify no heap allocation during the measured operations.
P4 callers must place the layout in PSRAM; integration is still pending.

`adapters/m5gfx/include/orcmap/m5gfx/label_target.hpp` uses native font metrics,
glyph checks and the private canvas, restoring font/text style on destruction.
The optional host FreeType adapter measures and draws actual glyphs at 24 px.
Both initial adapters explicitly reject scripts requiring shaping. This does
not establish complete international coverage; unsupported names are counted.

## Actual checks

Commands below ran from the OrcMaps quality worktree. Logs remain in that
working directory alongside the invoked commands, rather than in build folders.

- `cmake -S tests/host -B .local/screen-bounds`, followed by
  `cmake --build .local/screen-bounds --target orcmap_label_tests -j4` and
  `ctest --test-dir .local/screen-bounds -R '^orcmap_label_tests$' --output-on-failure`:
  **1/1 passed**. Logs: `label-place-green-config-20261018.log`,
  `label-place-green-build-20261018.log`, `label-place-green-20261018.log`.
- Optional inspector configured with `ORCMAP_INSPECT_LABELS=ON`, local matching
  FreeType headers, system FreeType runtime and `ORCMAP_TEST_FONT` pointing to
  system DejaVu Sans. `cmake --build .local/pack-inspect-labels -j4`, then
  `ctest --test-dir .local/pack-inspect-labels --output-on-failure`:
  **1/1 real-font test passed**. Logs: `label-font-config-20261018.log`,
  `label-font-build-20261018.log`, `label-font-test-20261018.log`.
- `python .local/check_label_adapters.py`: **two P4 syntax checks passed**,
  using cached SDK compiler flags for the new label adapter and core. Log:
  `label-adapter-syntax-20261018.log`. No native link or firmware rebuild.
- `python3 .local/render_label_views.py` under WSL: eight 1280x720 z8 previews
  generated from the verified global candidate, all commands/status counters
  saved in `label-world-previews-20261018.log`. Drawn counts in acceptance-list
  order: **19, 43, 34, 34, 37, 62, 39, 7**; unsupported glyphs: **0** in all
  these English-first views. Dense views exercise pool overflow. This is not
  proof that every local name is supported.

Visually inspected the eight-view contact sheet and full-resolution Eugene
preview. London, Tokyo, Nairobi, São Paulo, Delhi, Sydney and Suva names appear.
Springfield does not appear at this overview scale, where Eugene is selected;
local-detail and zoom acceptance remain open. Names are readable in the host
preview and do not overlap. Existing land/road styling is still too subdued
for final map-quality acceptance. No tile grid is visible in these previews.

Separate Eugene preview: 73 offered, 20 drawn, 21 overlap rejects, 32 offscreen,
zero unsupported/invalid/oversized names. `label-eugene-preview-20261018.log`.
Images are local generated artifacts under `.local/global-assessment/`;
`labels-world-contact.png` and `eugene-z8-labels.png` are inspection aids.
Host timings must not be reported as ESP32 performance.

## Remaining gates

Following explicit approval, the full host build and CTest run passed:
`cmake --build .local/screen-bounds -j4` and
`ctest --test-dir .local/screen-bounds --output-on-failure`: **5/5 passed**
(labels, profiles, screen bounds, tile composition and the existing engine
suite). Logs: `label-full-host-build-20261018.log` and
`label-full-host-test-20261018.log`. This was a host run, not a firmware build.

The profile classifier now also exposes a single-feature streaming entry point,
with batch classification using the same implementation. The focused profile
test first failed to compile because the API did not exist, then passed after
implementation: `cmake --build .local/screen-bounds --target orcmap_profile_tests -j4`
and `ctest --test-dir .local/screen-bounds -R '^orcmap_profile_tests$' --output-on-failure`.
Logs: `profile-stream-red-20261018.log`, `profile-stream-green-build-20261018.log`,
`profile-stream-green-20261018.log`. This later change has focused coverage;
the full-suite result above precedes it.

Remaining: consumer streaming profile and label integration;
OrcSDR dependency integration and PSRAM placement; actual P4 font coverage,
flash cost, readability and active-reception timing; boundary/style acceptance;
catalog/install/setup work and explicit release/merge approval. No overall
plan milestone is declared complete by this focused result.

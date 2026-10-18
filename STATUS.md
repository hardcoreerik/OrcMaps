# OrcMaps Current Status

Last updated: 2026-09-14

## Current development focus

Phase 0 (repo foundation, format decision) and Phase 1 (format vertical
slice, synthetic fixture) are complete. Phase 2 (rendering foundation) is
underway.

**IMPLEMENTED:** PMTiles reader, Web Mercator math, schema-agnostic MVT
decode, FeatureKind in `feature_kind.hpp`, Feature / Geometry model,
MVT→FeatureTile translation, style system, immediate `RenderTarget`,
`RenderFeatureTile`, bounded `DecompressPayload` (None/Gzip), host
framebuffer, M5GFX `DisplayTarget` (exported `orcmap/m5gfx/` headers),
  host + consumer tests, provenance registry, `PackManifest` validation,
  deterministic pack identity, `PackCatalog`, local `ResolvePack`,
`examples/generic-esp32` (no graphics framework), `examples/m5gfx`
compile proof, `examples/m5stack-tab5` (hardware-verified static
  Springfield render on Tab5), `examples/lilygo-tdisplay-s3` (hardware-
  verified ESP32-S3 Springfield demo), host pack-inspect,
`orcmap::esp_idf::FileByteSource`.

**PARTIAL:** Portable camera controls are implemented and host-tested; hardware
input bindings and viewport overzoom are not. Integer zoom is 0..31.
`tools/pack-builder` can reproduce the Springfield pack, acquire the pinned
Natural Earth 5.1.2 world-overview source bundle, and **build world-overview
PMTiles** — five candidates (z0-4 … z0-8) exist host-side as immutable
archive/manifest/checksum triplets. No cutoff is selected.

Runtime manifest JSON discovery **is** now implemented and host-tested
(`orcmap::DiscoverPacks`), and the Tab5 demo boots from whatever is in
`/sd/orcmaps` with no compiled-in manifest. On-device SHA-256 verification
is still **not** implemented: discovery checks that each archive exists and
matches the size its manifest declares, which is a cheap pairing check, not
an integrity proof.

**EXPERIMENTAL:** profile-aware feature classification supports measured
OpenMapTiles 3.16 Springfield tiles and the narrow `orcmaps-overview-1`
Natural Earth profile; neither is the final general schema.

**MEASURED (HOST):** Springfield / 97477 gzip PMTiles → 1280×720
`orcsdr-dark` PPM. **MEASURED (TAB5 HARDWARE):** same view on physical
M5Stack Tab5, current uncached cold frame **~4.4 s** (`RESULT: PASS`).
Before the no-text layer filter the same view was 11.3 s. Evidence:
`docs/evidence/TAB5_SPRINGFIELD_HARDWARE.md`. Static render only.

**PLANNED:** text labels, polygon holes, overzoom, further decoder speed.
Brotli/zstd stay unsupported.

**NOT FROZEN:** tile-content schema / stable FeatureKind mapping. Two
schema ids now exist in real built packs — `openmaptiles-3.16`
(Springfield detail) and `orcmaps-overview-1` (Natural Earth overview
candidates). Neither is final. Do not finalize OpenMapTiles, Shortbread,
`orcmaps-overview-1`, or a custom schema from these extracts.

## Repository / branch state

- `hardcoreerik/orcmaps`, `main` branch.
- Local working copy: `F:\Ai\OrcMaps`.
- Treat `git log` / `git status` as authoritative over this paragraph.
  MVT decoder, `examples/generic-esp32`, and `adapters/m5gfx` are already
  on `origin/main`. This file's job is to describe HEAD honestly, not to
  recap one session.
- OrcSDR's own branch (`grok/orcmap1-readable-map`, worktree
  `F:\Ai\OrcSDR-Temp\OrcSDR-orcmap1-readable-map`) is **untouched** by this
  work — no integration has started, per `ROADMAP.md` Phase 4 not begun.

## What works today

- `orcmap::PmTilesReader` opens a PMTiles v3 archive, parses its header and
  root directory (via `DecompressPayload`), and looks up individual tiles
  by z/x/y via Hilbert tile-ID addressing, including leaf-directory
  indirection and sparse/missing-tile handling — verified byte-exact
  against a fixture built with the official reference implementation.
  `GetTile()` returns archive bytes as stored. `DecompressPayload()`
  (`include/orcmap/compression.hpp`) inflates kNone/kGzip with a caller
  output budget; kBrotli/kZstd/kUnknown fail. Directories and metadata
  use the same helper. Host-tested through a gzip-compressed MVT fixture
  all the way to host framebuffer pixels.
- `orcmap::LatLonToTile`/`TileToLatLon`/etc. — standard Web Mercator tile
  math, antimeridian-safe, Mercator-latitude-clamp-safe.
- `orcmap::Viewport` camera controls — center, integer zoom, geographic pan,
  viewport size, visible bounds, projection/unprojection, anchor-preserving
  zoom, and two complementary framing calls: `FitBounds` (frame a pack's whole
  coverage area) and `FillBounds` (fill the screen with map data). Both derive
  centre and zoom from the caller's viewport size, so an application supplies
  only its screen dimensions — no per-board centre or zoom constant. The Tab5
  demo binds touch drag, zoom buttons, style and info to these calls.
- `orcmap::MapStyle` + 4 built-in styles (`orcsdr-dark`, `standard-light`,
  `high-contrast-field`, `night-red-safe`) + `ResolveFeatureStyle()` +
  `StyleManager` runtime switching + `RenderedTileCacheKey`. The LilyGO demo
  selects any built-in offline through `/orcmaps/style.txt`; only its
  `orcsdr-dark` result is hardware-measured so far.
- `orcmap::PackManifest` / `PackCatalog` / `ResolvePack` validate immutable
  pack metadata and deterministically choose one eligible local basemap by
  coverage, zoom, priority, then identity. No network fallback exists.
- `orcmap::AttributionInfo` / `orcmap::MapSourceInfo` /
  `CollectRequiredAttribution()` — the runtime attribution API shape
  (`include/orcmap/attribution.hpp`, `include/orcmap/map_source.hpp`),
  host-tested. Runtime discovery now parses each pack's declared credits
  into `PackManifest::attribution`, and the Tab5 demo displays them from
  there, so attribution is no longer firmware-hardcoded. Nothing populates
  the separate `MapSourceInfo` aggregate yet — that remains the API a
  future `MapEngine` will feed.
- Data provenance registry: 9 reviewed datasets under `data/sources/*.json`
  (Natural Earth, OpenStreetMap, U.S. Census TIGER/Line, NOAA ETOPO, USGS
  National Map (deliberate non-approval placeholder), USDOT NAD, Overture
  Places, geoBoundaries gbOpen, Google Open Buildings), validated by
  `tools/check_data_provenance.py`. **4 of 9 are `CONFIRMED`/approved for
  official use** (Natural Earth, OpenStreetMap, geoBoundaries gbOpen,
  Google Open Buildings); the other 5 are `REVIEW_REQUIRED` pending
  primary-source verification or (for Overture) per-record license
  filtering — see each record's `notes` field and `ROADMAP.md` "Deferred
  work" for exactly what's outstanding.
- `tests/consumer/` — a second, independent CMake project proving a
  public-headers-only build works (structurally, via CMake include-path
  visibility, not just as a documented rule).
- Documentation Truth CI (`tools/check_documentation_truth.py`) and its
  sibling, Data Provenance Truth CI (`tools/check_data_provenance.py`) —
  both passing against this repo, both with their own unit test suites.
- `orcmap::DecodeMvtTile()` (`include/orcmap/mvt.hpp`,
  `src/tiles/mvt_decoder.cpp`) — decodes real MVT vector-tile bytes
  (layers, features, geometry, typed attributes) via a hand-rolled minimal
  protobuf reader, no protobuf library dependency. Schema-agnostic by
  design: it does not yet know how to turn a decoded feature into an
  `orcmap::FeatureKind` — that mapping is still blocked on the tile
  content schema decision (`docs/FORMAT_DECISION.md` "Deferred").
  Host-tested against a real MVT fixture built with the `mapbox-vector-tile`
  reference encoder.
- `examples/generic-esp32` — ESP-IDF compile/link smoke test of the
  portable core (geo math + style resolve logged at runtime). Built
  against ESP-IDF 6.0.2, target `esp32p4`. **Does not depend on M5GFX or
  M5Unified.** Does not open a pack, decode MVT at runtime, or draw
  pixels. Not a CI gate.
- `orcmap::Feature` / `Geometry` / `FeatureTile` (`include/orcmap/feature.hpp`)
  — format-independent geometry + properties. `TranslateMvtToFeatureTile()`
  deep-copies MVT into owned Feature data. Does not assign `FeatureKind`.
  Geometry type lives only on `geometry.type`. `FeatureKind` lives in
  `feature_kind.hpp` (Feature does not include style.hpp).
- `RenderFeatureTile` + `RenderTarget` + `orcmap::host::FramebufferTarget`
  — FeatureTile → ResolveFeatureStyle → Viewport → pixels, no M5GFX.
  Unclassified features skipped. Every ring of a polygon is filled even-odd,
  so multipolygon parts are all drawn and holes are subtracted.
- Host test suite: **179 test functions, all passing**.
- `EnumerateVisibleTiles` (unique TileIds, X wrap, Y clamp).
  `TileScreenMap` uses shortest wrapped X delta. Overzoom still rejected.
- `RenderTarget::DrawLine` takes `width_px` from `MapPaint`. Shared
  stroke helper (`include/orcmap/stroke.hpp`). Label FeatureKinds are
  skipped (no placeholder dots).
- Host pack-inspect can open a real gzip PMTiles archive, dump layer
  evidence, and render a 1280×720 Springfield preview through
  `GetTile` → `DecompressPayload` → `DecodeMvtTile` →
  `TranslateMvtToFeatureTile` → experimental classify →
  `RenderFeatureTile`. HOST-ONLY. Pack and PPM live in gitignored
  `data/local/`.

## What is partially working

- **`orcmap::experimental::AssignFeatureKinds` — EXPERIMENTAL.** Extended
  from measured OpenMapTiles 3.16 Springfield tiles (see mapping in
  `src/tiles/mvt_classify_experimental.cpp`). Unrecognized layers stay
  `kind_assigned == false`. Not the production tile schema.
- **`adapters/m5gfx/` — optional exported DisplayTarget.** Header-only
  `orcmap/m5gfx/display_target.hpp` implements `RenderTarget` over
  `lgfx::v1::LovyanGFX&`. Core `REQUIRES ""`; consumers that want the
  adapter supply their own M5GFX. No MVT types, no map semantics. Opaque
  RGB565 (`Color.a` ignored). `examples/generic-esp32` still does not
  depend on M5GFX/M5Unified. Tab5 hardware-verified via
  `examples/m5stack-tab5`.
- The PMTiles reader has now opened a real 141-tile gzip archive
  (Springfield / 97477). Directories still fit in the root (no leaf
  hop on this pack). Synthetic fixtures remain the CI path.
- The MVT decoder has now decoded real urban tiles (thousands of
  features, mixed geom types). Hole rings are still not subtracted.
- Host-only regional pack production can extract bbox/GeoJSON regions from an
  existing PMTiles archive and emit an immutable archive/manifest/checksum
  triplet. Springfield can also be regenerated from OSM PBF via Planetiler.
  This is provisioning tooling, not runtime discovery or a remote service.
- Host-only Natural Earth acquisition pins and verifies 21 official archives
  covering seven layers at 110m, 50m, and 10m. Local archives, shapefiles,
  `SOURCE.json`, and `SHA256SUMS.txt` remain under gitignored `data/local/`.

- **World-overview packs are built (host-only, gitignored).** Five candidates
  under `data/local/world-overview/build/`, each an immutable
  archive/manifest/checksum triplet: z0-4 871,343 B; z0-5 1,492,862 B;
  z0-6 4,833,728 B; z0-7 9,737,500 B; z0-8 17,165,758 B. Built with
  Planetiler 0.10.2 + go-pmtiles 1.28.2 from the pinned Natural Earth 5.1.2
  bundle; `pack_class: clean`, sole `provenance_id: natural-earth` (a
  `CONFIRMED` record), per-input SHA-256 recorded in each manifest. They
  declare `schema_version: orcmaps-overview-1` — a second schema alongside
  the Springfield pack's `openmaptiles-3.16`, and equally **not frozen**.
  Host renders exist at z4/z5/z6 for world, North America, Pacific
  Northwest, and Oregon views. **No render timings were captured**, no
  z0-10 candidate was generated, no cutoff was chosen, and no overview pack
  has been rendered on device. Size/embedding analysis lives in
  `docs/ORCMAPS_EMBEDDED_WORLD_FIRMWARE_CONCEPT.md` (status PLANNED/DEFERRED).
  Host measurement detail: `docs/evidence/WORLD_OVERVIEW_HOST_MEASUREMENTS.md`.

## What is being worked on

World-overview pack generation and the size/cutoff question. Five
candidates are built and visually rendered on host; the open work is
timing them, picking a cutoff, and deciding whether a z0-4-class pack gets
embedded in firmware flash (`docs/ORCMAPS_EMBEDDED_WORLD_FIRMWARE_CONCEPT.md`,
PLANNED/DEFERRED — explicitly *not* to displace the SD-card path).

Previously (complete, not in motion): the Springfield demo runs on a
physical LilyGO T-Display-S3 Touch with SD Shield, flashed through COM13,
rendering `/orcmaps/springfield.pmtiles` in a corrected 320x170 landscape
view. Exact serial timing was not captured; the user observed power-to-map
as very fast. That firmware preserves each result as
`/orcmaps/orcmaps-report-NNNN.txt` and shows the frame total on screen;
the first confirmed numbered-report run displayed **1,037 ms**.

## Current blockers

- Brotli/zstd tile compression is unsupported (`DecompressPayload` fails).
- Viewport enumerates visible tiles and wraps X; overzoom is still
  rejected. Polygon holes are not subtracted. Text labels are not drawn.
- Tab5 current cold frame ~4.4 s uncached (decode+translate ~2.0 s,
  render ~1.24 s, storage ~0.61 s). No labels / pan / cache / overzoom.
- Several data sources are blocked on primary-source verification: NOAA
  ETOPO's ISO metadata page returned HTTP 503, USDOT NAD's disclaimer page
  returned HTTP 403 (twice, two mirrors) — both need a direct re-fetch
  before those records can move from `REVIEW_REQUIRED` to `CONFIRMED`.
  These do **not** block Phase 2/3 (Lane County can use the already-
  `CONFIRMED` `openstreetmap` record).

No longer blockers (corrected 2026-09-13): ESP-IDF 6.0.2 is present and
`examples/generic-esp32` has compiled/linked the portable core for
`esp32p4`. M5GFX headers are not required for core work; the m5gfx
adapter is an optional exported include surface.

## Known bugs

None currently — the one found (unsigned-integer underflow in
`LatLonToTile` when a point clamps to exactly the Mercator latitude limit,
causing `std::min` on wrapped-around unsigned values to pick the wrong
tile row) was caught by `tests/host/test_geo.cpp` and fixed in
`src/core/geo.cpp` (clamp in floating point before casting to `uint32_t`)
in an earlier part of this same session. Documented in `ARCHITECTURE.md`
"Projection / coordinates" as a cautionary note for anyone touching that
function again.

## Current performance measurements

HOST-ONLY Springfield / 97477 numbers are in `docs/PERFORMANCE.md`.
A busy z14 tile: ~0.6 ms gzip decompress, ~2.2 ms MVT decode, ~0.7 ms
FeatureTile copy, ~0.2 ms render; the current quiet 1280×720 z14 host
frame is ~75 ms. These are **not** ESP32 numbers. Physical Tab5 results
are ~4.4 s for the same viewport. The LilyGO ESP32-S3 frame total is
**1,037 ms**. Full power-to-map and stage timings have not been transcribed.

World-overview candidates have **measured sizes only** (see "What is
partially working"). Their host renders were produced but never timed, and
`docs/PERFORMANCE.md` records no overview numbers — do not quote an
overview render time until one is actually measured.

## Test status

Host engine tests:

```
cd F:\Ai\OrcMaps
cmake -S tests/host -B build -G "Visual Studio 18 2026"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Result as of this writing: `100% tests passed, 0 tests failed out of 1`
(one `ctest` entry, `orcmap_host_tests`, itself running 179 test functions
covering geo math, PMTiles, style, attribution, MVT, Feature/MVT
translation, Viewport, clip, host render, compression, and experimental
OpenMapTiles-like classification — see `ARCHITECTURE.md`
"Testing architecture").

LilyGO hardware-demo build:

```
. C:\Espressif\frameworks\esp-idf-v5.5.4\export.ps1
cd examples/lilygo-tdisplay-s3
idf.py set-target esp32s3
idf.py build
```

Result: build PASS; `orcmap_lilygo_tdisplay_s3.bin` is 381,824 bytes.
COM13 flash hashes verified. Physical display/SD/render: PASS after the
landscape rotation correction. The SD-report firmware also flashed with
verified hashes. The numbered report and display result passed physically;
the on-screen frame total was 1,037 ms. Detailed values have not been
transcribed from the card.

Consumer smoke test (separate CMake project):

```
cmake -S tests/consumer -B build-consumer -G "Visual Studio 18 2026"
cmake --build build-consumer --config Release
ctest --test-dir build-consumer -C Release --output-on-failure
```

Result: `100% tests passed, 0 tests failed out of 1`.

Documentation and provenance checkers (from repo root, no CMake needed):

```
python tools/check_documentation_truth.py
python -m unittest discover -s tests -p 'test_documentation_truth.py'
python tools/check_data_provenance.py
python -m unittest discover -s tests -p 'test_data_provenance.py'
```

Results as of this writing: both checkers report `0 errors` against the
real repository; 24 documentation-truth unit tests pass, 34
data-provenance unit tests pass. These Python counts **are** now verified
by Documentation Truth (`_check_python_test_count`), which recounts every
`tests/test_*.py` suite and fails on a stale number. That check exists
because this line silently said 20 against an actual 28 until
2026-09-14 — the host C++ count check did not cover Python suites.

Two further Python suites exist — `tests/test_regional_pack_builder.py`
(4 tests) and `tests/test_world_overview_acquisition.py` (5 tests). Their
counts are verified whenever a doc cites them, but **neither suite is run
by any CI workflow**: `documentation-truth.yml` and `data-provenance.yml`
each discover only their own single `-p` pattern. Those 9 tests are
local-only today.

## Integration status

Not integrated with OrcSDR. `idf_component.yml` declares version `0.2.3`
(`v0.2.0` was the first tagged release; see `CHANGELOG.md`); the contract OrcSDR
integrates against is `docs/ORCSDR_INTEGRATION.md`. The in-tree `examples/generic-esp32` consumes the
component via `EXTRA_COMPONENT_DIRS` (local convenience, not the
version-pinned `git:` + SHA pattern OrcSDR will use). `idf_component.yml`'s
declared ESP-IDF floor is `>=5.0` — deliberately broad, so it doesn't
conflict with OrcSDR's own `>=5.5.0,<5.6.0` pin. OrcMaps is developed and
tested against the latest ESP-IDF 6.x (currently v6.1) as its own
toolchain target, which is a separate thing from that declared floor —
see `PROJECT_TRUTH.md` "Compatibility Goals" for why the two are kept
distinct. No ESP-IDF version conflict currently blocks Phase 4. The intended
pin pattern (OrcSDR's `git:` URL + full commit-SHA, matching its existing
`esp_rtl_sdr` dependency) is documented in `PROJECT_TRUTH.md` "Public API
and Versioning" but not yet used anywhere, since there's no OrcSDR-side
manifest entry to point at OrcMaps yet.

## Most recent important decisions

1. Container format: PMTiles v3, decided with citations
   (`docs/FORMAT_DECISION.md`).
2. Repo/product naming: repo `hardcoreerik/orcmaps`, product "OrcMaps",
   component/namespace `orcmap`, pack extension `.pmtiles` (not a custom
   extension).
3. CMake layout copies `esp-rtl-sdr`'s exact convention (root
   `CMakeLists.txt` = ESP-IDF component, `tests/host/CMakeLists.txt` =
   separate standalone host project) rather than inventing a new layout.
4. miniz 3.1.2 (`77d0dce8627735138c51770d1799a1ef48f2117d`) vendored as
   the full source snapshot under `third_party/miniz`; only inflate is
   compiled. MIT-licensed, recorded in `docs/DEPENDENCY_LEDGER.md`.
5. Style system built ahead of the original phase ordering, per explicit
   request; `MapStyle`/`ResolveFeatureStyle` API is considered stable
   enough for a renderer to build against.
6. Documentation Truth CI added, adapted from OrcSDR's own. Running it
   against this repo for the first time found and fixed real drift (a
   stale test-count claim, misplaced doc path references).
7. Local worktree convention set: `F:\Ai\OrcMaps-Temp\`, matching OrcSDR's
   `F:\Ai\OrcSDR-Temp\` pattern.
8. **IP/provenance safety model established as a durable `PROJECT_TRUTH.md`
   principle**, with a real enforcement mechanism: an 11-value license-class
   enum, three map-pack policy classes (Clean/Permissive/Open), a JSON
   provenance registry (`data/sources/`), and Data Provenance Truth CI
   (`tools/check_data_provenance.py`, mirroring Documentation Truth's
   deterministic-stdlib-only design). Nine starter records reviewed; 4 are
   `CONFIRMED`/approved today — the checker actively prevents the other 5
   from being used officially until resolved, which is the system working
   as intended, not a gap.
9. Code-vs-data license separation formalized architecturally, not just in
   prose: `orcmap::AttributionInfo`/`MapSourceInfo` establish the runtime
   API shape without forcing a full `MapEngine` implementation that
   doesn't exist yet.
10. Public API boundary made structurally enforced (`tests/consumer/`),
    not just documented — a build failure now results if consumer code
    reaches into `src/`/`third_party/` internals.
11. `CONTRIBUTING.md` added with a DCO-style (Developer Certificate of
    Origin) contribution certification, explicitly flagging that a formal
    CLA + attorney review is the right next step if contribution volume
    ever outgrows this lightweight approach — not adopted preemptively.
12. ESP-IDF compatibility: `idf_component.yml` floor stays broad
    (`>=5.0`, satisfied by OrcSDR's current `>=5.5.0,<5.6.0` pin); the
    latest ESP-IDF 6.x (v6.1) is OrcMaps' own dev/CI toolchain target, a
    deliberately separate concept — see `PROJECT_TRUTH.md` "Compatibility
    Goals". Confirmed with the user: OrcSDR is expected to eventually port
    to the latest ESP-IDF itself; this floor is not expected to be raised
    to chase our own dev toolchain.
13. MVT decoder: hand-rolled minimal protobuf reader
    (`orcmap::DecodeMvtTile()`), not a library — a general-purpose
    protobuf parser was judged not worth the footprint for four fixed
    message shapes. Schema-agnostic by design; recorded in
    `docs/DEPENDENCY_LEDGER.md` "Resolved: MVT decoding".
14. Core graphics independence: M5GFX is the first *reference
    integration*, not the renderer architecture. `examples/generic-esp32`
    must not depend on M5GFX/M5Unified. If `adapters/m5gfx/` were deleted,
    core must still build and host-test.
15. GitHub Actions today are Documentation Truth + Data Provenance Truth
    only. Host C++ tests, consumer smoke test, ESP-IDF compile, sanitizers,
    and fuzzing are **not** CI gates yet.
16. The LilyGO T-Display-S3 uses the existing generic M5GFX
    `DisplayTarget`; only board bring-up and its 1-bit SDMMC Shield mount
    belong in the example. Touch input is outside the first static test.

## Next 3-7 actions

1. Capture host render timings for the five existing overview candidates
   (sizes and visual renders exist; timings were never recorded), then
   choose a cutoff. Building them is done; the decision is not.
2. Read a numbered `/orcmaps/orcmaps-report-NNNN.txt` file and record the
   exact stage timings.
3. Render an overview pack on device — no ESP32 has drawn one yet.
4. Continue the ESP32-P4 decode path after the LilyGO baseline exists.
5. Do not freeze `openmaptiles-3.16` or `orcmaps-overview-1` as the OrcMaps
   schema without review. Two schemas now exist; neither is final.

Do not do this tranche: OrcSDR integration, a second graphics framework,
overlay application features, pack marketplace UI.

Ongoing discipline (not a one-time action): keep both
`tools/check_documentation_truth.py` and `tools/check_data_provenance.py`
passing on every change — run them locally before committing, same
workflow as their CI counterparts.

## Files / areas currently in motion

World-overview pack generation (built, un-timed, no cutoff) and the
project-status documents. `docs/ORCMAPS_EMBEDDED_WORLD_FIRMWARE_CONCEPT.md`
is new and holds the firmware-embedding direction as PLANNED/DEFERRED. Tab5
and LilyGO hardware evidence remain unchanged. OrcSDR integration has not
started.

## Notes for the next development session

- Read `PROJECT_TRUTH.md` first for durable constraints, then this file
  for what's actually done, then `ROADMAP.md` Phase 2 for what's next.
  If those three disagree with the code, the code wins — then fix the
  docs in the same change.
- The synthetic test fixture (`tests/fixtures/tiny.pmtiles`) is
  deliberately not real map data — don't try to "improve" it into a real
  map; build a real archive separately for Phase 3, and keep the synthetic
  fixture as the fast, deterministic container-format regression test it's
  designed to be.
- Before adding any new third-party **code** dependency, update
  `docs/DEPENDENCY_LEDGER.md` first. Before using any new **data** source,
  add a `data/sources/*.json` record first and make sure
  `tools/check_data_provenance.py` passes — see `CONTRIBUTING.md`.
- Don't re-litigate the PMTiles-vs-MBTiles-vs-custom-format decision
  without new evidence — see `PROJECT_TRUTH.md` "Current Technology
  Direction". Same for the provenance/licensing model — see "IP and
  Provenance Safety Model" in the same file.
- A `REVIEW_REQUIRED` provenance record is not a bug to quietly "fix" by
  flipping its confidence to `CONFIRMED` without actually re-verifying the
  primary source. That defeats the entire point of the system.

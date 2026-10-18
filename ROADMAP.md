# OrcMaps Roadmap

Where we're going and in what order. Checkboxes reflect actual repository
state as of 2026-09-13 (see `STATUS.md` for the live snapshot; this file
changes on phase completion/priority shifts, not every session).

## Current objective

Get one real map pack (Lane County) rendering on Tab5 hardware through
OrcMaps, end to end: PMTiles archive on SD → decoded vector tile → styled
draw call → pixels on screen → pan/zoom. Nothing else matters more than
this until it's true, per the project's own "don't overengineer the first
milestone" principle (`PROJECT_TRUTH.md`).

## Phase 0 — Repository / architecture foundation

**Goal:** stand up the standalone repo with a real, evidence-based format
decision and a portable, host-tested core.

- [x] Repository created (`hardcoreerik/orcmaps`, public, AGPL-3.0)
- [x] Directory scaffold, LICENSE, LICENSING.md, README
- [x] ORCMAP1 audit (`docs/ORCMAP1_AUDIT.md`) — what to keep/replace
- [x] Format research: PMTiles v3 spec, MBTiles, yuiseki ESP32-S3 precedent
- [x] Format decision recorded with citations (`docs/FORMAT_DECISION.md`)
- [x] Data/licensing policy (`docs/DATA_AND_LICENSING.md`), dependency
      ledger (`docs/DEPENDENCY_LEDGER.md`)
- [x] Four-document project-memory system (this file + `PROJECT_TRUTH.md` +
      `ARCHITECTURE.md` + `STATUS.md`)
- [x] Documentation Truth CI (`.github/workflows/documentation-truth.yml` +
      `tools/check_documentation_truth.py`, adapted from OrcSDR's own,
      with unit tests in `tests/test_documentation_truth.py`)
- [x] IP/provenance safety model recorded as a durable principle in
      `PROJECT_TRUTH.md`, with code-vs-data-license separation formalized
      architecturally (`include/orcmap/attribution.hpp`,
      `include/orcmap/map_source.hpp`)
- [x] Data-license classification system + Clean/Permissive/Open map-pack
      classes defined (`docs/DATA_AND_LICENSING.md`,
      `docs/DATA_PROVENANCE_REGISTRY.md`)
- [x] Data provenance registry (`data/sources/*.json`, 9 starter records)
      + Data Provenance Truth CI
      (`.github/workflows/data-provenance.yml` +
      `tools/check_data_provenance.py`, `tests/test_data_provenance.py`)
- [x] `CONTRIBUTING.md` with a DCO-style contribution/relicensing policy
- [x] Consumer integration test (`tests/consumer/`) proving a public-
      headers-only build works, structurally enforced via CMake include
      visibility
- [x] Pack manifest schema, portable C++ data model/validation, deterministic
      identity, catalog, and local resolver implemented and host-tested

**Exit criteria:** met.

## Phase 1 — Map format vertical slice

**Goal:** prove the chosen container format works, in code, against a real
(if synthetic) archive — not just on paper.

- [x] `orcmap::ByteSource` abstraction + host `FileByteSource` adapter
- [x] `orcmap::PmTilesReader`: header parse, directory parse (gzip via
      vendored miniz), Hilbert tile-ID addressing, leaf-directory
      indirection, tile byte-range lookup
- [x] Deterministic, small, committed test fixture
      (`tests/fixtures/tiny.pmtiles`, 519 bytes) built via the official
      reference `pmtiles` Python library, not hand-rolled
- [x] Host tests: header fields, byte-exact tile content, sparse/missing
      tiles, corrupt-archive handling, Hilbert ID correctness — all passing
- [ ] Same proof against a **real** OSM-derived `.pmtiles` archive (not the
      synthetic fixture) — not yet done, blocks Phase 4

**Exit criteria:** met for the synthetic-archive proof; real-archive proof
deferred to Phase 4 (needs an actual OSM extract pipeline, Phase 4's job).

**Risks:** the synthetic fixture doesn't exercise leaf-directory
indirection (the fixture's directory fits in the root) or gzip-compressed
*tile* payloads (only the directory is gzip-compressed in the fixture,
tile payloads are stored uncompressed) — a real archive may exercise code
paths the current tests don't. Flagged, not yet mitigated.

## Phase 2 — Rendering foundation

**Goal:** decode and draw one real tile.

- [x] Geo/tile math (`geo.hpp`/`geo.cpp`), Web Mercator, host-tested,
      including a real bug (unsigned underflow at the Mercator latitude
      limit) caught and fixed by tests
- [x] Style system (`style.hpp`, `color.hpp`, `cache_key.hpp`,
      `style.cpp`) — done ahead of the original phase order because it was
      explicitly requested mid-session; does not block the items below, but
      means "Style System" is no longer its own later phase (folded in here)
- [x] MVT (vector tile) decoder — schema-agnostic container decode
      (`orcmap::DecodeMvtTile()`), hand-rolled minimal protobuf reader,
      host-tested against a real MVT fixture. **Not yet done: mapping
      decoded features to `orcmap::FeatureKind` for rendering** — that's
      the tile-content-schema decision (`docs/FORMAT_DECISION.md`
      "Deferred"), still open, and now the actual next blocker for a
      renderer rather than the decoder itself
- [x] Bounded tile-payload decompression (`DecompressPayload`, kNone/kGzip).
      Brotli/zstd return false. GetTile remains raw stored bytes.
- [x] `adapters/esp_idf` `FileByteSource` (FILE* on a mounted VFS path;
      buffered; no SD mount, no Tab5, no `_IONBF`)
- [x] `examples/m5stack-tab5` hardware-verified static Springfield
      render on physical Tab5 (`docs/evidence/TAB5_SPRINGFIELD_HARDWARE.md`).
- [x] `examples/lilygo-tdisplay-s3` hardware-verified static Springfield
      render on the T-Display-S3 Touch + SD Shield
      (`docs/evidence/LILYGO_TDISPLAY_S3_SPRINGFIELD_HARDWARE.md`).
- [x] OrcMaps-owned Feature / Geometry model, and an explicit MVT →
      OrcMaps translation boundary (`feature.hpp`, `TranslateMvtToFeatureTile`).
      MVT types must not become the renderer architecture. **EXPERIMENTAL**
      FeatureKind heuristic exists; the real schema is still deferred.
- [x] Graphics-independent renderer seam: immediate `RenderTarget` +
      `RenderFeatureTile` + host framebuffer proof. M5GFX consumes this
      seam via `DisplayTarget`.
- [x] Viewport: prepared `TileScreenMap`, `EnumerateVisibleTiles`, X
      wrap / Y clamp. Overzoom still rejected. Zoom 0..31.
- [x] `adapters/m5gfx` `DisplayTarget` (`RenderTarget` over
      `lgfx::v1::LovyanGFX&`, Color→RGB565, shared line clip). No MVT.
      `examples/m5gfx` is an ESP-IDF compile proof, not a Tab5 product
      demo.
- [ ] Tile content schema decided (`docs/FORMAT_DECISION.md` "Deferred").
      Springfield / 97477 OpenMapTiles 3.16 tiles are now MEASURED host
      evidence; the mapping in `experimental::AssignFeatureKinds` is still
      EXPERIMENTAL and must not be treated as the schema.
- [x] Renderer core (`src/render/renderer.cpp`) that walks OrcMaps
      features (not `MvtFeature`), calls `ResolveFeatureStyle()`, issues
      generic draw operations. Host proof only; not a full map engine.
- [x] Minimal framework-independent Viewport (prepared TileScreenMap).
      Overzoom still rejected.
- [x] First ESP-IDF *compile/link* of the OrcMaps component:
      `examples/generic-esp32` built against ESP-IDF 6.0.2, target
      `esp32p4`. Smoke test only (geo + style log lines). Not on-device
      functional validation, not a graphics demo, not in CI.

**Dependencies:** Phase 1 (reader) must keep working; style system (done)
feeds the renderer once it exists.

**Exit criteria:** host-side real-tile render + measured times: **met**
(Springfield / 97477, 1280×720, `docs/PERFORMANCE.md`). Tab5 / SD path
is **met** as a static hardware-verified frame (Tab5 current ~4.4 s
uncached; 11.3 s was the pre-filter measurement). Pan/zoom
and labels are not met.

## Phase 3 — Lane County / Eugene proof

**Goal:** the actual vertical slice success criteria from the original
project brief — pan, zoom, markers, ADS-B/LoRa overlays, bounded memory,
OrcSDR still builds.

- [x] HOST-ONLY evidence: Springfield / 97477 gzip PMTiles from Geofabrik
      Oregon via Planetiler 0.10.2, rendered 1280×720 through the real
      engine (`tools/pack-inspect`, `docs/PERFORMANCE.md`). Not a Lane
      County product pack, not on-device, schema not frozen.
- [ ] Real Lane County `.pmtiles` archive built from an OSM `.osm.pbf`
      extract (Geofabrik or similar) via a proper tiler (Planetiler/
      tippecanoe) — not the Overpass-JSON or incomplete-GeoJSON scripts
      documented in `docs/ORCMAP1_AUDIT.md` §8
- [x] Real Springfield golden-pack manifest and SHA-256 sidecar produced
      alongside it, following
      `docs/PACK_MANIFEST_SCHEMA.md`, referencing the `openstreetmap`
      provenance record (already `CONFIRMED`/approved) by id
- [x] Noninteractive regional PMTiles bbox/GeoJSON extraction emits an
      immutable archive + manifest + SHA-256 triplet; Springfield OSM/PBF
      regeneration emits the same sidecars
- [x] `tools/check_data_provenance.py` extended to validate manifest
      `sources[].provenance_id` references, per `docs/PACK_MANIFEST_SCHEMA.md`
      references and official/Clean pack eligibility
- [x] Portable pan changes geographic viewport state and visible tiles
- [x] Portable integer zoom, fit bounds, visible bounds, coordinate projection,
      and anchor-preserving screen-point zoom are host-tested
- [ ] Hardware touch/button bindings exercise pan and zoom on a real pack
- [ ] A generic marker overlay draws correctly at a lat/lon
- [ ] Memory measured and bounded (peak RAM, peak PSRAM logged)
- [ ] OrcSDR still builds and runs, unmodified, throughout (OrcMaps
      development must never break the working branch —
      `PROJECT_TRUTH.md` principle 13/`docs/ORCMAP1_AUDIT.md`)

**Dependencies:** Phase 2 renderer working.

## Phase 4 — OrcSDR integration

**Goal:** OrcSDR consumes OrcMaps as an external dependency; ADS-B and LoRa
basemaps migrate off `offline_map.cpp`.

- [ ] OrcSDR's `idf_component.yml` gains a version-pinned dependency on
      `hardcoreerik/orcmaps`, in the exact `git:` URL + full commit-SHA
      shape already used for `esp_rtl_sdr` (`PROJECT_TRUTH.md` "Public API
      and Versioning") — never a branch or floating tag
- [ ] `tests/consumer/` extended/mirrored as needed once OrcSDR is the
      real consumer, so drift between "what the smoke test proves" and
      "what OrcSDR actually needs" gets caught before it becomes an
      integration surprise
- [ ] ADS-B basemap migrated (preserve the existing sprite-cache pattern in
      `adsb_dashboard.cpp`; ADS-B keeps owning aircraft state and its own
      polar-math overlay placement — see `docs/ORCMAP1_AUDIT.md` §5)
- [ ] LoRa basemap migrated (preserve `follow_selected_position()` and
      the existing use of the map's own projection for node placement)
- [ ] `offline_map.cpp`/`.hpp` deleted **only after** both migrations are
      demonstrated working — never before
- [ ] OrcSDR Dark style set as OrcSDR's default at its integration point
      (`map.SetStyle(orcmap::BuiltinStyle::kOrcSdrDark)`), per
      `docs/STYLING.md`

**Note (resolved 2026-09-13, previously recorded here as a risk):**
OrcMaps' declared ESP-IDF floor is `>=5.0` — deliberately broad, so it
does not block on OrcSDR's own `>=5.5.0,<5.6.0` pin. See
`PROJECT_TRUTH.md` "Compatibility Goals" for the distinction between that
floor and the newer toolchain (latest ESP-IDF 6.x) OrcMaps is actually
developed/tested against. No ESP-IDF version conflict currently blocks
this phase on that basis.

## Phase 5 — Full-screen map application

**Goal:** OrcSDR gets a dedicated, generic full-screen map browser.

- [ ] Drag-to-pan, touch zoom
- [ ] Current center/zoom display, center-on-receiver, center-on-selected
- [ ] Marker placement (tap/long-press), naming, delete, recenter
- [ ] Visible attribution, pack info — consumes
      `orcmap::CollectRequiredAttribution()` (`include/orcmap/map_source.hpp`,
      already implemented/tested) once real `MapSourceInfo` values exist
      to feed it
- [ ] Graceful "no data for this area/zoom" state
- [ ] Map Style switcher wired into Settings → Maps (per
      `docs/STYLING.md` "OrcSDR-specific styling guidance" — deferred until
      now specifically because the ask was to get the underlying API right
      first, not to build UI early)

## Phase 6 — Pack management

**Goal:** discover and validate local packs. Optional transfer tools may use
SD copy or a network during provisioning, but runtime never requires either
network access or a remote service.

- [ ] On-device pack discovery: directory scan of installed `.pmtiles`
      files (not the fixed-slot catalog table pattern — see
      `docs/PACK_FORMAT.md`)
- [x] Portable manifest/catalog/resolver core selects exactly one eligible
      installed basemap and returns missing when no local pack qualifies
- [ ] Runtime JSON manifest ingestion and streamed SHA-256 verification
- [ ] Generalize OrcSDR's `catalog_sync` streaming/atomic-activate/rollback
      machinery for map-pack scale (chunked, resumable, per-chunk hash)
- [ ] Settings → Data & Maps → Maps UI: Installed/Available/World/regions
      tree, size/coverage/zoom display before download
- [ ] `orcsdr_storage::used_bytes()` fixed (currently stubbed to always
      return 0 — `docs/ORCMAP1_AUDIT.md` §7) so real free-space checks work
      before large map downloads trust them

## Phase 7 — Regional scale testing

**Goal:** prove the same code path scales past one county.

- [ ] Oregon / Pacific Northwest pack built and benchmarked
- [ ] Tile-boundary crossing tested heavily (pan across many tiles)
- [x] Host performance numbers recorded in `docs/PERFORMANCE.md` (Springfield
      97477) and Tab5 ESP32-P4 hardware numbers recorded. LilyGO ESP32-S3
      frame total is physically verified at 1,037 ms; full stage timings
      remain on the SD report.

## Phase 8 — Global / planet scale

**Goal:** evaluate, don't assume, whole-world feasibility.

- [x] Official Natural Earth 5.1.2 source bundle acquired and verified for
      seven layers at 110m, 50m, and 10m; archives stay outside Git
- [x] Natural Earth world base candidates z0-z6/z7/z8 built and benchmarked
      through the generic host renderer; tiny=z6 and standard=z7 are measured
      recommendations, not frozen release policy
- [ ] Full-planet pack evaluated against real hardware limits (SD card
      size, FAT32 4 GiB file limit → chunking scheme, per yuiseki precedent)
- [x] Document world-overview limits found, not estimated ones

## Phase 9 — Ecosystem / external applications

**Goal:** prove OrcMaps is reusable beyond OrcSDR.

- [x] `examples/generic-esp32` — ESP-IDF compile/link smoke test of the
      portable core with **no** M5GFX/M5Unified dependency (proves a
      generic ESP-IDF app can consume OrcMaps). Not a map-rendering demo.
- [x] A fuller non-OrcSDR example that opens a pack and draws. The physical
      LilyGO T-Display-S3 demo verifies this path independently of OrcSDR.
- [ ] Stable, tagged OrcMaps releases (semver, `idf_component.yml` version
      bumps) that OrcSDR (and, ideally, at least one unrelated project)
      pin against

## Deferred work

- Tile content schema decision (general MVT vs. narrower custom) —
  still deferred. Springfield OpenMapTiles 3.16 tiles are measured host
  evidence, not a freeze. See `docs/FORMAT_DECISION.md`.
- External `.orcstyle` style files — not blocked, not started.
- Brotli/zstd tile compression support in `DecompressPayload()`.
- HTTP Range `ByteSource` (remote streaming).
- Label collision avoidance / priority rendering.
- Payment/commercial-license enforcement — explicit non-goal for now, see
  `PROJECT_TRUTH.md`.
- Resolve the `REVIEW_REQUIRED` provenance records: directly fetch and
  quote the Census TIGER/Line TechDoc's attribution clause, re-fetch
  NOAA ETOPO's ISO metadata "use constraints" page (503 during research),
  re-fetch the USDOT NAD disclaimer page (403 during research, twice),
  decide the Overture Places per-record source-filtering approach, and
  split `usgs-national-map`'s placeholder record into narrower
  dataset-specific records (e.g. `usgs-nhd`) once actually needed. None of
  these block current work — they block those specific sources becoming
  usable.

## Research questions

- What tile content schema minimizes on-device MVT decode cost without
  requiring a fully custom tiler? (Phase 3)
- What's the actual Lane County / Oregon `.pmtiles` size at a practical
  zoom range? No credible published number exists for these — must be
  measured directly (`docs/FORMAT_DECISION.md`).
- Does the ESP32-P4 + PSRAM Tab5 target comfortably beat the yuiseki
  no-PSRAM ESP32-S3 numbers, or does BLE/Wi-Fi coexistence impose similar
  heap-fragmentation constraints? (Phase 2/3)

## Release milestones

First tagged release: `v0.2.0`; current: `v0.3.0` (`idf_component.yml`
declares `version: "0.3.0"`). 0.1.0 was the untagged in-development version. What
changed is in `CHANGELOG.md`; the consumer contract is
`docs/ORCSDR_INTEGRATION.md`.

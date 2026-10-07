# OrcMaps Project Truth

This document is the canonical statement of what OrcMaps is and what has
been decided. It should let a developer or AI agent enter this repository
months from now and know what must not be accidentally re-decided or
broken. If a future prompt or PR conflicts with something stated here,
**call out the conflict explicitly and resolve it deliberately** — do not
silently overwrite project direction, and do not silently write around a
contradiction between this document and the code. See "Document authority"
at the end.

## Project Identity

- Repository: `hardcoreerik/orcmaps` (public, created 2026-09-13).
- Local clone convention: `F:\Ai\OrcMaps`.
- Product/project name: **OrcMaps**.
- Component/namespace: `orcmap` (ESP-IDF component name, C++ namespace
  `orcmap::`). Note the singular — the repo/product is "OrcMaps", the
  in-code namespace is "orcmap". This is intentional, not a typo to fix.
- Pack file extension: `.pmtiles` (standard PMTiles extension — see "Map
  format direction" below; OrcMaps does not invent its own extension).

## Mission

OrcMaps is a reusable **offline map engine for embedded devices**. ESP32-
class hardware is the first target platform, not the only intended one —
the core is written to be portable to other microcontroller-class devices
later, though no non-ESP32 port exists yet (see STATUS.md). It is not
"OrcSDR's map code moved to another repo" — it is a standalone engine that
OrcSDR happens to be the first major application of. Unrelated ESP32
projects should be able to depend on it the same way OrcSDR already depends
on `hardcoreerik/esp-rtl-sdr`.

## Relationship to OrcSDR

- OrcSDR is a consumer, not the owner, of OrcMaps.
- OrcMaps must never depend on OrcSDR headers, types, or build files.
- OrcSDR-specific domain concepts (aircraft, LoRa/Meshtastic nodes, P25
  sites, RF transmitters, bearings, heatmaps, signal detections, broadcast
  stations) must never appear inside OrcMaps' core model. See "Map Data vs
  Rendering vs Overlay Separation".
- OrcSDR will eventually depend on OrcMaps as a version-pinned external
  ESP-IDF component (`idf_component.yml`), exactly like its existing
  `esp-rtl-sdr` dependency. **Not yet wired up** — OrcSDR's branch
  `grok/orcmap1-readable-map` still uses its own in-tree `offline_map.cpp`.
- The prototype OrcMaps supersedes is documented in full in
  `docs/ORCMAP1_AUDIT.md` (ORCMAP1: text-based format, 640-segment/32-label
  caps, plate-carrée projection, hardcoded colors, used by
  `apps/orcsdr-tab5/ui/adsb_dashboard.cpp` and `lora_dashboard.cpp`). It
  proved the UI concept works; its limits are not requirements for OrcMaps.

## IP and Provenance Safety Model

This is a durable principle, not a preference — treat a change to it the
same as any other decision this document protects (see "Document
authority"):

> OrcMaps uses an IP- and provenance-safe development model. Every
> external code dependency, dataset, asset, font, icon set, schema, map
> style source, build-time tool, or other third-party material must have
> known provenance and known usage rights before it is incorporated into
> official OrcMaps source or official OrcMaps map packs. Unknown licensing
> is rejected rather than assumed safe.
>
> OrcMaps prefers public-domain and permissively licensed sources when
> practical, but properly understood open-data licenses such as ODbL are
> allowed when they materially improve the project. Their obligations must
> remain explicit and isolated.

In one sentence: **we are not banning OpenStreetMap; we are banning
ambiguity.** A dataset under a clearly-understood license with real,
recorded obligations is approvable. A dataset whose terms haven't actually
been read is not, regardless of how reputable the publisher looks —
including a `.gov` domain; see `docs/DATA_AND_LICENSING.md` "Government
data isn't automatically safe" for why agency-level trust is explicitly
rejected in favor of dataset-level review.

This principle has a real, running enforcement mechanism, not just prose:

- **Code and build-time tool provenance**: `DEPENDENCY_LEDGER.md`.
- **Map data provenance**: a machine-readable registry at
  `data/sources/*.json`, one record per reviewed dataset, validated by
  `tools/check_data_provenance.py` (CI:
  `.github/workflows/data-provenance.yml`). See
  `docs/DATA_PROVENANCE_REGISTRY.md` for the full schema and
  `docs/DATA_AND_LICENSING.md` for the plain-language policy, including the
  **OrcMaps Clean / Permissive / Open** map-pack classification system.
- **AI-assisted development is not exempt.** If an AI agent finds a GitHub
  implementation, a dataset, a font, an icon, or a fixture and wants to
  incorporate it, the identical review standard applies — "an AI generated
  it for us" is never proof the underlying source material is ours to use.
  See `docs/DATA_AND_LICENSING.md` "Contribution / AI-assisted development
  safeguard".
- **Code licensing and map-data licensing remain separate intellectual-
  property layers, always.** A map pack being ODbL does not make the
  OrcMaps engine ODbL. A public-domain map source does not make the
  OrcMaps engine public domain. A commercial OrcMaps engine license would
  not make OSM (or any other source's) data proprietary. See
  `docs/DATA_AND_LICENSING.md` "Code and data are different intellectual-
  property layers" — this is architectural, not just contractual: the
  engine reads a `MapSource`/PMTiles archive, it never becomes what it
  reads.

We do not describe any of this as "zero legal risk" or "copyright risk
free" anywhere in this project, including in casual conversation — see
`docs/DATA_AND_LICENSING.md`'s opening section for why, and the preferred
phrasing ("verified low-risk source", "approved for official OrcMaps
distribution under the documented source terms").

## Core Design Principles

1. Fully offline after provisioning — boot, installed-pack selection and
   validation, pan, zoom, rendering, projection, attribution, and overlays
   require no network. Missing tiles never trigger an online fallback.
2. One basemap, many applications — base geography is shared; every
   consumer supplies its own overlays.
3. Bounded memory — pack/archive size must never determine RAM usage.
4. Streaming/random access — read only what the current viewport needs.
5. Portable core — no application state inside the engine.
6. M5GFX first, not M5GFX forever — rendering backend and map core are
   separable.
7. Map data is not firmware — packs install/update independently.
8. User data is not map-pack data — markers/waypoints survive pack
   replacement, never leave the device as part of pack discovery.
9. Safe updates — a corrupt/partial download never replaces a working pack.
10. Provenance matters — every pack carries source/license/attribution/hash.
11. No unauthorized tile scraping — packs come from legitimate extract data
    (e.g. OSM `.osm.pbf`), never bulk-scraped raster tile servers.
12. Measure performance — do not optimize or choose formats blindly.
13. Maintain host-application responsiveness — on OrcSDR specifically, SDR
    USB/audio/DSP work takes priority over background map operations; map
    I/O never runs on a real-time thread (inherited constraint from
    ORCMAP1's `load()` contract, `docs/ORCMAP1_AUDIT.md` §2).
14. Don't overengineer the first milestone.

## Current Technology Direction

- **Map pack container: PMTiles v3, decided.** See
  `docs/FORMAT_DECISION.md` for the full evaluation (PMTiles vs. MBTiles
  vs. custom `ORCMAP2`, raster vs. vector vs. hybrid) and citations. Do not
  re-litigate this without new evidence — MBTiles was rejected specifically
  because SQLite's locking/WAL assumptions don't map cleanly onto FAT32/SD,
  and a custom container was rejected because PMTiles already gives
  bounded-memory seek+read with a real ESP32-S3 precedent
  (`yuiseki/m5-cardputer-offgrid-tiny-map`, ~78 GiB whole-planet vector
  PMTiles archive, no PSRAM).
- **MVT container decode: implemented, schema-agnostic**
  (`orcmap::DecodeMvtTile()`, `include/orcmap/mvt.hpp`,
  `src/tiles/mvt_decoder.cpp`, hand-rolled minimal protobuf reader — no
  protobuf library dependency, see `docs/DEPENDENCY_LEDGER.md` "Resolved:
  MVT decoding").
- **OrcMaps Feature / Geometry model: implemented**
  (`include/orcmap/feature.hpp` — `Point`, `Path`, `Geometry`, `Feature`,
  `FeatureTile`, `GeomType`). Format-independent; no MVT/PMTiles/M5GFX
  types. Coordinates are tile-local integers. `FeatureKind` is optional
  (`kind_assigned`) and is not implied by geometry type.
- **MVT → OrcMaps translation: implemented**
  (`orcmap::TranslateMvtToFeatureTile()`, `include/orcmap/mvt_translate.hpp`).
  Deep-copies decoded MVT into owned FeatureTile data. Does **not** assign
  `FeatureKind`.
- **Tile content schema / FeatureKind mapping: still not decided**
  (general OpenMapTiles-style MVT vs. a narrower OrcMaps-specific schema).
  An **EXPERIMENTAL** heuristic lives in
  `include/orcmap/experimental/mvt_classify.hpp` and is not the stable
  public API. Deferred to measurement against the Lane County vertical
  slice — see `docs/FORMAT_DECISION.md` "Deferred: tile content schema".
- **Rendering: vector tiles decoded once, cached as RGB565** (RAM/PSRAM
  first, optional SD-backed cache second) — the hybrid approach, matching
  the yuiseki precedent and ORCMAP1's own informal sprite-cache pattern in
  `adsb_dashboard.cpp`. Renderer core + Feature model + Viewport
  (enumerate + wrap; no overzoom)
  exist. Rendered-tile cache does not. `adapters/m5gfx/DisplayTarget` is a
  `RenderTarget`, not the renderer architecture — see STATUS.md.
- **Projection: standard Web Mercator tile math**
  (`orcmap::LatLonToTile`/`TileToLatLon`, `include/orcmap/geo.hpp`),
  implemented and host-tested. RF-specific distance/bearing/geodesic math
  stays in the consuming application (OrcSDR), never in OrcMaps.

## Map Data vs Rendering vs Overlay Separation

```
Geographic map data (.pmtiles)
        |
Base map style (orcmap::MapStyle)
        |
Rendered map (host-proof renderer; optional M5GFX DisplayTarget;
hardware-verified static Tab5 Springfield frame)
        |
Generic OrcMaps overlays (marker/icon/text/polyline/polygon/circle/route/
        |                  track/waypoint -- src/overlays, not yet implemented)
Application-specific overlays (aircraft/nodes/RF sites -- OrcSDR's own code)
```

`MapStyle` has, and must continue to have, zero fields for any OrcSDR
concept. See `docs/STYLING.md` "Overlays are NOT base-map styles" — this is
treated as a correctness rule, not a preference.

## Supported / Target Hardware

- Primary initial target: **ESP32-P4 / M5Stack Tab5**, SD-card storage,
  M5GFX rendering, 1280×720 display.
- Secondary feasibility target: ESP32-S3, kept realistic by not letting
  core code assume Tab5-specific resources (PSRAM size, display size).
  `examples/lilygo-tdisplay-s3` runs the real Springfield pipeline on a
  physical ESP32-S3R8 T-Display-S3 Touch + SD Shield and records each
  successful run in `/orcmaps/orcmaps-report-NNNN.txt` on the card.
- The core (`include/orcmap`, `src/core`, `src/tiles`, `src/render`)
  has **zero** ESP-IDF, M5Stack, M5GFX, LovyanGFX, or other graphics-
  framework dependency today, and must stay that way — this is enforced
  structurally (those headers are never included from `src/core`,
  `src/tiles`, or `src/render`) and verified by host tests plus the
  `examples/generic-esp32` ESP-IDF smoke test, which must not require
  M5GFX/M5Unified. M5GFX is the first *reference graphics integration*
  (`adapters/m5gfx`), not the OrcMaps renderer architecture. Architectural
  test: if `adapters/m5gfx/` were deleted, core must still build, run host
  tests, open/decode map data, project, style, and produce host pixels.

## Storage Model

`orcmap::ByteSource` (`include/orcmap/byte_source.hpp`) is the one seam:
`Read(offset, dest, length)`, `Size()`, `Valid()`. No filesystem, no
ownership of the underlying resource beyond what an adapter documents.
Implemented adapters: `orcmap::host::FileByteSource`
(`adapters/host/file_byte_source.cpp`, stdio-backed, host tests/tools only).
**Not yet implemented:** `adapters/esp_idf` (wrapping OrcSDR's
`orcsdr::storage::FileSystem` or raw ESP-IDF VFS).

## Rendering Model

Host-proof renderer exists: `ClearMapBackground` once per frame, then
`RenderFeatureTile` per source tile onto `RenderTarget`. Paint comes only
from `ResolveFeatureStyle()`. Projection uses a prepared `TileScreenMap`
(Mercator once per tile, multiply-add per vertex). `orcmap::Color` is
plain RGBA8. Unclassified features are skipped. Zoom mismatch returns
false. This is not a complete map engine (no overzoom, no
text labels). Visible tiles are enumerated with wrapped X. Line width
comes from `MapPaint`.

`adapters/m5gfx/DisplayTarget` is a `RenderTarget` over LovyanGFX
(RGB565 in the adapter only). It must not implement map semantics.

## Styling Model

Implemented and host-tested. See `docs/STYLING.md` for the full design.
Four built-in styles exist: `orcsdr-dark`, `standard-light`,
`high-contrast-field`, `night-red-safe` (`src/render/style.cpp`).
`orcmap::DefaultStyle()` is `standard-light` (OrcMaps' own neutral
default) — OrcSDR selects `orcsdr-dark` explicitly at its own integration
point, not inside OrcMaps. Runtime style switching
(`orcmap::StyleManager`) never reloads map-pack data. Rendered-tile cache
keys (`orcmap::RenderedTileCacheKey`, `include/orcmap/cache_key.hpp`) must
include pack id, tile coordinate, style id, style version, and renderer
version — this is enforced by `tests/host/test_style.cpp`.

## Pack Distribution Model

- `PackManifest`, deterministic pack identity, validation, `PackCatalog`,
  and one-best-local-pack resolution are implemented in
  `include/orcmap/pack.hpp` / `src/core/pack.cpp`. JSON discovery and
  on-device full-file SHA-256 verification are not implemented yet.
- Direction (per `docs/ORCMAP1_AUDIT.md` §6 and `docs/PACK_FORMAT.md`):
  on-device pack discovery will be directory-scan based
  (enumerate installed `.pmtiles` files), not the fixed 16-slot table
  OrcSDR's `catalog_sync.cpp` currently uses for its other data types.
- OrcSDR's existing `catalog_sync` streaming-download / atomic-activate
  (`.part` → verify → `.bak` backup → rename)/rollback machinery is the
  proven foundation to generalize for map-scale (multi-GB, chunked,
  resumable) transfers — not to be thrown away and rebuilt from scratch.
- Regional/world product packs will be distributed separately rather than
  committed. The repository deliberately retains the small Springfield
  hardware-regression pack and synthetic fixtures as golden evidence.

## Licensing Model

- Engine code: **AGPL-3.0-only**, with commercial licensing available by
  agreement — same model as `hardcoreerik/esp-rtl-sdr` and
  `hardcoreerik/OrcSDR`. See `LICENSE`, `LICENSING.md`. This is the actual
  repository license today (`LICENSE` is the real GNU AGPLv3 text), not
  aspirational.
- This dual-license structure is a deliberate choice to preserve future
  commercial licensing optionality (to manufacturers/proprietary products),
  not a legal claim beyond what the LICENSE file states.

## Data Licensing Rules

See `docs/DATA_AND_LICENSING.md` in full, and `docs/DATA_PROVENANCE_REGISTRY.md`
for the machine-readable classification schema. Key rules:

- Never build packs by bulk-downloading `tile.openstreetmap.org` or any
  other commercial map tile service — OSM's tile service explicitly
  prohibits offline/bulk use of its rendered tiles (confirmed against
  `operations.osmfoundation.org/policies/tiles/`), separate from and in
  addition to whatever the underlying data's own license permits.
- Packs come from legitimate extract data (OSM `.osm.pbf` extracts via
  Geofabrik/planet + osmium/Planetiler/tippecanoe, or another source whose
  terms explicitly permit offline redistribution).
- Every pack must carry local source, version, license, attribution, builder,
  compatibility, size, and hash metadata. The schema and first real manifest
  are implemented; runtime JSON loading and file hashing remain partial.
- Host-only regional tooling can extract bbox/GeoJSON subsets from an existing
  PMTiles archive and emit the immutable archive/manifest/checksum triplet.
  The separate Springfield builder retains the OSM PBF → Planetiler path.
- Engine license and map-data license are independent. OSM-derived data
  carries ODbL obligations (attribution, share-alike on the *data*) that
  apply regardless of the engine's own license.
- **Government-produced does not mean automatically approved.** U.S.
  federal government works are public domain by statute (17 U.S.C. §105),
  a real and strong basis — but sources are approved *dataset by dataset*,
  never by agency. A specific USGS product, for instance, can blend in
  third-party-licensed content; see the `usgs-national-map` registry
  record for a worked example of a deliberate non-approval pending
  narrower, dataset-specific review.
- **Three map-pack policy classes** (`docs/DATA_AND_LICENSING.md`): **Clean**
  (public-domain-class sources only — no attribution/share-alike
  obligation, can in principle be exclusive), **Permissive** (commercially
  usable, attribution required, no database share-alike), **Open** (ODbL-
  class sources — allowed, must remain clearly identified, never silently
  blended into a Clean pack). A pack's class is only as permissive as its
  most restrictive constituent source.
- Where practical, keep different license-obligation sources in separately
  loadable `MapSource`s rather than one blended archive (`world-base` /
  `terrain` / `us-roads` / `osm-detail`, etc.) — this is a "where
  practical" architectural preference, not an absolute rule, and physical
  packaging may still merge sources later if performance testing justifies
  it, **provided the logical provenance boundary survives in manifest
  metadata regardless.**

## Performance Constraints

Measured host and physical Tab5 results exist for the bounded Springfield
pack. The current Tab5 cold frame is ~4.4 s after excluded MVT layers are
skipped before payload decode; the same view is ~75 ms on the host.
`docs/PERFORMANCE.md` and
`docs/evidence/TAB5_SPRINGFIELD_HARDWARE.md` are authoritative. The
LilyGO ESP32-S3 path is physically render-verified at a 1,037 ms frame
total; full power-to-map and stage timings have not been transcribed.

## Memory Constraints

`PmTilesReader` holds only the 127-byte header and the decompressed root
directory (≤16,257 bytes compressed per the PMTiles v3 spec) after
`Open()`. Leaf directories and tile bytes are fetched on demand into
caller-owned buffers, never accumulated. This is implemented and verified
by `tests/host/test_pmtiles.cpp` reading a real (synthetic) archive.

## Compatibility Goals

- A properly-licensed `.pmtiles` archive built by any standard tool
  (Planetiler, tippecanoe, `pmtiles` CLI) should be a valid OrcMaps input,
  not just archives built by OrcMaps' own (not-yet-built) pack builder.
- **Development toolchain vs. declared compatibility floor — two
  different things, deliberately kept separate (confirmed 2026-09-13).**
  We build and test OrcMaps itself against the **latest ESP-IDF 6.x**
  (currently v6.1, released 2026-08-27, confirmed via the GitHub releases
  API) — that's the toolchain a contributor should have installed, and
  what CI would use once an ESP-IDF build stage exists (`STATUS.md`
  blockers — no such CI stage exists yet). It is **not** the same thing as
  what `idf_component.yml` declares as OrcMaps' minimum required ESP-IDF
  version for *consumers*.
- **`idf_component.yml` declares `idf: ">=5.0"`** — deliberately broad, so
  the widest practical range of ESP-IDF projects can adopt OrcMaps,
  including OrcSDR's own current manifest
  (`apps/orcsdr-tab5/main/idf_component.yml` pins `>=5.5.0,<5.6.0`, which
  already satisfies this floor — there is no version conflict blocking
  Phase 4 on this basis). **Do not raise this floor to chase the dev
  toolchain version.** Raise it only when a specific ESP-IDF API OrcMaps'
  code actually calls requires a newer minimum — a decision made from a
  real compile error, not preemptively "for robustness." Preferring the
  highest practical compatibility for downstream projects is itself the
  robustness goal here, not a newer version number for its own sake.

## Public API and Versioning

- **Public API surface is `include/orcmap/` plus the adapter directories**
  (`adapters/host/` today; `adapters/m5gfx` is an optional RenderTarget
  integration, `adapters/esp_idf` is not yet implemented) — nothing under
  `src/` or `third_party/` is a consumer-facing contract, ever. Format
  headers `mvt.hpp` / `pmtiles.hpp` are public *today* because they are
  still usable low-level APIs; they are not the long-term application API
  (`MapEngine` does not exist yet; Viewport provides the portable camera
  state and controls but is not a full engine). `feature.hpp` is the
  format-independent feature model. `experimental/mvt_classify.hpp` is
  **not** stable public API. This is enforced structurally, not just by
  convention:
  `tests/consumer/` builds an external-consumer smoke test whose own
  include path never adds `src/` or `third_party/` — if that target builds,
  a real external project (OrcSDR included) could integrate the same way.
  Extend that test as real API surface (`MapEngine`, `Viewport`, overlays)
  is added, rather than letting it go stale once written.
- **OrcSDR pins a specific OrcMaps commit/version, never `main`.** The
  precedent is OrcSDR's existing `esp-rtl-sdr` dependency
  (`apps/orcsdr-tab5/main/idf_component.yml` in the OrcSDR repo):
  ```
  esp_rtl_sdr:
    git: https://github.com/hardcoreerik/esp-rtl-sdr.git
    version: 1cd19d1363daea49b013c2d28a25750fcbfcff78
  ```
  a `git:` URL plus a full 40-character immutable commit SHA — not a
  branch, not a floating tag. OrcMaps is not published to the ESP
  Component Registry (and won't be without an explicit instruction to do
  so), so this exact `git:` + SHA pattern is how OrcSDR's future OrcMaps
  dependency should look too, once integration actually happens
  (`ROADMAP.md` Phase 4 — not started).
- **Semantic versioning, pre-1.0 discipline.** `idf_component.yml` starts
  at `0.1.0`. Before 1.0, the public API can still evolve, but changes
  must be deliberate and documented (this file + `ARCHITECTURE.md`), not
  incidental. After 1.0, a breaking public-API change requires a major
  version bump.
- **Map-pack releases are versioned separately from engine releases.** An
  engine version (e.g. `OrcMaps v0.4.0`) and a map-pack version (`Oregon Clean
  2026.09`) are independent release trains — updating map data should
  never force an engine release unless pack-format/API compatibility
  actually changed. See `docs/PACK_MANIFEST_SCHEMA.md` "Compatibility
  metadata" for how a pack states what engine version it needs.

## Local Development Conventions

- All local branches/worktrees for OrcMaps development are created under
  `F:\Ai\OrcMaps-Temp\` — the same pattern OrcSDR already uses
  (`F:\Ai\OrcSDR-Temp\`). The primary clone stays at `F:\Ai\OrcMaps`; a
  worktree for branch `foo` goes at `F:\Ai\OrcMaps-Temp\OrcMaps-foo` (or
  similar), never scattered elsewhere.
- **Documentation Truth CI is a project rule, not optional tooling.**
  `.github/workflows/documentation-truth.yml` runs
  `tools/check_documentation_truth.py` on every PR, push to `main`, and
  weekly on a schedule — adapted directly from OrcSDR's own
  `documentation-truth.yml`/`tools/check_documentation_truth.py`, which is
  explicitly credited by the user as part of what has made that project's
  workflow work well. Every durable-doc edit (`PROJECT_TRUTH.md`,
  `ARCHITECTURE.md`, `ROADMAP.md`, `STATUS.md`, `README.md`,
  `LICENSING.md`, `docs/*.md`) should be checked locally
  (`python tools/check_documentation_truth.py` from the repo root) before
  committing — the same discipline as running host tests before pushing.
  See "Document authority" at the end of this file for why this matters:
  the checker exists specifically to catch the gap between what a doc
  claims and what the repository actually contains (it already caught and
  this session fixed a real drift: a test-count claim that said 17 when
  the actual count was 20).
- **Data Provenance Truth CI is the sibling gate for map-data licensing.**
  `.github/workflows/data-provenance.yml` runs
  `tools/check_data_provenance.py` on the same schedule as Documentation
  Truth. It enforces the registry rules in "IP and Provenance Safety
  Model" above (e.g. an `UNKNOWN` or `REVIEW_REQUIRED` source can never be
  marked `official_pack_allowed`, an `ODBL` source can never be marked
  `clean_pack_allowed`) — it does not replace human legal judgment, it
  enforces decisions that have already been made and recorded, the same
  way `check_documentation_truth.py` doesn't decide what's true, only
  catches when a doc stops matching what is.
- **Contributions**: `CONTRIBUTING.md` documents the practical mechanics
  and the DCO-style ("Developer Certificate of Origin") licensing
  certification contributors implicitly make — chosen deliberately over a
  heavier CLA tool, with an explicit flag that a formal CLA + attorney
  review is the right next step if contribution volume ever makes the
  lightweight approach unclear.

## Naming Conventions

- Files/types: `PascalCase` for types (`PmTilesReader`, `MapStyle`),
  `PascalCase` for free functions (`ResolveFeatureStyle`, `ZxyToTileId`) —
  matches the Google-style C++ convention already used in this codebase,
  not `snake_case` (note: this differs from OrcSDR's own `offline_map.cpp`,
  which uses `snake_case` — OrcMaps does not have to match OrcSDR's C++
  style, only its licensing/distribution pattern).
- `k`-prefixed constants (`kMercatorMaxLatDeg`, `kRendererVersion`).
- Style ids: lowercase-kebab-case strings (`orcsdr-dark`).

## Decisions Already Made

- Repo: `hardcoreerik/orcmaps`, public, AGPL-3.0.
- Local path convention: `F:\Ai\OrcMaps`.
- Container format: PMTiles v3 (see above).
- Rendering approach: vector-decode-once + RGB565 cache (hybrid).
- Style system: 4 built-in compiled styles, `MapStyle`/`FeatureRule`/
  `ResolveFeatureStyle` API, `RenderedTileCacheKey` includes style
  id+version+renderer version.
- Dependency: miniz 3.1.2 (`richgel999/miniz` commit
  `77d0dce8627735138c51770d1799a1ef48f2117d`), MIT, vendored as the full
  3.1.2 source snapshot under `third_party/miniz`. Only inflate is
  compiled (`miniz.c` + `miniz_tinfl.c` with `MINIZ_NO_DEFLATE_APIS` /
  `MINIZ_NO_ZLIB_APIS` / `MINIZ_NO_ARCHIVE_APIS`). Recorded in
  `docs/DEPENDENCY_LEDGER.md`.
- Test fixture built via the official `pmtiles` Python package (BSD-3-
  Clause, dev-tool only, not vendored) rather than hand-rolled binary data —
  see `tests/fixtures/generate_fixture.py`.
- CMake layout matches `esp-rtl-sdr`'s precedent exactly: repo-root
  `CMakeLists.txt` is the ESP-IDF component registration file;
  `tests/host/CMakeLists.txt` is a separate standalone host-only CMake
  project (no ESP-IDF toolchain required to build/run tests).
- Data provenance registry: JSON (not YAML) under `data/sources/`, one
  record per reviewed dataset, validated by
  `tools/check_data_provenance.py` — JSON chosen specifically to keep the
  checker standard-library-only, matching `check_documentation_truth.py`'s
  own no-third-party-dependency discipline.
- Nine starter provenance records reviewed and committed: Natural Earth
  and OpenStreetMap `CONFIRMED`/approved; U.S. Census TIGER/Line, NOAA
  ETOPO, USGS National Map (deliberately unapproved as a whole — see its
  own record's notes), USDOT NAD, and Overture Places `REVIEW_REQUIRED`
  pending primary-source verification or per-record license filtering;
  geoBoundaries gbOpen and Google Open Buildings (CC BY 4.0 option)
  `CONFIRMED`/approved. See `data/sources/*.json` and
  `docs/DATA_PROVENANCE_REGISTRY.md`.
- `tests/consumer/` established as the external-consumer build gate,
  structurally isolated from `src/`/`third_party/` via CMake include-path
  visibility rather than a documentation-only rule.
- `include/orcmap/attribution.hpp` and `include/orcmap/map_source.hpp`:
  minimal, header-only runtime attribution API
  (`AttributionInfo`, `MapSourceInfo`, `CollectRequiredAttribution()`),
  host-tested. No `MapEngine` or source-discovery implementation yet —
  this establishes the shape those will eventually populate.
- `Viewport` is the sole portable camera state: center, integer zoom,
  viewport size, pixel pan, visible/fit bounds, projection/unprojection,
  and anchor-preserving zoom are implemented and host-tested. Hardware
  touch/button bindings are not implemented yet; adapters must translate
  input into this API rather than create a second coordinate system.
- World-overview source acquisition is pinned to 21 official Natural Earth
  5.1.2 archives across 110m, 50m, and 10m. The archives and extracted
  shapefiles remain gitignored local provisioning inputs. Reproducible
  `orcmaps-overview-1` z6/z7/z8 candidates measure 4.61/9.29/16.37 MiB and
  render through the generic host target. z6 tiny and z7 standard are evidence-
  based recommendations; no release cutoff is frozen.

## Explicit Non-Goals

- OrcMaps will not know what an aircraft, LoRa node, RF site, or any other
  OrcSDR concept is.
- OrcMaps will not implement payment/license enforcement, DRM, activation
  keys, or entitlement systems (commercial licensing stays a manual
  agreement process — see `CONTRIBUTING.md`).
- OrcMaps will not require every user to install a full-planet pack —
  tiered packs (world/country/state/local) are the goal, sharing one
  rendering path.
- OrcMaps will not casually invent `ORCMAP2` — see "Current Technology
  Direction" above.

## Open Decisions

- Tile content schema (general OpenMapTiles-style MVT vs. narrower custom
  schema) — deferred to Lane County vertical-slice measurement.
- Exact country/state/local tiers remain open. The Natural Earth overview is
  now measured through z8; it supports a provisional regional handoff at z8,
  not a final policy for other pack types.
- Multi-file/chunked archive scheme for packs exceeding FAT32's 4 GiB
  single-file limit — not yet designed (yuiseki's 2 GiB-chunk precedent is
  the closest reference).
- External style file format (`.orcstyle`) — shape not designed, though the
  `MapStyle` struct is already serializable-shaped for it later.
- Several provenance records are `REVIEW_REQUIRED`, not `CONFIRMED` — see
  each record's `notes` field for exactly what's unverified (e.g. TIGER/
  Line's exact repackaging-attribution clause wording, ETOPO's license
  page returning HTTP 503 during research, NAD's rights page returning
  HTTP 403, Overture Places needing per-record license filtering before
  ingest). None of these are usable in an official pack until resolved —
  see "IP and Provenance Safety Model" above.
- Runtime manifest JSON loading, removable-storage discovery, and on-device
  full-file SHA-256 verification are not implemented yet.

## Historical Context That Matters

ORCMAP1 (`docs/ORCMAP1_AUDIT.md`) proved the UI/integration pattern:
`draw_base()` into a cached sprite (ADS-B) or direct-to-display (LoRa),
overlays drawn by the consuming dashboard using the map's own
`project()`/`View` for placement (LoRa) or independent polar math (ADS-B).
This pattern — base map draws once, overlays are a separate pass, style
lives in the caller — is exactly what OrcMaps formalizes. It is not being
discarded; it is the validated precedent this project is built from.

## Document Authority

When a durable decision changes — architecture, requirement, constraint,
naming, technology selection/rejection — **update this file as part of that
change**, not as an afterthought. Do not let an important design decision
exist only in a chat transcript, a commit message, or an AI agent's memory.
If a future prompt conflicts with something stated here, surface the
conflict explicitly before proceeding, and update this document
deliberately if the conflict resolves in favor of the new direction —
never silently overwrite it, and never silently work around the
contradiction in code while leaving this document stale.

# OrcSDR integration contract

This is what OrcSDR (or any other ESP-IDF application) can rely on when it
consumes OrcMaps **v0.2.x** (currently v0.2.1). It lists which APIs are stable and which are
experimental, the SD-card and flash contracts, memory behaviour, and how to
update. OrcMaps stays a standalone engine: nothing below is OrcSDR-specific
code, and OrcSDR must not carry a copy or fork of it.

Evidence for every number here is in
`docs/evidence/ESP_IDF_554_BUILD_SIZE_MEMORY.md`. Anything marked
**not measured** has not been measured.

## 1. How OrcSDR depends on OrcMaps

As with `esp-rtl-sdr`: a component-manager `git:` dependency pinned to the
full commit SHA of a release tag, never a branch.

```yaml
# apps/orcsdr-tab5/main/idf_component.yml
dependencies:
  orcmaps:
    git: https://github.com/hardcoreerik/orcmaps.git
    version: <full 40-character SHA of tag v0.2.1>
```

**Do not pin v0.2.0 on ESP32-P4, S3 or the C/H series.** In 0.2.0 the ROM's
miniz replaced the bundled one at link time and corrupted the heap on the
first inflate (`CHANGELOG.md` 0.2.1). CI now checks for this on every
change (`tools/check_miniz_symbols.py`).

```cmake
idf_component_register(... REQUIRES orcmaps ...)
```

- The component name is the dependency key, `orcmaps`. The in-repo examples
  use `OrcMaps` because they find the component by folder name; that does
  not apply to a managed dependency.
- Verified with ESP-IDF v5.5.4 for esp32p4: a scratch project with exactly
  this dependency (pinned to a pre-release commit of this branch) resolved,
  fetched into `managed_components/orcmaps`, and built. The component
  requires `idf >= 5.0` and privately requires `esp_partition`.
- The fetched component is about 6 MB on disk (examples, test packs, docs).
  Only the sources listed in the root `CMakeLists.txt` are compiled.
- The M5GFX adapter is header-only and does not depend on M5GFX itself;
  OrcSDR supplies its own M5GFX (or LovyanGFX) component.

**Updating.** Read `CHANGELOG.md` from your pinned version to the new tag,
then change `version:` to the new tag's full SHA. Before 1.0, any change to
an API marked **stable** below comes with a minor version bump and a
CHANGELOG entry. **Experimental** APIs can change in any release.

## 2. The flow this release supports

OrcMaps provides the engine and the PC-side pack tools. OrcSDR is the first
consumer: it uses the OrcMaps scripts to produce the maps its users need,
and the goal is an entirely offline experience. Nothing in the device
pipeline uses the network. On the PC, the tools run offline once their
inputs are local:

- `provision_pack.py` cuts a pack from a **source pack already on the PC**,
  using go-pmtiles `extract` on a local file. No network is needed, but a
  source pack covering the user's area must be staged first (for example a
  regional OpenMapTiles-profile pack, or a larger one per
  `docs/GLOBAL_Z13_COVERAGE_ANALYSIS.md`).
- Building a source pack from OpenStreetMap or Natural Earth needs the raw
  data once (`acquire_world_overview_sources.py`, or a downloaded OSM
  extract). After that, builds are offline and reproducible.
- How OrcSDR distributes source packs to its users (bundled with the PC
  tool, on a card, or downloaded once) is an OrcSDR decision. OrcMaps does
  not decide it.

1. **First boot, no SD card, no network.** The firmware carries the z0-z4
   world pack in a flash partition. `PartitionByteSource` reads it,
   `PmTilesReader` opens it, and `LocationPicker` shows a crosshair picker.
   The user pans and zooms, and the selection is the viewport centre.
2. **Selection to pack.** The selected latitude/longitude (plus a radius
   OrcSDR chooses) goes to `tools/pack-builder/provision_pack.py` on a PC.
   It produces `<name>.pmtiles`, `<name>.manifest.json` and `<name>.sha256`,
   laid out for the card's `/orcmaps/` directory. OrcMaps does not generate
   packs on the device.
3. **Dashboards.** At boot, `DiscoverPacks` finds the packs on the card.
   Each map view (ADS-B and LoRa today, satellite and weather later)
   calls `ResolvePack` for its visible area and zoom and draws with the
   same pipeline. Where no installed pack covers the view, the application
   can fall back to the embedded world pack.

Resolution of the world pack: z4 is 4,096 px around the equator, about
**9.8 km per pixel at the equator** (about 6.9 km at 45° latitude). That is
enough to pick a town or region, not a street.

Overlays (aircraft, nodes, tracks, satellite footprints) are **not** an
OrcMaps API yet. A dashboard draws its own markers on top of the map, using
`ProjectLatLon(viewport, point, &x, &y)` to place them.

## 3. Public API and stability

"Stable" means it can change only in a new minor version with a CHANGELOG
entry. Everything under `src/` and `third_party/` is private.

| API | Header | Status | Notes |
|---|---|---|---|
| `ByteSource` | `orcmap/byte_source.hpp` | **Stable** | `Read`/`Size`/`Valid`. OrcSDR can implement its own. |
| `esp_idf::FileByteSource` | `orcmap/esp_idf/file_byte_source.hpp` | **Stable** | `FILE*` on a VFS path the app already mounted. Offsets beyond 32 bits fail cleanly on ESP-IDF newlib. |
| `esp_idf::PartitionByteSource` | `orcmap/esp_idf/partition_byte_source.hpp` | **Stable** (new) | Data partition by label, any subtype. Read-only. `Valid()` is false if the label is absent, which is a normal state and the cue to fall back. `Size()` is the partition size, not the archive size. |
| `PmTilesReader`: `Open`, `IsOpen`, `Header`, `TileExists`, `StreamTile`, `GetTile`, `GetTileInflated`, `ReadMetadata`; `ZxyToTileId` | `orcmap/pmtiles.hpp` | **Stable** | `StreamTile` is the path to use on-device. `leaf_cache_slots` is 1 or 2. `LocateTileForTest` is for tests, not API. |
| `MvtStreamOptions`, `StreamedFeatureSink`, `MvtStreamScratch`, `ReserveMvtStreamScratch` | `orcmap/mvt_stream.hpp` | **Stable** (new) | Treat `MvtStreamScratch` as opaque: its fields are not API. Own one scratch and reuse it for every tile. |
| `DecodeMvtTile`, `MvtTile`, `TranslateMvtToFeatureTile`, `DecompressPayload` | `orcmap/mvt.hpp`, `orcmap/mvt_translate.hpp`, `orcmap/compression.hpp` | **Stable**, low-level | Batch decode: holds the whole inflated tile and all its features. Fine on host; prefer streaming on-device. |
| `Feature`, `FeatureTile`, `FeatureKind` | `orcmap/feature.hpp`, `orcmap/feature_kind.hpp` | **Stable** | Format-independent feature model. |
| Pack discovery: `PackFileSystem`, `DiscoverPacks`, `DiscoveryReport`, `RejectedPack`, `PackRejection`, `ArchivePathForManifest`; `esp_idf::PackFileSystem` | `orcmap/pack_discovery.hpp`, `orcmap/esp_idf/pack_filesystem.hpp` | **Stable** | See section 4. |
| `PackManifest`, `PackCatalog`, `ResolvePack`, `ValidatePackManifest`, `PackValidationError`, `ParsePackManifestJson` | `orcmap/pack.hpp`, `orcmap/pack_json.hpp` | **Stable** | `ResolvePack` returns one pack whose bounds fully contain the request at that zoom (highest `priority`, then highest `pack_id`), or `nullptr`. It never falls back on its own. |
| `Viewport` and its functions (`SetCenter`, `SetZoom`, `PanByPixels`, `ZoomAtScreenPoint`, `FitBounds`, `FillBounds`, `MinFillZoom`, `WorldViewZoom`, `ProjectLatLon`, `ScreenToLatLon`, `GetVisibleBounds`, `EnumerateVisibleTilePlacements`, ...) | `orcmap/viewport.hpp`, `orcmap/geo.hpp` | **Stable** | Portable camera state. Each dashboard owns its own `Viewport`. |
| `LocationPicker`, `LocationPickerLimits`, `LocationPickerZoomRange` | `orcmap/location_picker.hpp` | **Stable** (new) | Section 2. `Moved()` lets the wizard require a deliberate choice. |
| Renderer: `ClearMapBackground`, `RenderFeatureAt`, `RenderFeatureTile`, `RenderFeatureTileAt` | `orcmap/renderer.hpp` | **Stable** | Unclassified features are skipped. Labels are not drawn (no text rendering). Polygons fill from the first ring only (section 7). |
| `RenderTarget` | `orcmap/render_target.hpp` | **Stable** | Implement it for a non-M5GFX display. |
| `MapStyle`, `BuiltinStyle`, `styles::OrcSdrDark()` and the other built-ins, `FindBuiltinStyleById` | `orcmap/style.hpp` | **Stable** | Style ids are stable; exact colours may be tuned between releases. |
| M5GFX `m5gfx_adapter::DisplayTarget` | `orcmap/m5gfx/display_target.hpp` | **Stable** | Header-only, over `lgfx::v1::LovyanGFX&`. RGB565, alpha ignored. Draws straight to the display or sprite it is given. |
| Attribution: `AttributionInfo`; `PackManifest::attribution` | `orcmap/attribution.hpp`, `orcmap/pack.hpp` | **Stable** | Show every entry with `required == true` for each pack on screen. Never hard-code credit text. |
| `MapSourceInfo`, `CollectRequiredAttribution` | `orcmap/map_source.hpp` | **Experimental** | Descriptive placeholder for a future engine API; nothing in the pipeline produces it. |
| `experimental::TryClassifyFeature`, `AssignFeatureKinds`, `AssignFeatureKindsForProfile`, `IncludeNoTextBasemapLayer` | `orcmap/experimental/mvt_classify.hpp` | **Experimental** | Layer-name heuristic for `openmaptiles-3.16` and `orcmaps-overview-1`. **Required in practice:** the renderer draws nothing that is not classified. Expect it to be replaced by a real tile-content schema. |
| `InflatingByteStream`, `DecompressStreaming`, `StreamMvtTile` | `orcmap/compression.hpp`, `orcmap/mvt_stream.hpp` | **Experimental** | Building blocks under `StreamTile`; call `StreamTile` instead. |
| `orcmap_bench::RenderMeasuredFrame` and the rest of `examples/hardware-benchmark` | examples only | **Not part of the component** | Demo and benchmark harness. OrcSDR writes its own frame loop (below). |

### The frame loop OrcSDR writes

OrcMaps has no `MapEngine` yet, so the per-frame loop belongs to the
application. This is the shape that compiled for esp32p4 against v0.2.0
(the size probe in the evidence file):

```cpp
// Once, at startup.
orcmap::MvtStreamScratch scratch;
orcmap::ReserveMvtStreamScratch(&scratch, 48 * 1024);

// Per frame.
struct Ctx { orcmap::Viewport vp; orcmap::RenderTarget* t; orcmap::TilePlacement p; };
bool Sink(const orcmap::Feature& f, void* c) {
  Ctx& s = *static_cast<Ctx*>(c);
  auto& feature = const_cast<orcmap::Feature&>(f);  // reused per feature, not kept
  orcmap::FeatureKind kind;
  if (orcmap::experimental::TryClassifyFeature(feature, &kind)) {
    feature.kind = kind;
    feature.kind_assigned = true;
  }
  return orcmap::RenderFeatureAt(feature, s.p, s.vp,
                                 orcmap::styles::OrcSdrDark(), s.t);
}

void DrawMap(const orcmap::PmTilesReader& reader, const orcmap::Viewport& vp,
             orcmap::RenderTarget* target) {
  orcmap::ClearMapBackground(vp, orcmap::styles::OrcSdrDark(), target);
  std::vector<orcmap::TilePlacement> placements;
  orcmap::EnumerateVisibleTilePlacements(vp, &placements);
  for (const auto& p : placements) {
    Ctx ctx{vp, target, p};
    orcmap::MvtStreamOptions o;
    o.include_layer = &orcmap::experimental::IncludeNoTextBasemapLayer;
    o.feature_sink = &Sink;
    o.feature_sink_ctx = &ctx;
    reader.StreamTile(p.tile.z, p.tile.x, p.tile.y, o, &scratch);
  }
}
```

When the world wraps (a low zoom on a wide screen), one tile appears at
several placements. `examples/hardware-benchmark/bench.cpp` groups
placements by tile so each tile is decoded once and drawn at each copy; do
the same if low-zoom frames matter.

## 4. SD card contract

```
<mount>/orcmaps/
    <name>.manifest.json    required
    <name>.pmtiles          required
    <name>.sha256           optional, not read on the device
```

- **Location.** OrcMaps never mounts anything and has no built-in path. The
  application mounts the card and passes the directory. The Tab5 demo mounts
  at `/sd`, so it scans `/sd/orcmaps`.
- **Pairing by filename stem.** `x.manifest.json` describes `x.pmtiles`.
  A manifest never contains a path. One directory, no recursion; any other
  file is ignored.
- **Manifest schema.** Builders write `"manifest_version": 1`, but the
  engine does not read that field. What the engine enforces
  (`ValidatePackManifest`):
  - `schema_version` is `openmaptiles-3.16` or `orcmaps-overview-1`
    (anything else is rejected, never guessed);
  - `pmtiles_version` is 3, and `pack_class` is `clean`, `permissive` or
    `open`;
  - every required field is present, bounds and zooms are valid, the
    provenance ids are non-empty, and every required attribution entry has
    text;
  - `size_bytes > 0`, `output_sha256` is a well-formed SHA-256, and
    `pack_id` matches the identity derived from the other fields.

  The manifest file must be at most 64 KiB and nested at most 12 levels.
  `docs/PACK_MANIFEST_SCHEMA.md` is the full field list.

**What happens with a bad pack.** One bad pack never hides the others:
discovery records it in `DiscoveryReport::rejected` and keeps scanning.

| Situation | Result |
|---|---|
| `.pmtiles` missing | rejected: `kArchiveMissing` |
| `.manifest.json` missing | the archive is invisible (no metadata, not a pack) |
| manifest unreadable or over 64 KiB | rejected: `kUnreadable` |
| manifest not well-formed JSON | rejected: `kBadJson` (with `PackJsonError`) |
| manifest well-formed but invalid or unsupported | rejected: `kInvalidManifest` (with `PackValidationError`) |
| archive not the size the manifest states | rejected: `kArchiveSizeMismatch` |
| second manifest with the same `pack_id` | rejected: `kDuplicate` |
| directory cannot be listed (no card, not mounted) | `directory_listed == false`, no packs |
| archive the right size but **corrupt** | **not detected by discovery.** The SHA-256 is not checked on the device. A damaged header or root directory makes `PmTilesReader::Open` return false. A damaged tile usually fails to inflate or parse, and `StreamTile` returns false for that tile only. The streaming path does **not** check the gzip CRC-32, so corruption that still inflates can draw wrong geometry. Verify cards on a PC with `tools/pack-verify` and the `.sha256` sidecar. |

`DiscoverPacks` returns true only if nothing was rejected. To ask "do we
have any maps", check `report.packs_added`.

## 5. Embedded world map (flash)

Release asset `orcmaps-world-z4-0.2.0.pmtiles`: 871,343 bytes, SHA-256
`9aea08772bacf1f024d1da90cc52aa8fcf0b0e37405415dc7c91e75e56596f0f`,
Natural Earth 5.1.2 (public domain; no credit required, credit welcome:
"Made with Natural Earth"). It contains no OpenStreetMap data.

- **It cannot go in the app partition.** It needs its own data partition,
  so OrcSDR's partition table has to change whatever the code size. The
  minimum is 0xD5000 (872,448 B, 4 KiB aligned); 0x100000 (1 MiB) leaves
  room for a rebuilt pack. Example partition-table line (the label is
  OrcSDR's choice; OrcMaps matches on the label only):

  ```
  orcmaps_world, data, 0x40, , 1M,
  ```
- **Flashing it.** ESP-IDF's
  `esptool_py_flash_to_partition(flash orcmaps_world <path-to-pmtiles>)` in
  the project `CMakeLists.txt` makes `idf.py flash` write it. Not tried on a
  device.
- **Discovery does not apply.** There is no manifest in flash. The
  application knows it has one world pack. It takes the zoom range from
  `reader.Header()` (z0-z4), uses the Mercator world bounds, and passes
  `LocationPickerLimits{min_zoom, max_zoom}`.
- **Verified on the host** (`tools/embedded-world-check`): the archive,
  padded with 0xFF to a partition size, opens, and `LocationPicker` frames it
  at 1280x720, 930x720 and 480x320. Every visible tile of the opening view,
  a picked point at z4, and a drag decoded with nothing missing. On
  hardware: **not verified**.

## 6. Memory

Allocation goes through `new`/`malloc` (`std::vector`, `std::string`).
OrcMaps never asks for internal or DMA-capable memory, and no allocation is
tied to a specific heap. There is no per-call allocator hook: adding one
would change the public types, so it is future work. **Which heap a block
comes from is therefore decided by ESP-IDF's malloc policy, i.e. OrcSDR's
sdkconfig:** a request smaller than `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL`
tries internal RAM first; larger ones go to PSRAM.

Main allocations of the streaming pipeline (sizes for ESP32-P4 unless noted):

| What | Size | Lifetime | Heap with ALWAYSINTERNAL = 8192 |
|---|---|---|---|
| Inflate window (`MvtStreamScratch`) | 32 KiB | reserved once, kept | PSRAM |
| Inflater state | 8,364 B | kept | PSRAM (just above 8,192) |
| Compressed input chunk | 2 KiB | kept | internal first |
| Feature byte buffer | the `max_feature_bytes` you reserve (largest feature measured: 36,259 B); released above 48 KiB | kept | PSRAM |
| Retained geometry | up to 4,096 points (~32 KiB) between tiles | kept | mostly PSRAM |
| Per-layer key/value tables | worst measured ~24 KiB total, as many small strings | per tile | **internal first** |
| Per-feature rings and points | many small vectors | per feature | **internal first** |
| `PmTilesReader` root directory | ≤ 16 KiB compressed, 24 B per parsed entry | reader lifetime | PSRAM if large |
| Leaf-directory cache | ~21 KiB per slot, 1 or 2 slots | reader lifetime | PSRAM |
| `DecompressPayload` state (directory reads) | 8,364 B | per call, on the **heap** since 0.2.0 (was the stack) | PSRAM |

Host measurement (valgrind DHAT, x86-64, one 1280x720 frame, not an ESP32
figure) of pipeline heap live at the process peak:

- Springfield z14: ~344 KB, of which ~163 KB is ~2,800 blocks under 8 KiB;
- world z2: ~121 KB, of which ~14 KB is small blocks.

Before streaming (0.1.0 batch path) the same Springfield frame held ~2.35 MB.

What this means for OrcSDR (about 80-100 KB internal free, 35-60 KB
DMA-capable, 32 MiB PSRAM):

- Large buffers already land in PSRAM with the Tab5 demo's setting,
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=8192`.
- The risk is the **small** per-feature allocations on a dense tile. They
  try internal RAM first, and ESP-IDF falls back to PSRAM only once internal
  is exhausted. Set `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` (the Tab5 demo
  uses 40,960) so DMA and Wi-Fi keep a reserve. If internal pressure is
  still too high, lowering `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` pushes more
  small blocks to PSRAM, for the whole app, at some speed cost.
- Call `ReserveMvtStreamScratch` once, early, from the task that will draw.
- Stack: the draw task no longer needs room for the 8 KiB inflater state.
  The Tab5 demo draws from a 16 KiB main task. OrcSDR's own draw-task stack
  requirement is **not measured**.
- Keep one `PmTilesReader` per open archive and share it across dashboards
  that show the same pack; each reader holds its root directory and leaf
  cache.
- Measure internal free/minimum around a frame inside OrcSDR; the benchmark
  harness shows how (`internal_min`, `internal_largest` hooks).

## 7. Code size and performance

**Flash (ESP32-P4, ESP-IDF 5.5.4).** `libOrcMaps.a` in an OrcSDR-shaped
probe that calls every API above (world partition, picker, SD discovery,
`FileByteSource`, streaming render, M5GFX target):

| Optimization | `.text` | `.rodata` | Total flash | Internal `.bss` |
|---|---:|---:|---:|---:|
| `-O2` (`OPTIMIZATION_PERF`) | 79,518 B | 1,367 B | **80,885 B** | 208 B |
| `-Os` (`OPTIMIZATION_SIZE`) | 43,324 B | 1,386 B | **44,710 B** | 208 B |

Add a few KB for OrcSDR's own glue and the header-only M5GFX adapter, and
shared `libstdc++` code OrcSDR may not already link. Against OrcSDR's
~470 KB free in the app partition the code fits. The world pack does not
(section 5).

**Performance.** No tile cache: every frame is decoded and rendered from
storage.

- The Tab5 numbers on record come from the **OrcMaps demo**, from **before**
  the streaming decoder, at a 1280x600 map area. They are about 0.93 s mean
  per uncached frame over a zoom/pan sweep (`docs/evidence/TAB5_ZOOM_PAN_SWEEP.md`)
  and about 1.3 s for a Springfield z15 frame.
- They are **not** OrcSDR numbers and not a target. OrcSDR draws inside its
  own tasks, display pipeline and memory budget.
- The streaming decoder inflates each tile twice. It has **not been
  re-measured on the Tab5**, and OrcMaps performance inside OrcSDR is
  **not measured**. Both need to be measured after integration.

## 8. Known limitations in 0.2.0

- (0.2.0 and 0.2.1 only) Polygons were filled from their first ring only, so on the Natural Earth world packs some
  tiles showed as tile-shaped patches of unpainted land or ocean. Fixed after 0.2.1: every ring is filled and
  holes are subtracted (`RenderTarget::FillPolygonRings`; see CHANGELOG.md). Pin a commit that includes the fix.
- No text or labels, no overzoom, no tile cache, no overlay API.
- Classification is experimental and required (section 3).
- Streaming does not verify the gzip CRC-32 (section 4).
- `FileByteSource` fails reads beyond 32-bit offsets on ESP-IDF newlib, so
  single archives over 2 GiB are not usable.

## 9. Future work (not in this release)

- A tile or rendered-tile cache. The expensive stages are decode and render
  (sweep evidence); a PSRAM cache of rendered tiles would make pans and
  dashboard switches cheap.
- An overlay API, so dashboards (ADS-B aircraft, LoRa nodes, satellite
  footprints, weather) share projection and z-ordering instead of each
  drawing its own.
- A per-call allocator or heap-caps hook for pipeline buffers.
- A `MapEngine` that owns the frame loop above.

# Changelog

OrcMaps follows semantic versioning with pre-1.0 discipline (see
`PROJECT_TRUTH.md` "Public API and Versioning"): before 1.0 a public API may
change, but only in a new minor version, and every such change is listed here.
Consumers pin a release by its full commit SHA (`docs/ORCSDR_INTEGRATION.md`).

## 0.3.0 - 2026-10-18

Adds a second painting path that projects a feature once and then draws it any
number of times, so a target can paint a frame in horizontal bands instead of
one panel transaction per primitive. On the CYD 3.5" this takes a 202-feature
map frame from 285.65 ms to 50.88 ms (3.5 -> 19.7 fps). The investigation,
every measurement and the remaining work are in
`docs/evidence/2026-10-18-cyd-frame-rate.md`.

Rendered output is unaffected: the new path is pinned to be pixel-identical to
`RenderFeatureAt` by a host test.

### Added

- `ScreenFeature`, `ProjectFeatureToScreen()` and `DrawScreenFeature()`
  (`include/orcmap/renderer.hpp`): the result of every per-vertex projection,
  the style lookup and the polygon-winding grouping for one feature, kept so
  the same frame can be painted more than once without repeating them. Pass the
  same `ScreenFeature` back in to reuse its buffers; after warm-up projection
  does not allocate. Most useful for a target that cannot hold a whole
  framebuffer and paints a band at a time, and for skipping features whose
  screen bounds miss the band.
- `examples/esp32-cyd-35`: an FPS probe build
  (`-DORCMAP_CYD_FPS_PROBE=1`) that sweeps the display SPI write clock, times
  the wire floor, times CPU-only rendering against a counting target, verifies
  each clock by reading pixels back, and reports a banded frame broken into
  projection, rasterisation and bus time. Needs no SD card or pack.
- `examples/esp32-cyd-35`: `BandTarget` and the screen-space cache that paint a
  frame band by band in RAM, so the panel receives one `pushImage` per band.
- `Cyd35Display::SetWriteClock()` / `WriteClock()` for changing the display SPI
  clock at runtime.

### Changed

- The skip rules and the style lookup are now one `ResolveDrawablePaint()`
  helper shared by both draw paths, so a feature cannot be drawn by one and
  skipped by the other.
- The CYD example builds at 240 MHz instead of the ESP-IDF default 160 MHz.
  Every CPU cost in a frame is on the critical path now that the display bus is
  understood, and the chip is a 240 MHz part.

### Notes

- On this board the panel bus, not the CPU, sets the frame rate. A full
  480x320 RGB565 frame is 307,200 bytes and `Panel_ST7796` only speaks
  `rgb565_2Byte` or `rgb888_3Byte`, so bytes per frame is fixed and the fastest
  possible update is one `fillScreen` at 80 MHz: 30.98 ms, 32.3 fps. Pushing an
  empty frame as four bands already costs 33.16 ms, so 30 fps is beyond the
  hardware, not merely unoptimised. The realistic target is ~25 fps.
- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240` in `sdkconfig.defaults` only seeds a
  new `sdkconfig`; an existing generated `sdkconfig` must be updated or
  regenerated for a frequency change to take effect.

## 0.2.3 - 2026-10-17

Combines the integer render path with the polygon-ring and streaming-cleanup
fixes, which had landed on separate branches after 0.2.1. Both are required
together: the 0.2.2 commit as it existed on its own branch lacked
`FillPolygonRings` and the allocation-free scratch cleanup that the OrcSDR
engine pin already relies on, so pinning it directly would have regressed world
views and re-introduced an allocating cleanup path. Nothing from 0.2.2 is
removed here.

### Changed

- **The per-vertex and per-segment render path no longer uses double
  precision.** `ProjectLocal` evaluated `origin + local * scale` in IEEE
  double and clamped with `isfinite`/`floor`; on ESP32, whose FPU is
  single-precision only, the compiled function made ~18 soft-float ROM calls
  per projected vertex. Disassembly of the CYD firmware showed
  `__floatsidf`, `__muldf3`, `__adddf3`, `__unorddf2`, `__gtdf2`, `__gedf2`,
  `__ledf2`, `floor` and `__fixdfsi` twice each per call. `TileScreenMap` now
  also carries the coefficients in Q16.16 and `ProjectLocal` is integer-only
  (two multiplies and a shift per axis).
- **`ClipLineToPixelRect` clips with exact rational arithmetic.** It made ~50
  soft-float calls per segment, including four `__divdf3`. Endpoints beyond
  +-2^20 still take an equivalent double path, so a saturated coordinate
  clips exactly as before.
- **`RasterStrokeWidthPx` rounds in single precision** rather than promoting
  to double and calling `floor`.
- A line string now projects each vertex once instead of once per segment.

### Fixed

- **Every ring of a polygon is now filled, and holes are subtracted.** The
  renderer used to fill only the first ring of a polygon feature, so a
  multipolygon lost every part after the first and holes were never cut out.
  The Natural Earth world overview packs a continent or an ocean as several
  parts in one feature, so world views showed whole landmasses and ocean areas
  (Australia, Japan, Indonesia, parts of the Atlantic and Indian Ocean) as
  unpainted tile-shaped patches, with admin boundaries visible only inside
  those patches. Rings are now grouped into parts (a ring winding like the
  first is a new exterior part, the opposite winding is a hole) and each part
  is filled even-odd.
- **The streaming scratch is released without allocating during cleanup.** The
  trim path ran `std::vector` copy-and-swap for the feature buffer and
  `clear()` + `shrink_to_fit()` for the layer tables, both of which allocate,
  and the trim runs from a scope guard that can execute while unwinding
  `std::bad_alloc`. Every trim is now an allocation-free swap with an empty
  vector.

### Added

- `RenderTarget::FillPolygonRings(xy_pairs, ring_point_counts, ring_count,
  color)`: one polygon made of several rings, even-odd. It has a default
  implementation that fills only the first ring, which is the previous
  behaviour, so an existing `RenderTarget` keeps compiling and drawing as
  before. The host `FramebufferTarget` and the M5GFX `DisplayTarget` override
  it; `FillPolygon` is now the single-ring case.
- `RenderScratch` plus `RenderFeatureAt(..., RenderScratch*)`: a streaming
  caller reuses one scratch per tile, so the polygon vertex buffer is
  allocated at most once and the tile's screen map is prepared once instead
  of once per feature. The 5-argument `RenderFeatureAt` is unchanged.
- `TileScreenMap::origin_sx_q16`, `origin_sy_q16`, `scale_q16`. They are
  derived, so build maps with `MakeTileScreenMap` /
  `MakeTilePlacementScreenMap`. The struct grew, so consumers must recompile.

### Removed

- The "polygons are filled from their first ring only" known limitation.

**The render path change does not move a pixel; the polygon change does, on
purpose.** The host render (synthetic 1280x720) and `pack-inspect preview` for
Springfield z14, Springfield z15, world z2 and Oregon z7 are byte-identical to
the previous engine revision that already had the polygon and cleanup fixes,
and `tests/host/test_clip.cpp` compares the integer clipper against an
independent long-double reference over 4,000 random segments. Device timings
for the integer path have not been measured; the host is not a proxy for ESP32.

## 0.2.1 — 2026-09-25

### Fixed

- **Bundled miniz was replaced by ESP32 ROM miniz on ESP-IDF, corrupting the
  heap on first inflate.** ESP-IDF ROM linker scripts define `tinfl_*`,
  `tdefl_*` and some `mz_*` symbols at absolute addresses. On ESP32-P4, S3
  and the C/H series those beat the bundled objects, so OrcMaps called the
  ROM inflater with a `tinfl_decompressor` sized for the bundled miniz
  (8,364 B on ESP32-P4; the ROM's is 10,992 B). The first inflate, the
  PMTiles root directory in `PmTilesReader::Open`, wrote past the
  allocation. Found on a Tab5 during OrcSDR integration. Every bundled miniz
  symbol, and every miniz name an ESP-IDF ROM exports, is now renamed to
  `orcmap_*` (`third_party/miniz/orcmap_miniz_prefix.h`), so devices run the
  same inflater as the host tests. The 0.2.0 change that moved
  `DecompressPayload`'s state to the heap exposed this on the directory
  path; the streaming path was already exposed.

### Added

- `tests/esp_idf_link` and `tools/check_miniz_symbols.py`, run in CI for
  esp32p4 and esp32s3: fails if any miniz symbol OrcMaps uses resolves to
  ROM.

No API or data change; the 0.2.0 world pack is unchanged.

## 0.2.0 — 2026-09-24

First tagged release (`v0.2.0`). 0.1.0 was the version in
`idf_component.yml` from the start of the project; it was never tagged. This
entry lists what 0.2.0 adds over that baseline for a consumer.

### Added

- **`LocationPicker`** (`include/orcmap/location_picker.hpp`): a crosshair
  "choose your location" camera. The selection is the viewport centre; the
  opening zoom and the zoom-out floor are derived from the screen size and
  the data's coverage, so the view never shows empty margin.
  `LocationPickerZoomRange` exposes the same zoom choice.
- **`esp_idf::PartitionByteSource`**
  (`adapters/esp_idf/include/orcmap/esp_idf/partition_byte_source.hpp`):
  reads a PMTiles archive from a read-only flash data partition found by
  label, so a map can ship in the firmware image. The component now
  privately requires `esp_partition`.
- **Runtime pack discovery** (`include/orcmap/pack_discovery.hpp`):
  `DiscoverPacks` scans one directory for `<name>.manifest.json` +
  `<name>.pmtiles` pairs, validates each, and reports every rejection;
  `ResolvePack` picks the pack for a view. `esp_idf::PackFileSystem` is the
  ESP-IDF implementation.
- **Streaming decode** (`include/orcmap/mvt_stream.hpp`,
  `include/orcmap/compression.hpp`, `PmTilesReader::StreamTile`,
  `RenderFeatureAt`): a tile is inflated and parsed straight from the
  archive, one feature at a time, without ever holding the inflated tile.
  No single allocation exceeds roughly one feature's bytes. Also
  `PmTilesReader::GetTileInflated`, `TileExists`, `DecompressStreaming` and
  `InflatingByteStream`.
- **`tools/pack-builder/provision_pack.py`**: cuts an SD-ready pack for a
  dropped pin and radius from an existing pack, deriving all provenance from
  the source pack's manifest.
- **World overview builder** also derives the z0-z4 and z0-z5 packs;
  z0-z4 is the firmware-embedded world candidate.
- **`tools/embedded-world-check`**: opens a world pack through a
  partition-shaped byte source and frames it with `LocationPicker`.
- `docs/ORCSDR_INTEGRATION.md`: the interface OrcSDR can rely on.

### Changed

- `PmTilesReader(ByteSource*, int leaf_cache_slots = 2)`: the leaf-directory
  cache holds decompressed directory bytes (~21 KiB per slot) instead of
  parsed entries, and its slot count is a constructor argument (1 or 2).
- PMTiles directories are searched in their serialized form instead of being
  expanded into entry vectors.
- `DecompressPayload` keeps its ~8 KiB inflater state on the heap instead
  of the calling task's stack.

### Fixed

- World overview manifests used bounds slightly outside the engine's
  Mercator limit, so runtime discovery rejected them.
- OpenMapTiles-profile packs credited only OpenStreetMap, not the
  OpenMapTiles CC-BY grant, and the credit text was double-encoded.
- The host tests did not compile with GCC 13 (missing `<algorithm>`), and
  the consumer gate, `pack-inspect` and `pack-verify` did not link after
  streaming decode landed (missing `mvt_stream.cpp`).

### Known limitations

- The streaming path does not verify the gzip CRC-32 of a tile.
- No tile cache and no overlay API yet.
- Feature classification is still `experimental::TryClassifyFeature`,
  and the renderer draws nothing without it.

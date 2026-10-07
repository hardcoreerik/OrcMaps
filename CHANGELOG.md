# Changelog

OrcMaps follows semantic versioning with pre-1.0 discipline (see
`PROJECT_TRUTH.md` "Public API and Versioning"): before 1.0 a public API may
change, but only in a new minor version, and every such change is listed here.
Consumers pin a release by its full commit SHA (`docs/ORCSDR_INTEGRATION.md`).

## Unreleased

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

### Added

- `RenderTarget::FillPolygonRings(xy_pairs, ring_point_counts, ring_count,
  color)`: one polygon made of several rings, even-odd. It has a default
  implementation that fills only the first ring, which is the previous
  behaviour, so an existing `RenderTarget` keeps compiling and drawing as
  before. The host `FramebufferTarget` and the M5GFX `DisplayTarget` override
  it; `FillPolygon` is now the single-ring case.

### Removed

- The "polygons are filled from their first ring only" known limitation.

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

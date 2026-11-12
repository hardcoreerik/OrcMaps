# U.S. state regional map pack production

Status: **50 build targets defined, builder and integrity tests implemented. No
50-state prebuilt PMTiles catalog has been published.** This is production
tooling in OrcMaps, not a new map renderer or an OrcSDR firmware integration.

## Included

- data/catalog/us-states.json: all **50 states** with approximate padded
  geographic bounds, regional zoom targets, and US-XX identifiers.
  This is a **build-target catalog, not a download index**.
- tools/pack-builder/build_us_state_packs.py: batch production driver for
  the existing build_regional_pack.py tool; reuses OrcMaps PMTiles v3 manifests,
  SHA-256, data provenance, attribution and immutable sidecars.
- tests/test_us_state_packs.py: validates all states, antimeridian, geographic
  source coverage, no-write default, source integrity, and resume behavior.
- Generated us-states-built.json: **verified local artifact inventory** with
  downloadable set to false; never publish it as a remote catalog without
  uploading and independently checking the archives.

Alaska spans the International Date Line. It uses **two** non-crossing map
archives (us-ak-americas and us-ak-aleutians), making **51 archives for the
50 states**. They share the US-AK region and should be distributed together.
Hawaii is a normal single archive. DC and territories are not included.

Bounding boxes were derived from simplified state polygons and padded
approximately 0.15 geographic degrees. They are approximate production
footprints, not legal/survey boundaries. Geometry reference:
https://github.com/PublicaMundi/MappingAPI/blob/master/data/geojson/us-states.json.
Alaska is deliberately split instead of making a world-spanning bounding box.

## Map detail policy

The world overview supports z0-7; regional packs add context at higher zooms;
optional separately provisioned city packs provide maximum local detail.

| Tier | Default zoom | Intended use |
| --- | --- | --- |
| World overview | z0-7 | Shared global fallback |
| Most states | z7-11 | Cities, major roads, water, labels |
| Small states and Hawaii | z7-12 | Compact-state detail |
| Rhode Island | z7-13 | Higher local detail |
| Alaska (both archives) | z7-10 | Size-conscious regional map |
| Optional city/metro | z12-15 | Detailed local use |

These are **targets, not measured sizes or independently validated visual
quality**. Actual features, roads and labels depend on the PMTiles source.
The --max-zoom option can override the default if the source supports it.
Higher levels can greatly increase storage for large states. Do not commit
all 51 large map binaries to the engine repository or bundle them in firmware.

## Source requirements

Use a **local**, legitimate OpenMapTiles 3.16 / PMTiles v3 source archive with
a matching OrcMaps .manifest.json that records the data snapshot, size, hash,
bounds, zoom, provenance, and attribution. A source must fully contain every
requested target rectangle and zoom. The tool refuses incomplete coverage.

Building all 50 at once requires a large source that actually includes Alaska,
Hawaii and the lower 48. A regional Oregon source is suitable for --state OR
only when it covers the expanded bounding box. OSM .osm.pbf input must first
be converted to a supported PMTiles source; it is not a substitute for the
--source-archive parameter.

Real builds verify the source size and SHA-256 once before extraction.
The underlying build script enforces source provenance and preserves OSM and
OpenMapTiles credit/ODbL obligations. Never scrape OSM's raster tile server.

The go-pmtiles (pmtiles extract) CLI must be installed on the host. Once
source data and the executable are provisioned, all builds are offline.

## Commands

From the OrcMaps root, preview an Oregon build without writing anything:

~~~sh
python tools/pack-builder/build_us_state_packs.py \
  --source-manifest data/local/source-us.manifest.json \
  --state OR
~~~

Build a three-state set (requires sufficient source bounds and zoom):

~~~sh
python tools/pack-builder/build_us_state_packs.py \
  --source-manifest data/local/source-us.manifest.json \
  --state OR --state WA --state CA \
  --output-dir data/local/us-states \
  --execute
~~~

Plan all 50 states (51 geographic targets), then add --execute once source
coverage, storage and extraction effort have been reviewed:

~~~sh
python tools/pack-builder/build_us_state_packs.py \
  --source-manifest data/local/source-us.manifest.json \
  --output-dir data/local/us-states
~~~

An interrupted run can continue with --execute --resume. Existing artifacts
are never replaced by default; --resume verifies complete outputs and
--force explicitly replaces them. A different zoom profile is not silently
accepted by resume.

If a run stops while writing one pack, that pack can be left with only some
of its archive, manifest and checksum files. Resume refuses it, and the
local index lists it under incomplete_parts instead of aborting. Check the
partial files, delete them by hand, then run again with --resume; or rebuild
that one state with --force. Nothing is deleted automatically.

## Validation and SD installation

1. Run:
   python -m unittest discover -s tests -p 'test_us_state_packs.py' -v
2. Inspect data/local/us-states/us-states-built.json. It lists states only
   when all needed local triplets have passed manifest/bounds/zoom/hash
   checks. Its complete_50_states flag does NOT establish that rendering
   has been tested or that remote downloads exist.
3. Run the existing orcmap_pack_verify host tool on the output directory
   (docs/SD_CARD_LAYOUT.md). Check representative visual renders using
   tools/pack-inspect. A hashed archive can still contain poor-quality
   layers or incompatible geometry if source selection was wrong.
4. Copy the desired state's .pmtiles, .manifest.json and .sha256 into the
   **flat** /orcmaps/ directory on an SD card. Copy both Alaska triplets.
   Manual PC copy works; an account, ZIP installer, online tiles or a new
   runtime map engine are unnecessary.
5. The OrcMaps runtime DiscoverPacks scans /orcmaps/; ResolvePack chooses
   the highest-priority eligible local archive by bounds and zoom. State
   packs use priority 10; local city packs may use priority 20. Current
   ResolvePack does NOT stitch adjacent state packs into a seamless mosaic.
   A viewport spanning a state edge may require a larger regional pack or
   the world overview.

## OrcSDR catalog publication handoff

This work does **not** add a second embedded mapping catalog to OrcSDR.
OrcMaps owns pack format/production; OrcSDR owns any future PC/web
distribution interface, download queue and installation flow.

Before marking a map as **downloadable** in OrcSDR:

1. Build every required part, validate archive structure, checksums,
   attribution and representative actual map renders.
2. Upload the immutable triplets to reliable hosting and independently
   verify the served bytes and hashes; record real URLs and file sizes.
3. Publish only successfully hosted states with the exact manifest,
   archive and checksum URLs, version, zoom, attribution and sizes.
   Alaska must not appear complete when one half is missing.
4. Use the existing OrcSDR install/SD workflow and maintain manual PC
   installation, safe replacement, and totally offline runtime behavior.

This is the first production-ready step toward a 50-state library; it does
**not** claim that 51 map binaries have already been built or published.

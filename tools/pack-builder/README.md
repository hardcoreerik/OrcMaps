# Pack builders (host-only)

## Natural Earth world-overview sources

`world_overview_sources.json` pins the 21 official Natural Earth 5.1.2
archives used for the proposed 110m/50m/10m overview tiers. Acquire and
verify them outside Git with:

```
python tools/pack-builder/acquire_world_overview_sources.py \
  --destination data/local/world-overview/natural-earth
```

Use `--dry-run` to list the destination, total bytes, and URLs without
network access. The script preserves the ZIPs, extracts the required
shapefiles, verifies size and SHA-256, and writes local `SOURCE.json` and
`SHA256SUMS.txt` records. Existing invalid archives are refused unless
`--force` is explicit. This acquires source data only; it does not build a
PMTiles pack or select final zoom cutoffs.

## Regional packs

`build_regional_pack.py` extracts a bbox or GeoJSON region from an existing
PMTiles archive using the official `go-pmtiles` CLI, then writes the immutable
OrcMaps triplet:

```
<name>.pmtiles
<name>.manifest.json
<name>.sha256
```

It requires all identity, provenance, compatibility, and attribution inputs;
it never guesses them or contacts a metadata service. `--dry-run` prints the
exact command and deterministic pack ID without creating files. Existing
artifacts are refused unless `--force` is explicit. The content profile is
manifest metadata: extraction preserves the source archive's layers and does
not pretend to transform one schema/profile into another.

Run `python tools/pack-builder/build_regional_pack.py --help` for the complete
noninteractive interface. Example:

```
python tools/pack-builder/build_regional_pack.py \
  --source D:/maps/planet.pmtiles \
  --bbox=-124.7,41.9,-116.4,46.3 --min-zoom 0 --max-zoom 14 \
  --content-profile standard --output D:/maps/oregon.pmtiles \
  --region-id US-OR --region-name "Oregon, USA" --display-name Oregon \
  --source-snapshot 2026-09 --source-version 2026-09 --source-label planet.pmtiles \
  --source-sha256 <verified-source-sha256> --provenance-id openstreetmap \
  --acquired 2026-09-14 --pack-version 2026.09.1 --pack-class open \
  --attribution "© OpenStreetMap contributors" \
  --attribution-link https://www.openstreetmap.org/copyright \
  --builder-version 1.28.2 --builder-commit <orcmaps-commit>
```

The `go-pmtiles` executable must be on `PATH`, or supplied with
`--pmtiles-cli`. It is a host provisioning tool, never a device/runtime
dependency.

## Springfield / 97477 source build

Reproducible OSM → PMTiles path for the first real-geography host preview.
This is **pack production**, not an OrcMaps runtime dependency. Planetiler
is not vendored and is not linked into the engine.

Do not treat the resulting layer schema as the OrcMaps tile-content
decision. Inspect tiles with `tools/pack-inspect` first.

## Geographic test area

Springfield, Oregon / ZIP 97477 urban bbox (WGS84, west/south/east/north):

```
-123.055, 44.030, -122.960, 44.090
```

Center used for the 1280×720 preview (not hardcoded in core):

```
44.0500 N, 123.0220 W
```

This bbox is test geography. It is not an application concept.

## Source (provenance)

| Field | Value |
|---|---|
| Provider | Geofabrik OSM extract |
| URL | https://download.geofabrik.de/north-america/us/oregon-latest.osm.pbf |
| Format | `.osm.pbf` (OSM Protocolbuffer Binary) |
| License | ODbL 1.0, © OpenStreetMap contributors |
| Provenance record | `data/sources/openstreetmap.json` (`CONFIRMED`) |
| Acquisition | raw extract; **not** `tile.openstreetmap.org` raster tiles |

Record the download date and SHA-256 in `data/local/SOURCE.txt` when you
run the script.

## Toolchain

| Tool | Role | Notes |
|---|---|---|
| Planetiler 0.10.2 | OSM PBF → PMTiles v3 / MVT | `planetiler.jar` (OpenMapTiles profile 3.16.0). Requires Java 21. Build-tool only. |
| OrcMaps `pack-inspect` | inspect / preview | Uses the engine; not a tiler. |

Exact versions are printed by `build_springfield_pack.ps1`.

## Output contract

```
PMTiles v3
TileType = MVT
tile_compression = gzip
maxzoom = 15
```

Brotli/zstd are not used. OrcMaps does not implement those codecs.

## Commands

From the repository root (PowerShell, Temurin JDK 21):

```
pwsh tools/pack-builder/build_springfield_pack.ps1
```

Artifacts land in `data/local/` (gitignored):

```
data/local/oregon-latest.osm.pbf
data/local/springfield-97477.pmtiles
data/local/springfield-97477.manifest.json
data/local/springfield-97477.sha256
data/local/SOURCE.txt
```

Then:

```
cmake -S tools/pack-inspect -B build-pack-inspect -G "Visual Studio 18 2026"
cmake --build build-pack-inspect --config Release
build-pack-inspect/Release/orcmap_pack_inspect.exe header data/local/springfield-97477.pmtiles
build-pack-inspect/Release/orcmap_pack_inspect.exe preview data/local/springfield-97477.pmtiles --out data/local/springfield-97477-orcsdr-dark.ppm
```

## Natural Earth world overview

`build_world_overview.py` builds one z0-z8 master from the already provisioned
Natural Earth 5.1.2 files, then uses official go-pmtiles v1.28.2 to derive z7
and z6 candidates, plus the z5 and z4 firmware-fallback candidates. It validates the local source hashes, source record, Java,
Planetiler, and go-pmtiles versions before running. It never downloads data or
tools.

```powershell
python tools/pack-builder/build_world_overview.py `
  --source-root data/local/world-overview/natural-earth/5.1.2 `
  --planetiler-jar data/local/planetiler.jar `
  --pmtiles-cli data/local/tools/go-pmtiles/v1.28.2/pmtiles.exe `
  --dry-run
```

Remove `--dry-run` to build. Existing immutable outputs are refused unless
`--force` is explicit. PMTiles, manifests, checksums, compiled classes, and
renders remain under ignored `data/local/world-overview/build/`.

## provision_pack.py -- pin to SD card

`provision_pack.py` is the PC half of a device setup wizard. Where
`build_regional_pack.py` takes full provenance on the command line,
this takes a pin and a radius and derives everything else from the
**source pack's own manifest**, so a cut pack cannot record a provenance
that disagrees with what it was cut from.

```bash
python tools/pack-builder/provision_pack.py \
  --source-manifest data/local/oregon.manifest.json \
  --lat 44.0521 --lon -123.0867 --radius-km 50 \
  --name home-eugene --display-name "Home: Eugene OR" \
  --sd-root G:/ --builder-commit "$(git rev-parse HEAD)"
```

It writes `<sd-root>/orcmaps/<name>.pmtiles` plus the `.manifest.json` and
`.sha256` sidecars **on the same filename stem**, which is how
`src/core/pack_discovery.cpp` pairs an archive to its manifest. Renaming one
without the others is the single mistake that silently breaks discovery, so
staging verifies the copied archive against the manifest's size and SHA-256,
then publishes the archive and checksum before publishing the manifest last.
The checksum sidecar is generated from the verified digest. Existing files on
any of those three names are refused, including when `--force` was used for
the separate build. Use a new versioned name rather than replacing a live pack.

Build output defaults to the host's working directory. `--work-dir` must not
be the card's `orcmaps` directory: direct extraction there bypasses staging.
Remove the card from the device and give this tool exclusive access while
provisioning, then safely eject it before reinserting it.

Staging uses a hidden directory and per-name reservation. Ordinary copy/hash/
rename failures remove only files created by that attempt. Sudden power loss
can leave a hidden `.install-<name>` reservation or unreferenced archive; do
not delete unrelated files or overwrite them to retry. Inspect the interrupted
attempt offline, or choose a new versioned name. File flushes and manifest-last
publication do not establish FAT directory durability under power loss; that
still requires hardware fault testing. This is host-side provisioning, not
the device's future resumable network installer or trusted catalog.

Behaviour that matters:

- A pin outside the source pack's coverage is **refused**, rather than
  producing an empty pack that looks valid.
- A box that overruns the source is **clamped to the source's coverage**, so
  the manifest never claims tiles the archive lacks (the device compares the
  two and rejects a mismatch).
- The requested zoom range is **narrowed** to what the source holds.
- stdout is machine-readable JSON; the `pmtiles` CLI's progress goes to
  stderr so a wizard can parse the result.

Measured on an 84 MB Oregon source, z1-13 (go-pmtiles 1.28.2):

| Radius | Tiles | Pack size | Time |
| --- | --- | --- | --- |
| 25 km | 384 | 4.06 MB | 276 ms |
| 50 km | 1,218 | 7.34 MB | 249 ms |
| 100 km | 4,526 | 20.30 MB | 295 ms |

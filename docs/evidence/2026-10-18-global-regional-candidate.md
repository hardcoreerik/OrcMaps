# Global regional candidate — October 18, 2026

Assessment candidate, not an installed or published OrcSDR pack. No firmware or SD writes.

## Actual archive and acquisition

User explicitly approved one local download capped at 600 MB. Extracted zooms 0–8 from the official documented Source Cooperative mirror, using go-pmtiles 1.28.2. A local range proxy enforced an aggregate 600,000,000-byte limit, explicit ranges, HTTP 206, exact Content-Range/Length and a fixed source ETag. Range-guard self-checks rejected missing/open/reversed/over-limit/multi-range requests before acquisition.

- Source: `https://data.source.coop/protomaps/openstreetmap/v4.pmtiles`.
- Source object bytes: 134,812,420,554, header zooms 0–15.
- Source ETag: `"9e09dd251e816c0ea9208e1e34c28eba-2572"` (object identity, **not** a SHA-256).
- Source Last-Modified: March 31, 2026; this is not a verified date of the underlying OSM snapshot.
- Extracted archive: `.local/global-assessment/global-z8-candidate.pmtiles`.
- Actual archive bytes: **543,380,195** (about 518.2 MiB).
- SHA-256: `16adde0b4f67734535db6871b89949e68c7e1dabf1c34d2bd7bd23842a9e5991`.
- Actual remote range bytes: **543,439,268**, five requests, no proxy errors, below cap.
- Verified CLI ZIP: 13,303,524 bytes, SHA-256 `6276bc64c4499c9af12dc90accc84d9560061c4be4886fa1e738316258ef284d`, matched official GitHub asset digest before execution. Extraction command uses `--maxzoom=8 --download-threads=1 --overfetch=0`.
- `pmtiles.exe verify .local/global-assessment/global-z8-candidate.pmtiles`: exit 0, completed structure verification. This does not validate every MVT or prove upstream full-object integrity. The full 134 GB source was not downloaded or hashed.

Host invocation directory: `F:/AI/OrcSDR-TEMP/orcmaps-quality-delivery`. Local acquisition script `.local/global-assessment/download_capped.py`; identity/metadata `.local/global-assessment/mirror-metadata.json`; result `.local/global-assessment/download-result.json`. Logs: `global-z8-download-cli-20261018.log`, `global-z8-download-guard-20261018.log`, `global-z8-verify-20261018.log`.

## Size choices measured from directory dry runs

The official CLI's `--dry-run` estimates: z0–7 about 185 MB, z0–8 about 543 MB, z0–9 about 1.5 GB. Despite its wording “Extract transferred”, dry-run output describes prospective transfer; candidate files were absent afterwards. Only z0–8 was actually acquired under approval.

The 185 MB cutoff cannot retain county geometry that first appears at zoom 8 in this profile. The upstream boundary generator currently sets county administrative levels 5/6 to zoom 8, country/state OSM lines to zoom 6, locality to zoom 10. These are current-source observations, not proof of the exact older mirror's generator revision. Actual z8 samples below contain county features. A center tile without a border does not imply missing county coverage: Eugene z9 lacks a boundary in its center tile, but its z8 regional tile has county boundaries.

## Bounded worldwide content inspection

Built the current-source host inspector once with CMake/GCC under WSL, from this worktree (`10fc262` plus two diagnostic fields). Logs `pack-inspect-config-20261018.log`, `pack-inspect-build-20261018.log`: exit 0. The tool now reports `kind_detail`, `admin_level` and `min_zoom`, allowing administrative detail inspection.

Eleven z8 tiles decoded without reported read/decompression/MVT errors: the eight views in `tools/pack-builder/global-base-acceptance.json`, plus northern Lane County and both sides of the antimeridian. Each regional view has earth, landuse, places, roads and water; county boundary kinds occur at all eight regional views. Tokyo, Nairobi, Delhi and Suva samples also include region boundaries. Ocean/island seam samples need not have places or boundaries in every tile. Landcover is present in lower-zoom samples; the z8 archive uses landuse for higher-zoom cover.

Data log: `global-z8-reference-tiles-20261018.log`; structured results `.local/global-assessment/reference-tiles.json`. Source detail samples and SHA-256 values: `.local/global-assessment/sample-manifest.json`, `global-mirror-samples-20261018.log`. Those six single-tile archives total 699,459 bytes; two additional Eugene z10/11 samples are recorded separately.

**No rendering compatibility claim:** Protomaps v4 uses a different schema from OpenMapTiles 3.16. Inspections explicitly pass `--schema protomaps-v4`, which this classifier does not support, leaving kinds unassigned. Geometry and properties can be decoded; the current engine cannot correctly style/render these as a supported profile. The existing inspector ignores the classifier's false return; this is an inspection limitation, not evidence of compatibility. Do not publish a manifest claiming OpenMapTiles for this archive.

## Names, ownership and provenance

Local reference hashes were independently calculated from current files (not copied from filenames): Oregon raw OSM, 253,592,262 bytes, SHA-256 `5511e363f0cfdc41ac3d6a9b34668b6a34a2131ae88cc638c7f8d818190c6ddc`, matching its recorded acquisition hash; Oregon PMTiles, 84,615,534 bytes, `27ce8cbde3d821a015cda5b3792e9f70911b1befc9f07592f2122a342b6cef3c`; Springfield PMTiles, 3,507,636 bytes, `8bf23873915668f41d098b98df32b11a6d08ec6754a63d885fce2f29abe4adfd`. Machine-readable local record: `.local/global-assessment/reference-source-hashes.json`.

User chose **English names where available, then local names**. Preserve original UTF-8 names. Glyph support and fallback reporting must be verified; no silent blank/replacement-glyph acceptance.

This candidate is a prebuilt Protomaps-derived dataset, not a raw OSM acquisition under our existing raw-source record. Production needs an explicit provenance/license entry for the actual inputs and derivation. Protomaps documents this as an ODbL Produced Work with OSM attribution. It is not Google map content. Our catalog/immutable artifacts must be under our control; this mirror is an assessment input, not a device hotlink dependency. The upstream generation code is open, but we have not reproduced this planet build locally.

References checked October 18:

- https://docs.protomaps.com/basemaps/downloads
- https://docs.protomaps.com/basemaps/layers
- https://source.coop/protomaps/openstreetmap/v4.pmtiles
- https://github.com/protomaps/go-pmtiles/releases/tag/v1.28.2
- https://github.com/protomaps/basemaps/blob/main/tiles/src/main/java/com/protomaps/basemap/layers/Boundaries.java

## 20,000-user delivery model, not a hosting purchase

One full download each: **10,867,603,900,000 bytes (10.868 decimal TB)**. Three retained global versions: 1,630,140,585 bytes before local packs and other artifacts. One logical base does not require 20,000 separately stored copies or per-user builds.

As a replaceable hosting comparison, current R2 Standard lists $0.015/GB-month storage, $0.36/million read operations, free Internet egress and a shared monthly free tier of 10 GB-month/10 million reads. Billing rounds to units. Three global versions and 20,000 full GETs plus one HEAD/catalog read each would fit those allowances **if no other account usage consumes them**. This is not a promise that running the service costs nothing: build compute, domains, signing/catalog services, taxes, monitoring, local packs, retries, abuse and other usage are excluded. A 1 MiB-per-request resume design would cause approximately 10.38 million GETs for these downloads; larger streamed HTTP ranges reduce request count without requiring equally large RAM buffers. Actual install chunking remains to be measured on P4.

Pricing reference: https://developers.cloudflare.com/r2/pricing/ (checked October 18). No account, bucket, deployment, spend, or publication created.

## Remaining gates

Select/verify the actual input profile or reproducible compatible production build, implement labels and boundary styling, inspect full viewports and all required zooms, measure Tab5 under radio reception, verify safe download/activation and source licensing, then obtain publication/hosting approval. Structural verification and eleven tiles do not prove all 33,167 tile entries or full-world quality. This candidate establishes a real size/coverage baseline; it does not close Phase 1 or release gates on its own.

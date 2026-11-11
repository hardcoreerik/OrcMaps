# Owned catalog builder and strict preflight

Changes: `tools/pack-builder/build_map_catalog.py`, its `MAP_CATALOG.md`,
`tests/test_map_catalog_builder.py`, `tools/pack-verify/main.cpp` and
`tests/test_pack_verify_cli.py`. These are host tools/tests only; no engine
rendering API, dependency pin, firmware, device or SD content changed.

The catalog contains the existing engine manifest and content-addressed HTTPS
mirrors, size and SHA-256. The builder checks full archive hashes, requires
the actual engine preflight, signs canonical bytes using operator-supplied
P-256 material and verifies the resulting signature. Output paths cannot be
supplied by remote catalog entries. See the tool contract for exact limits.

## Actual verification

Run from the OrcMaps worktree root:

```powershell
python -m unittest discover -s tests -p test_map_catalog_builder.py
cmake --build .local/global-pack-verify-20261019 --config Debug
$env:ORCMAP_PACK_VERIFY=(Resolve-Path .local/global-pack-verify-20261019/Debug/orcmap_pack_verify.exe).Path
python -m unittest discover -s tests -p test_pack_verify_cli.py
```

- Catalog: **9 tests passed**, including real temporary P-256 signing,
  altered-payload rejection, hash/size errors, duplicate identity/JSON keys,
  unsafe/mutable URL forms, revision/time checks, engine rejection and refusal
  of old warning-only verifier success. Keys were generated inside a temporary
  test directory and removed by its cleanup; no production key was accessed.
- Verifier CLI: **2 tests passed**, with valid actual PMTiles root data,
  unreadable header, wider declared bounds and wider zoom cases.
- The verifier bug was reproduced first: a 127-byte zero archive with valid
  manifest metadata printed "not a valid PMTiles container" and returned 0.
  It now returns failure for unreadable headers or overclaimed bounds/zoom.
  Runtime discovery behavior is unchanged; host publication preflight is
  deliberately stronger. The new contract marker lets the builder reject an
  older verifier that would report false success.

Saved root logs:
`map-catalog-builder-red-20261019.log`,
`map-catalog-builder-green-20261019.log`,
`pack-verify-header-red-20261019.log`,
`pack-verify-header-build-20261019.log`,
`pack-verify-header-green-20261019.log`.

## Real global candidate

The existing 543,380,195-byte global archive was hashed by the catalog builder,
passed engine preflight and was signed/verified using an ephemeral test key.
The private test key was removed at the end of the smoke run. Artifacts under
`.local/catalog-smoke-20261019/TEST-ONLY-output` are **test-only**, use reserved
`example.test` URLs and are not a production trusted catalog. Log:
`map-catalog-global-smoke-20261019.log`.

The final strict catalog-to-engine preflight was also exercised on that same
real candidate after adding the contract marker, and passed. Log:
`map-catalog-strict-preflight-20261019.log`. No archive download or copy,
hosting, push, release, SD write or device operation occurred.

## Remaining work

This establishes host catalog generation, not device authentication. OrcSDR
still needs the bounded signed-payload consumer, approved trust-key/endpoint
configuration, freshness/rollback handling, HTTP/SD download-resume worker,
activation recovery and setup wiring. Production hosting and publication remain
unapproved. No full host suite or native build was needed for these tool-only
changes; actual Tab5 download behavior remains untested.

## Firmware-consumer contract alignment

The following integration step added a bounded OrcSDR consumer and compared
its accepted language with this builder. The builder now refuses percent-
encoded/non-ASCII paths, malformed DNS labels, dates before 1970, NUL strings,
more than 4096 JSON nodes/128 object members and nesting beyond 16 containers.
These are explicit v1 constraints, documented in `MAP_CATALOG.md`, rather than
silently producing signed catalogs the device would reject. Regressions
reproduced the unsupported URL and structure acceptance before correction.

Actual final command: `python -m unittest discover -s tests -p test_map_catalog_builder.py`.
Result: **10 tests passed**; root log
`map-catalog-consumer-contract-green-20261019.log`. Separate red logs are
`map-catalog-consumer-contract-red-20261019.log` and
`map-catalog-structure-red-20261019.log`.

OrcSDR's actual crypto consumer accepted the existing test-only global catalog
payload/signature/public-key triplet with exit 0. That integration-side log is
`map-catalog-consumer-interoperability-20261019.log`. No production publication,
key or device operation was involved.

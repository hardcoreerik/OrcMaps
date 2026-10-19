# Explicit Protomaps v4 pack compatibility

User approved source and focused host tests only. Branch
`codex/map-quality-delivery`, base HEAD 63f0573; changes uncommitted.
No full suite, push, release, consumer pin update, archive transfer or device use.

The renderer's explicit Protomaps-v4 classifier existed, but pack validation
accepted only OpenMapTiles and the overview profile. Added `protomaps-v4` to
the exact supported-schema list in `src/core/pack.cpp`. No broad schema guessing,
manifest migration, PMTiles layout change or validation bypass.

Changed files: `src/core/pack.cpp`, `tests/host/test_pack.cpp`,
`tests/host/test_pack_discovery.cpp`, `tests/host/CMakeLists.txt`, new focused
runner `tests/host/pack_main.cpp`, and this evidence note.

Tests written first failed: valid Protomaps manifests were incompatible;
discovery added no valid pack and reported compatibility instead of archive
size failure. After the one-profile allowance, focused validation/discovery
checks pass. Unknown profiles, bad identity, invalid checksum metadata, zero
size and invalid bounds still reject; discovery accepts the good Protomaps
manifest while rejecting its wrong-sized neighbour. Existing pack/resolver/JSON
and discovery safety cases run in this focused executable as well.

Commands from this worktree root under WSL:

```sh
cmake -S tests/host -B .local/pack-profile-host
cmake --build .local/pack-profile-host --target orcmap_pack_tests
ctest --test-dir .local/pack-profile-host -R '^orcmap_pack_tests$' --output-on-failure
```

Result: 1/1 CTest executable passed. Logs in root:
`pack-profile-config-20261018.log`, `pack-profile-red-build-20261018.log`,
`pack-profile-red-20261018.log`, `pack-profile-green-build-20261018.log`,
`pack-profile-green-20261018.log` (and final namespace-cleanup build/test logs).

Limits: discovery validates manifest metadata and archive size; it does not
calculate archive SHA-256 or attest to tile schema/content. Tests use synthetic
manifest/archive inventory, plus the existing temporary host filesystem case.
This is no claim that a complete global pack was installed or quality accepted.
Retain separate hash/provenance, real archive rendering, font/boundary quality,
catalog trust and device gates. OrcSDR's current pin remains unchanged and still
rejects this profile at discovery until a verified upstream revision is pinned.

Follow-up header check read only 127 bytes of the actual downloaded archive.
Recorded `global-header-compatibility-20261018.json`: PMTiles v3/MVT,
543,380,195 bytes, z0–8, E7 bounds [-1800000000,-850511287,1800000000,850511287].
These fit current manifest limits without rounding/clamping changes. Focused
tests now use these dimensions/size, resolve a distant region at zoom 8, and
refuse zoom 9 rather than claiming missing detail. Result 1/1 passed; logs
`pack-profile-global-bounds-build-20261018.log` and
`pack-profile-global-bounds-20261018.log`. This did not rehash the archive or
inspect tiles; discovery tests still use a fake filesystem inventory.

## Approved full configured host regression

User separately approved one full host CTest run. Tested code checkpoint
`dbb1b98b8eebbb8b30ed57b37c87ea4d76c66800` on the clean tracked worktree.
From worktree root under WSL:

```sh
cmake --build .local/pack-profile-host -j 2
ctest --test-dir .local/pack-profile-host --output-on-failure
```

Result: all 7 configured CTest programs passed (pack, frame transform, labels,
profile classifier, screen bounds, tile composition and general host tests).
Logs in root: `pack-profile-full-build-20261018.log` and
`pack-profile-full-tests-20261018.log`. No build/test failures. This run does
not include separately configured font backends, Python builder/provenance
suites, firmware linking, device operation or full global-tile acceptance.
No push, consumer pin update, archive transfer or publication occurred.

# Boundary hierarchy candidate — October 19, 2026

Source implementation only; not committed, published, integrated into OrcSDR,
or accepted on hardware at this checkpoint.

## Source schema and rendering policy

- Protomaps documents `boundaries` kinds and numeric `kind_detail` at
  <https://docs.protomaps.com/basemaps/layers>.
- OpenMapTiles documents the boundary layer and `admin_level` at
  <https://openmaptiles.org/docs/schema/>.

This renderer's policy appends country, region and local administrative
categories without changing existing enum values. Protomaps matches exact
space-separated `country`, `region`, `county` and `locality` tokens; combined
categories take country before region before local. Unknown kinds remain generic;
numeric `kind_detail` is preserved and does not override an unknown kind.

For the explicit OpenMapTiles profile only, strict integral `admin_level` 2
maps to country, 3–4 to region and 5–10 to local. Signed/unsigned integers,
integral finite doubles and canonical decimal strings are accepted. Bool,
fractional/nonfinite values, malformed/noncanonical strings and values outside
2–10 remain generic. These are visual categories, not a claim that every
country's administrative divisions correspond to states or counties.
The unprofiled legacy classifier and embedded overview retain generic boundaries.

All four built-in styles define explicit restrained colors and widths;
country starts at zoom 0, region at 4, local at 6. These are candidate display
values pending reference-render and panel acceptance. Style versions increase
from 1 to 2 to invalidate rendered caches. Night Red uses red-dominant colors.
No road/land/water palette, renderer API, pack format or consumer changes.

## Executed verification

Invoked from `F:/AI/OrcSDR-TEMP/orcmaps-quality-delivery` using WSL:

```sh
cmake -S tests/host -B .local/boundary-host-20261019
cmake --build .local/boundary-host-20261019 --target orcmap_profile_tests -j2
ctest --test-dir .local/boundary-host-20261019 -R '^orcmap_profile_tests$' --output-on-failure
```

Result: **1/1 focused profile test passed**, zero failures (CTest 0.03 seconds).
Coverage includes combined/exact categories, known Protomaps kinds, unknown
fallback, numeric variants, malformed values, preserved properties, legacy
behavior and batch/stream classifier agreement.

Logs beside the invocation directory:

- `boundary-profile-config-20261019.log`
- `boundary-profile-build-20261019.log`
- `boundary-profile-tests-20261019.log`

Follow-up focused style approval exercised only `RunStyleTests()` using a tiny
local harness; shared CMake and the full host executable were not changed:

```sh
c++ -std=c++17 -Iinclude -Itests/host .local/boundary-host-20261019/style_main.cpp tests/host/test_style.cpp src/render/style.cpp -o .local/boundary-host-20261019/style_tests
.local/boundary-host-20261019/style_tests
```

Result: **focused style tests PASS, zero failures**. Build/test logs are
`boundary-style-build-20261019.log` and `boundary-style-tests-20261019.log` in
the worktree root. Source inspection confirms all four built-in constructors
invoke `ApplyStandardZoomGating`, which supplies the 0/4/6 boundary policy;
the focused tests exercise visibility thresholds and style versions for all four.

## Remaining gates

Focused style tests pass for hierarchy gating/widths/colors, preserved enum
identities and style-version cache invalidation; existing Night Red iteration
also includes new rules. The broader host executable was outside this approval.
`TestScreenFeatureMatchesDirectRender` now includes all three new categories as
separate visible stroked lines in its direct-with-scratch versus projected
screen-cache pixel comparison. Only the test's local style copy lowers their
minimum zoom to 0; production gating is unchanged. It asserts actual pixels for
each category as well as pixel equality. **This parity test was added but has not
been compiled or run.** Even a passing synthetic parity test would not establish
real-archive streaming acceptance.
Immediate/streamed/cached pixel agreement, eight full worldwide viewport
comparisons, broader shared-engine regressions, adapter/native builds, active-RF
performance and physical readability remain unverified. No device was accessed.

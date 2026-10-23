# Caller-bounded feature replay — October 23, 2026

Local implementation only. No branch push, consumer pin change, native build,
firmware flash or hardware speed claim is included in this checkpoint.

## Implementation

FeatureReplay encodes complete Features into caller-owned fixed-capacity memory.
Coordinates use exact signed deltas and varints. IDs, classification, extent,
layer, all paths, property keys and every PropertyValue alternative are retained.
This is a transient in-memory representation, not a pack format or SD layout.
Append allocates nothing and publishes size/count only on success. An exhausted
append leaves earlier complete records replayable; the owner must discard an
incomplete tile rather than count a complete prefix as a complete tile.

Replay reconstructs one caller-owned scratch Feature at a time. A false sink
result cancels immediately. Malformed records are rejected before their callback.
Allocation exceptions propagate to the existing owner's recovery boundary, as
with normal streamed Feature callbacks. The owner supplies lifetime, exclusive
access, archive/tile identity, eviction, invalidation, complete-tile publication,
allocation-failure fallback and platform placement/reclaim. No cache manager is
claimed to exist yet. Do not mutate supplied bytes during replay.

Files: include/orcmap/feature_replay.hpp, src/tiles/feature_replay.cpp,
tests/host/test_feature_replay.cpp and component/host CMake registration.
STATUS.md distinguishes this focused run from the earlier eight-program suite.

## Focused tests

Commands below run from the OrcMaps worktree; output files are beside invocation.

```text
wsl -d Ubuntu -- bash -c 'cd /mnt/f/AI/OrcSDR-TEMP/orcmaps-quality-delivery && cmake --build .local/feature-replay-host-20261023 --target orcmap_feature_replay_tests -j2 && ctest --test-dir .local/feature-replay-host-20261023 -R ^orcmap_feature_replay_tests$ --output-on-failure' > maps-feature-replay-allocation-cmake-20261023.log 2>&1
wsl -d Ubuntu -- bash -c 'cd /mnt/f/AI/OrcSDR-TEMP/orcmaps-quality-delivery && g++ -std=c++17 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude tests/host/test_feature_replay.cpp src/tiles/feature_replay.cpp -o .local/feature_replay_allocation_sanitized_20261023 && ASAN_OPTIONS=detect_leaks=1 .local/feature_replay_allocation_sanitized_20261023' > maps-feature-replay-allocation-sanitized-20261023.log 2>&1
wsl -d Ubuntu -- bash -c 'cd /mnt/f/AI/OrcSDR-TEMP/orcmaps-quality-delivery && g++ -std=c++17 -fno-exceptions -Wall -Wextra -Werror -Iinclude -c src/tiles/feature_replay.cpp -o .local/feature_replay_noexceptions_20261023.o' > maps-feature-replay-noexceptions-20261023.log 2>&1
python tools/check_documentation_truth.py > maps-feature-replay-doc-truth-20261023.log 2>&1
```

All exit 0. CTest is 1/1, not a full suite. Sanitizers report no errors; leak
detection is enabled. The portable no-exceptions compile is not a P4 link check.
Documentation truth reports zero errors/warnings before this evidence entry.
The initial configuration log is maps-feature-replay-cmake-20261023.log.

Checks cover full field round-trip, INT32/INT64/UINT64 extrema, UTF-8 label data,
signed zero, every too-small append capacity, failed-append rollback, empty
storage, Clear, cancellation, malformed committed varint, rejected invalid kind,
Append with allocation prohibited and caught Replay allocation failure before a
callback receives an incomplete feature. Scratch allocations are not part of
the caller-owned arena's byte cap and must be budgeted separately on hardware.

## Real-archive comparison with labels

The integration worktree's host harness compares the actual new core against
the unmodified streaming Pipeline using the existing global-z8 archive. Center
44.0628866,-123.0160273, requested zoom 9/source 8, dimensions 842 by 330.
Starting view and independent pans 40,0; 0,20; -40,0; 0,-20, each with labels
off and on: ten full frames, zero differing pixels. The real FreeType backend
uses DejaVu Sans at size 20; label-on frames draw four or five labels, matching
streaming output. Both paths use the same font, layout and renderer.

Complete feature payload is 702,110 bytes for six tiles. The host harness used
one 1 MiB vector allocation as an arena, not separate growing tile vectors.
The payload fits a proposed 768 KiB device arena, but that allocation and its
remaining scratch/index/memory budgets are not yet tested on P4. Tokyo and
whole-world label-on cases remain open. No timing multiplier is inferred.

Command from the integration worktree:

```text
wsl -d Ubuntu -- bash .local/maps/run_feature_replay_core_archive_20261023.sh > maps-feature-replay-core-archive-corrected-20261023.log 2>&1
```

Exit 0. Source is .local/maps/feature_replay_core_archive_20261023.cpp. The first
build failed because pkg-config could not locate FreeType headers, not because
of a codec error; maps-feature-replay-core-archive-20261023.log is preserved.
The corrected command uses the already-extracted FreeType headers identified by
the previous pack-inspect-labels CMake cache and the existing system runtime.
No package installation or component dependency change was needed.

## Remaining gate

Integrate complete-tile publication and bounded eviction/reclaim into the shared
consumer, retain ordinary streaming on cache/allocator failure, prove cancellation
and archive/camera transitions, then run P4 placement/size and native timing.
The required COM17 p95 refill remains two seconds; current installed 30a00ce1
still has the failing 4.55-second baseline. Full host regressions/publication
retain their separate approval boundaries. Neither main nor a device is changed.

# Conservative off-screen rejection — October 23, 2026

Source candidate after published 2133547; no native build, publication or device
timing is claimed here. The installed consumer remains f38b8601, whose 20-pan
p95 3.549776 seconds fails its two-second gate.

## Scope

RenderOrderedFeatureAt now scans a feature's tile-local bounds and projects only
the two bounding corners before rejecting geometry wholly outside the target's
reported clip. Rejected features allocate no polygon projection or deferred
stroke storage and issue no primitives. Visible geometry still uses the existing
renderer unchanged. A conservative stroke-width margin plus one spare pixel
retains thick strokes with off-screen centerlines. Polygons surrounding the
viewport remain visible even when every vertex is outside it.

Tile clipping is optional in RenderTarget. The rejection uses its reported clip,
not an assumed tile scissor that a legacy target may decline. A regression keeps
buffered geometry from adjacent world copies visible on such targets.

The consumer separately skips classification for replayed Features because the
complete record already carries the classification computed before capture.
Archive/profile and place-layer changes retain their existing invalidation.
No format, public API, layer/detail selection, geometry precision, label policy,
PSRAM buffer or radio-service scheduling change is introduced by this step.

## Focused checks

From the OrcMaps worktree:

```text
wsl -d Ubuntu -- bash -c 'cd /mnt/f/AI/OrcSDR-TEMP/orcmaps-quality-delivery && cmake --build .local/feature-replay-host-20261023 --target orcmap_paint_order_tests orcmap_screen_bounds_tests orcmap_tile_composition_tests -j2 && ctest --test-dir .local/feature-replay-host-20261023 -R "^orcmap_(paint_order|screen_bounds|tile_composition)_tests$" --output-on-failure' > maps-offscreen-rejection-legacy-20261023.log 2>&1
```

Exit 0, 3/3. Added rejection test failed before implementation; red log is
maps-offscreen-rejection-red-20261023.log. The sanitizer-configured paint-order
target also passes 1/1 with ASan/UBSan and leak detection:
maps-offscreen-rejection-sanitized-20261023.log. Existing capacity, cancellation,
allocation-failure and drawing-order assertions still pass.

OrcSDR Pipeline/cache targeted CTest passes 2/2 against this local engine and
the existing global archive: maps-offscreen-integration-20261023.log, invocation
and commands follow the preceding bounded replay evidence. This includes warm
replay equality, labels, overflow fallback, compaction, reopen and cancellation.
P4 syntax checks for map_pipeline.cpp, map_service.cpp and renderer.cpp pass
against local headers: maps-offscreen-p4-syntax-20261023.log. No link is implied.

## Independent renderer oracle

Built one host executable against the exact renderer source from git 2133547
and another against this candidate. All other inputs/code are the same. The
reference copy stays under the integration worktree's .local/maps and is only a
test build artifact; no component source was vendored into the application.

Sixteen complete RGBA outputs compare byte-for-byte equal, not just sampled
pixels: reference center, Tokyo, antimeridian and 1280x720 whole-world views;
each with and without a 40,20 pan and with labels on/off. The actual FreeType
backend draws the same labels. Reference/candidate logs preserve primitive counts
and label counts. For the unpanned reference view, DrawLine calls fall from
6,074 to 4,437 and FillPolygonRings calls from 6,176 to 1,881. These are measured
primitive counts, not a hardware multiplier or proof of the two-second gate.

Commands, from the OrcSDR integration root:

```text
wsl -d Ubuntu -- bash .local/maps/run_offscreen_oracle_reference_20261023.sh > maps-offscreen-oracle-reference-20261023.log 2>&1
wsl -d Ubuntu -- bash .local/maps/run_offscreen_oracle_candidate_20261023.sh > maps-offscreen-oracle-candidate-20261023.log 2>&1
python .local/maps/compare_offscreen_oracle_20261023.py
python .local/maps/check_offscreen_syntax_20261023.py
```

Both executable logs report ORACLE_RESULT PASS views=16. Comparison reports
OFFSCREEN_ORACLE_PASS frames=16 complete_byte_comparisons=16. Per-frame byte
lengths and SHA-256 values: maps-offscreen-oracle-comparison-20261023.json.
Sources, old reference and output frames remain under .local/maps. Earlier tool
sessions expired, but result logs and complete frame outputs confirm completion;
they were inspected rather than restarting those processes.

## Remaining gate

Full nine-program host regressions and development publication need their scoped
approval. Then resolve the consumer pin/lock through the manager and use the
already-approved native COM17 build/flash path. Preserve recovery, NVS, SD/world
and C6; COM39 stays untouched. Measure the same bounded radio workload and its
memory/stack/DMA, cancellation, SD and physical behavior. The entire integration
goal stays open until the acceptance matrix's other requirements are closed.

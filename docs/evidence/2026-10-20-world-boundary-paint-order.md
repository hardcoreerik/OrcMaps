# Worldwide boundary rendering checkpoint — October 20, 2026

Engine baseline: `e50c6bd`, including the boundary hierarchy from `3d9d01c`.
This checkpoint does **not** accept worldwide quality or device appearance.

## Executed checks

From `F:/AI/OrcSDR-TEMP/orcmaps-quality-delivery`:

```sh
wsl -e bash /mnt/f/AI/OrcSDR-TEMP/orcmaps-quality-delivery/.local/boundary-preview-20261020/run-named.sh
wsl -e python3 /mnt/f/AI/OrcSDR-TEMP/orcmaps-quality-delivery/.local/boundary-preview-20261020/render-world.py
wsl -e bash /mnt/f/AI/OrcSDR-TEMP/orcmaps-quality-delivery/.local/boundary-preview-20261020/paint-order-repro.sh
```

The first script rebuilds only `orcmap_pack_inspect` and renders Eugene at
1280×720, zoom 8, using the verified global candidate and `protomaps-v4`.
The second renders London, Tokyo, Nairobi, São Paulo, Delhi, Sydney and Suva
at the same dimensions/zoom. Each viewport found all 24 requested tiles.
These host timings are not ESP32 performance measurements.

The diagnostic `KindName` change identifies country/region/local categories
instead of reporting them as unknown. Eugene pixels before/after this change
are byte-identical: SHA-256
`7b3171b762c0670af1c802ed6a7e45c9e8a88834eefe0f6705ea2d52e1986ae2`.
The change does not alter rendering.

| View | Country / region / local features | Labels drawn | Candidate overflow |
| --- | --- | --- | --- |
| Eugene | 0 / 4 / 20 | 19 | 0 |
| London | 4 / 10 / 34 | 43 | 182 |
| Tokyo | 0 / 13 / 16 | 34 | 209 |
| Nairobi | 8 / 34 / 29 | 34 | 0 |
| São Paulo | 0 / 12 / 31 | 37 | 177 |
| Delhi | 7 / 17 / 48 | 62 | 185 |
| Sydney | 0 / 5 / 12 | 39 | 0 |
| Suva | 0 / 2 / 6 | 7 | 0 |

Feature counts include complete visible tiles, duplicated boundary fragments
and offscreen geometry; they are not counts of unique visible borders.
No unsupported glyph was reported for these English-first views. That does
not establish coverage of local-script fallback. Dense-view candidate overflow
and the missing Springfield label near Eugene remain quality observations.

## Verified blocker: archive order hides boundaries

The real archive's `boundaries` layer precedes `earth` and `landuse`.
`RenderFeatureTileAt` draws in feature order; the OrcSDR streaming callback
likewise draws immediately through `RenderFeatureAt`. Later opaque polygons
therefore cover earlier boundary strokes. Classification and isolated-line
parity tests did not exercise this composition failure.

A 64×64 synthetic reproduction uses the production renderer, host framebuffer
and dark style. It draws a regional line across a full land polygon; only the
test's regional minimum zoom is lowered to permit a small zoom-0 fixture.

```text
boundary-before-land=0 land-before-boundary=128 streamed-boundary-before-land=0
REPRODUCED: later polygon hides valid boundary in batch and streaming draw paths
```

Exit 0 means the defect was reproduced, **not** that rendering is correct.
The original build command referenced nonexistent `src/render/stroke.cpp`;
the next invocation compiled but its CRLF script prevented launch. The final
LF script compiled and executed successfully. All three logs are retained.

The next engine change must define shared paint ordering for both batch and
bounded streaming callers. Merely increasing boundary contrast or fixing the
host inspector alone cannot close this blocker. Replaying decode passes and
retaining projected geometry have different CPU/memory costs; measure whichever
bounded approach is selected before accepting it on P4. Preserve clipping,
polygon holes, cancellation, allocation failure and radio servicing.

## Evidence and remaining gates

Logs in the invocation directory:

- `boundary-preview-eugene-20261020.log`
- `boundary-preview-eugene-named-20261020.log`
- `boundary-world-previews-20261020.log`
- `boundary-paint-order-repro-20261020.log` (failed compile)
- `boundary-paint-order-repro-fixed-command-20261020.log` (launch failure)
- `boundary-paint-order-repro-result-20261020.log` (defect reproduced)

Local previews, contact sheet, summary and reproduction source/scripts are in
`.local/boundary-preview-20261020/`. The candidate archive is unchanged.
No full suite, firmware build, device operation, publication or pin update was
performed for this checkpoint. COM17 is left available for the user's ADS-B
appearance assessment; COM39 is untouched. Worldwide quality, shared ordering,
physical readability and performance remain open.

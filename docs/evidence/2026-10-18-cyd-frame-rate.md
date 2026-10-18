# CYD frame rate: where a frame's time actually goes, and why 30 fps is out of reach

Date: 2026-10-18, America/Los_Angeles.
Board: CYD 3.5" (ESP32-3248S035), ESP32-D0WD-V3, no PSRAM, ST7796 480x320
landscape, display on SPI2, SD on SPI3. Serial: COM10.
Firmware: `examples/esp32-cyd-35`, ESP-IDF v5.5.4, probe build
`idf.py -DORCMAP_CYD_FPS_PROBE=1 build`.

Evidence class: **measured on the device.** Every number below came off COM10
in this session unless it is explicitly labelled a projection. The map
*content* is synthetic (see "What the workload is"), because the SD card does
not mount; the drawing, the bus traffic and the timings are real.

Goal this investigation was asked to hit: **30 fps while dragging**.

Verdict: **not reachable.** Pushing the pixels alone, before drawing anything,
costs 33.16 ms = 30.2 fps. See "The ceiling" below.

---

## 1. What the workload is

`BuildProbeTile()` in `examples/esp32-cyd-35/main/main.cpp` synthesises a
frame-shaped workload from a fixed LCG seed, so runs are comparable and no
card is needed:

| Name | Features | Polygons | Roads | Vertices | Reported span |
|---|---|---|---|---|---|
| light | 70 | 10 | 60 | 540 line segments | 23,439 px |
| busy | 202 | 30 | 172 | 1,548 line segments | 65,053 px |
| dense | 580 | 80 | 500 | 4,500 line segments | 195,885 px |

"busy" matches the 202-feature tile recorded in
`docs/evidence/CYD35_BENCHMARK.md`, so it is the row to compare against the
older device figures.

Polygons are 12-gons placed by the LCG; roads are 10-point random walks with
+-200 local-unit steps on a 4096 extent. Kinds are set and `kind_assigned` is
true, alternating across `kWater`/`kPark`/`kLand` and
`kMotorway`/`kPrimaryRoad`/`kSecondaryRoad`/`kMinorRoad`, because
`RenderOneFeature` returns immediately for an unassigned feature — an earlier
iteration of this probe measured 0.66 ms and drew nothing for exactly that
reason.

## 2. The bus ladder, and the absolute ceiling

`display.fillScreen()` is one bulk transaction of exactly the frame's bytes
(480 x 320 x 2 = 307,200 B), so it measures the wire floor with no drawing in
the way. Best of 3 at each clock:

| Requested clock | Effective | Wire floor | Throughput | Ceiling | Read-back |
|---|---|---|---|---|---|
| 20 MHz | 20 MHz | 123.18 ms | 2.38 MB/s | 8.1 fps | 0 bad |
| 26.67 MHz | 26.67 MHz | 92.40 ms | 3.17 MB/s | 10.8 fps | 0 bad |
| 40 MHz | 40 MHz | 61.78 ms | 4.74 MB/s | 16.2 fps | 0 bad |
| 60 MHz | **40 MHz** | 61.78 ms | 4.74 MB/s | 16.2 fps | 0 bad |
| 80 MHz | 80 MHz | 30.98 ms | 9.46 MB/s | **32.3 fps** | 0 bad |

`60 MHz` is not a real setting. APB runs at 80 MHz and the SPI divider is an
integer, so the reachable clocks are 80/n: **80, 40, 26.67, 20 MHz**. Asking
for 60 silently lands on 40. This is why the ladder must be quoted as those
four values and not as a smooth sweep.

Read-back is `int VerifyBands()`: it fills eight solid colour bands across the
whole panel at the clock under test, then reads one pixel back from the middle
of each band with `readPixel()`. `readback_bad = 0` at 80 MHz means the panel
received exactly the colours written, which is the strongest in-band evidence
available without a logic analyser. It is a **correctness** check, not a
longevity/soak test — see open item O3.

Immediately: **32.3 fps is the fastest this panel can be updated**, at any CPU
cost, at the highest clock the ESP32 has.

## 3. CPU was never the bottleneck

Projection + clipping only, measured against a `CountingTarget` that counts
primitives and does no I/O at all:

| Workload | CPU-only |
|---|---|
| light | 1.60 ms |
| busy | 3.14–3.20 ms |
| dense | 8.76–8.97 ms |

At 160 MHz. At 240 MHz the busy row is 2.22 ms. The integer render path
released in 0.2.3 is doing its job: a whole 202-feature frame costs ~2–3 ms of
arithmetic.

And yet the same frame took **276–323 ms** to appear on the panel. The gap is
not arithmetic.

## 4. The actual blocker: per-primitive panel traffic

Splitting the frame by primitive type, drawn straight to the panel at 80 MHz:

| Drawn | Time |
|---|---|
| 30 polygons only (`areas_only`) | 47.62–48.56 ms |
| 172 roads / 1,548 segments only (`lines_only`) | 263.5–388.3 ms |
| both | 278.2–402.3 ms |

**Roads were 94% of the frame.** 263.5 ms / 1,548 segments = **170 us per
segment**. A segment here is about 12 screen pixels, i.e. 24 bytes of payload.
170 us of bus is roughly 1,700 bytes. The cost is not the pixels; it is
LovyanGFX `DisplayTarget::DrawLine` calling `display_.drawLine()`, which walks
Bresenham and issues a windowed SPI write **per pixel**.

This also retroactively explains a note that had been sitting in
`examples/esp32-cyd-35/main/main.cpp` since the first CYD report: holding one
`startWrite()`/`endWrite()` across a whole frame measured *slower*
(616.8 -> 735.8 ms). Batching commands cannot help when the cost is per-pixel
writes inside the batch. That comment is still correct; this is why.

Note the run-to-run spread on `lines_only` (263 -> 388 ms across flashes). It
scales with the same workload, so it is flash/clock-state variance on this
board, not a different workload. Treat any single number as +-20%.

## 5. The fix: rasterize in RAM, push in bands

There is no framebuffer on this board (307,200 B for one frame against roughly
129 KiB of usable internal RAM), so the frame cannot be drawn to RAM at full
size. It *can* be drawn to RAM one horizontal band at a time, and then handed
to the panel as one `pushImage` per band. That removes every per-pixel SPI
transaction, leaving one transaction per band.

Cost of the push alone, with a pre-filled buffer, at 80 MHz:

| Bands | Buffer | Time | Rate |
|---|---|---|---|
| 8 x 40 rows | 38,400 B | 33.85 ms | 29.5 fps |
| 4 x 80 rows | 76,800 B | 33.16 ms | 30.2 fps |

Each push call costs ~0.36 ms of turnaround, which is why 4 bands beats 8.
4 x 80 rows is what ships in the probe; 76,800 B does allocate successfully
alongside the streaming scratch and the feature buffer.

Banded full frame, measured after each optimisation step (all at 80 MHz):

| Step | Frame time | fps |
|---|---|---|
| Project + per-band rasterize + push, naive | 97.39 ms | 10.3 |
| + direct pixel Bresenham for 1 px, insertion sort, no resize-down | 91.83 ms | 10.9 |
| + interior fast path skips the exact-rational clipper; polygon DDA | 86.67 ms | 11.5 |
| + stamp a w x w brush for every width (no per-run `FillRect`) | 66.60 ms | 15.0 |
| + 4 x 80 bands, 32-bit band clear | 60.20 ms | 16.6 |
| + CPU raised 160 -> 240 MHz | **50.88 ms** | **19.7** |

Against the direct-to-panel baseline of 285.65 ms, that is **5.6x, 3.5 -> 19.7
fps**.

Final phase split at 240 MHz with 4 x 80 bands:

| Phase | Time |
|---|---|
| Project once into the screen-space cache | 1.52 ms |
| Rasterize all four bands into RAM | ~16.0 ms |
| Push four bands (bus-limited) | 33.34 ms |
| **Total** | **50.88 ms** |

The three optimisations that mattered, in order of payoff:

1. **Stamp strokes instead of `FillRect` per run.** Three quarters of the
   segments in a street-level view are wider than one pixel, and they were
   going through `RasterizeCenteredStroke`, which emits one `FillRect` per
   coalesced run; on a diagonal line a run is one pixel, so it was a full clip
   and a bounds-checked loop per pixel. `BandTarget::Stamp()` writes the w x w
   brush inline. This is pixel-equivalent, because the shared stroke contract
   states the union of brushes over a run equals the coalesced rectangle.
2. **One division per polygon edge, not per (edge, row).** The crossing x is
   linear in y, so `BandTarget::FillPolygonRings` builds an edge list with a
   16.16 fixed-point step and advances it. The per-row `int64_t` division for
   every crossing edge was the single largest cost in the first banded
   version.
3. **CPU frequency.** The board had been running at the IDF default 160 MHz.
   It is a 240 MHz part.

## 6. The ceiling, stated plainly

- Absolute: **32.3 fps** (one `fillScreen` at 80 MHz).
- Bulk-push: **30.2 fps** (4 bands, empty screen, zero drawing).
- 30.0 fps needs a whole frame in 33.33 ms. The push alone is 33.16–33.34 ms.
  The remaining budget is 0.0–0.2 ms, against a 16 ms rasterizer.

So 30 fps is not a matter of more optimisation. Even an infinitely fast
rasterizer lands on 30.2 fps, and it is not infinitely fast.

**Realistic target: ~25 fps.** Halving the 16 ms rasterizer gets to ~42 ms
(23.8 fps); getting to ~37 ms (27 fps) would need the rasterizer at ~4 ms.
That is worth attempting, and it is the difference between "usable drag" and
"this feels like a map in your hand".

---

## 7. What is in the tree, and where

Committed with this document:

- `include/orcmap/renderer.hpp`, `src/render/renderer.cpp` — new public API:
  `ScreenFeature`, `ProjectFeatureToScreen()`, `DrawScreenFeature()`, plus a
  shared `ResolveDrawablePaint()` so the two draw paths can never disagree
  about what is skipped. Additive; `RenderFeatureAt` and its `RenderScratch`
  overload are unchanged.
- `tests/host/test_render.cpp` — `TestScreenFeatureMatchesDirectRender()`
  pins the new path to be **pixel-identical** to `RenderFeatureAt` across a
  multipolygon with a hole, a stroked (width > 1) road, a 1 px road, a point
  feature and a style-hidden feature. If the two paths ever drift, this fails.
- `examples/esp32-cyd-35/main/main.cpp` — the probe (`ORCMAP_CYD_FPS_PROBE`),
  `BandTarget`, `RenderFrameBanded()`, `CountingTarget`, `VerifyBands()`, the
  `SetWriteClock()`/`WriteClock()` hooks on `Cyd35Display`, and the touch/drag
  work from the previous session.
- `examples/esp32-cyd-35/main/CMakeLists.txt` — `ORCMAP_CYD_FPS_PROBE`,
  `ORCMAP_CYD_INTERACTIVE`, `ORCMAP_CYD_TOUCH_DEBUG` gates.
- `examples/esp32-cyd-35/sdkconfig.defaults` — `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`.

Gates at this commit: 180 host test functions pass, documentation-truth gate 0
errors, data-provenance gate clean, ESP32 cross-build clean.

## 8. Open items

Ordered by what unblocks the most. O1 and O2 are the blockers.

### O1. The banded renderer is not wired into the drag loop
`RenderFrameBanded()` is only called by `RunFpsProbe()`. The interactive path
still goes through `RenderFrameWith()` -> `RenderMeasuredFrame()`, i.e. the
285 ms direct-to-panel route, so a real drag is still ~3.5 fps.

What it needs: `RunInteractive()` in `examples/esp32-cyd-35/main/main.cpp`
renders through the shared harness, which streams decoded features into a
`feature_sink`. To band it, the frame's features have to be captured into the
screen-space cache first (`g_screen_frame` / `g_screen_count` already exist for
this) instead of being drawn as they stream. Two ways:

- simplest: have the sink `push_back` each `Feature` into a `FeatureTile` for
  the frame, then call `RenderFrameBanded()` on it. Costs RAM: a 202-feature
  tile is roughly 30–60 KB of owned geometry, which is affordable but tight
  next to the 76,800 B band buffer and the 56 KiB streaming scratch.
- better: project straight into the screen-space cache from the sink, skipping
  the intermediate `FeatureTile`. `ProjectFeatureToScreen()` is exactly the
  per-feature step, so the sink can call it and never retain MVT geometry.

The second is the right one and avoids the RAM question entirely.

Then the drag loop becomes: pan -> project each streamed feature into
`g_screen_frame` -> `RenderFrameBanded()` -> repeat. The "drag style" reduced
budget already exists (`DragStyle()`), and dropping `kBuilding`/`kMinorRoad`
will directly reduce the 16 ms rasterizer.

### O2. The SD card does not mount, so nothing real can be measured
Reproducible on every boot of every firmware built here:

```
E (1118) vfs_fat_sdmmc: sdmmc_card_init failed (0x107).
E (1119) orcmap_cyd35: SD SPI mount failed: ESP_ERR_TIMEOUT
```

`0x107` is `ESP_ERR_TIMEOUT`. The same code mounted in September, and the pins
have not changed, so the card itself is the first suspect (absent, not seated,
or a different card). Until it mounts there is no way to draw a real map, and
every number in this document stays synthetic-workload.

To unblock: a card with `/orcmaps` containing a pack triplet (manifest +
archive + SHA-256 sidecar). `examples/esp32-cyd-35/main/main.cpp` `MountSd()`
is where it fails. The touch/drag build falls back to `RunTouchTest()` when
there is no card, and the probe does not need one at all.

### O3. 80 MHz has not been soak-tested
`readback_bad = 0` at 80 MHz is one fill-and-verify pass. Most CYD code runs
24 or 40 MHz, and signal integrity on this board's ribbon at 80 MHz is not
proven over time or temperature. Before shipping a drag build at 80 MHz:
run it for tens of minutes, drag continuously, and watch for banding,
channel-swapped pixels or dropped frames. 40 MHz is the safe fallback and
still gives 61.78 ms wire / 16.2 fps ceiling.

### O4. Rasterization is still ~16 ms and needs to halve
Ideas, roughly in payoff order:

- **Bucket features into bands once per frame.** Today every band re-walks all
  `g_screen_count` features and bbox-tests each. Building a per-band index once
  removes ~4 x 202 tests and, more importantly, lets the inner loop be tight.
- **32-bit span fills** in `BandTarget::FillPolygonRings`. The span loop
  writes one `uint16_t` at a time; the band clear already went to 32-bit and
  that alone was worth several ms.
- **Reuse the `BandTarget`.** It is constructed inside the band loop, so its
  `crossings_` and `edges_` vectors allocate every band. Give it a `Reset()`.
- Consider whether the polygon scanline needs to visit every row of every
  band it touches, or can be clipped to the band's rows only (it already is;
  check the edge-list build, which currently walks all rings regardless).

### O5. Touch mapping is confirmed in one direction only
Established this session with `ORCMAP_CYD_TOUCH_DEBUG=1`: CS 33 answers,
`kTouchSwapAxes = true` is right (screen Y follows raw X), and screen X was
mirrored until `kTouchInvertX` was set to `true`. A corner-to-corner drag
reported raw x 790..3578 and raw y 502..3673, which is narrower than the
`kTouchRawMin`/`kTouchRawMax` of 300/3900, so the outer few percent of the
panel is a dead margin and the drag gain runs a few percent low. Re-derive
those constants if a replacement board feels offset; the debug build logs
`raw x,y -> screen x,y` per sample.

### O6. On-device output differs slightly from the panel adapter
`BandTarget::DrawLine` uses its own Bresenham with a w x w stamp, so a
diagonal stroke can differ by a pixel at ties from
`DisplayTarget`'s run-coalesced `RasterizeCenteredStroke`. The two are
pixel-equivalent by contract for axis-aligned runs, not necessarily at
diagonal tie-breaks. This is a target-level difference of the same kind that
already exists between the host and M5GFX targets; it is not a library
regression, but do not expect byte-identical panel output from the banded path.

### O7. Housekeeping
- `sdkconfig` is generated and untracked; the tracked change is
  `sdkconfig.defaults`. If `sdkconfig` is deleted it regenerates at 240 MHz.
  Editing `sdkconfig.defaults` alone does **not** change an existing build —
  that mistake cost one build cycle here.
- `OrcMesh.code-workspace` is untracked and pre-dates this work.
- The local `v0.2.2` tag was deleted without ever being published: it pointed
  at a commit that lacked the polygon-ring and allocation-free-cleanup fixes.
  Its content lives in v0.2.3. Do not resurrect it.
- `STATUS.md` still says "Not integrated with OrcSDR", which is stale —
  OrcSDR pins this engine on `codex/maps-integration-completion`. Left alone
  because released-state wording is a separate decision.

## 9. How to reproduce any number here

```
# probe: clock ladder, CPU-only cost, banding, banded frame, phase split
cd examples/esp32-cyd-35
idf.py -B build-fps -DORCMAP_CYD_FPS_PROBE=1 build
idf.py -B build-fps -p COM10 flash
# then read COM10 at 115200; the probe halts with
#   "FPS PROBE COMPLETE: see serial log for the clock table"

# touch mapping
idf.py -B build-live -DORCMAP_CYD_INTERACTIVE=1 -DORCMAP_CYD_TOUCH_DEBUG=1 build

# the shipped scenario benchmark
idf.py -B build-rel023 -DORCMAP_CYD_SCENARIOS=1 build
```

The probe needs no SD card. It leaves the display clock at 24 MHz and stops in
`Fail()`, so a board left running the probe shows the failure screen until
reflashed.

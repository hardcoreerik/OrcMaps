# Map styling

## Why styles are separate from map data

A map pack (`.pmtiles`, see [`PACK_FORMAT.md`](PACK_FORMAT.md)) describes
*geography*: where roads, water, and boundaries are, and what category each
feature belongs to. It does not describe *appearance*. ORCMAP1 already
proved this separation informally — `offline_map::draw_base()` takes colors
as call-site parameters, never bakes them into the pack (see
[`ORCMAP1_AUDIT.md`](ORCMAP1_AUDIT.md) §4) — OrcMaps formalizes it into a
real API so an application can switch appearance without touching map data,
and so the same installed pack serves every consumer regardless of which
style each one wants.

## API

`include/orcmap/style.hpp` / `src/render/style.cpp`.
`FeatureKind` lives in `include/orcmap/feature_kind.hpp` so Feature and
Style share it without Feature depending on the style subsystem.

- `FeatureKind` — the base-map categories a style can paint: background,
  land, water, motorway/primary/secondary/minor road, rail, boundary,
  building, park, airport, and three label tiers (primary/secondary/muted).
- `FeatureRule` — one category's color, line width, and zoom-visibility
  window (`min_zoom`/`max_zoom`, `visible`). Zoom gating lives *inside* the
  rule, not a separate parallel structure — a style file is the single
  source of truth for both appearance and visibility of a category.
- `MapStyle` — a named, versioned (`version`), array of 15 `FeatureRule`s
  (one per `FeatureKind`), plus `label_scale` and
  `label_priority_threshold`. `id` is a short stable string (e.g.
  `"orcsdr-dark"`) — see "External style files" below for why that matters.
- `ResolveFeatureStyle(kind, zoom, style) -> MapPaint` — the one function
  renderer code calls. Never read `style.rules[...]` directly from draw
  code (avoid `drawLine(..., 0x7BEF)`-style scattered literals); always go
  through this resolver, so a later per-feature-attribute resolver (e.g.
  varying road color by an OSM `class` attribute, not just `FeatureKind`)
  can extend behavior at one call site instead of every draw call.
- `StyleManager` — holds the active style for one map view. `SetStyle()`
  and `SetStyleById()` switch it at runtime; switching never touches map-
  pack data (no reload of geography), it only changes which `MapStyle`
  `ResolveFeatureStyle()` uses on the next render. `SetStyleById()` returns
  `false` and leaves the previous style active if the id is unknown —
  invalid input degrades gracefully, it never crashes or silently falls
  back to a different style than the caller expects.

## Built-in styles

Four, per the initial design brief, all in `src/render/style.cpp`:

1. **`orcsdr-dark`** ("OrcSDR Dark") — very dark graphite background, muted
   charcoal roads, deep desaturated blue water, restrained boundaries,
   off-white primary labels. Deliberately visually quiet: OrcSDR's RF
   overlays (aircraft, LoRa nodes, transmitters, selected objects, bearings,
   heatmaps) are the subject; the base map is context. Semantic overlay
   colors (cyan=selection, green=active, amber=warning, red=alarm) belong
   to the *overlay* layer, never to this style — see "Overlays are NOT
   base-map styles" below.
2. **`standard-light`** ("Standard Light") — conventional daytime
   cartography. `orcmap::DefaultStyle()` returns this — it is OrcMaps'
   generic, application-neutral default, **not** the OrcSDR default (see
   "OrcSDR-specific styling guidance" below).
3. **`high-contrast-field`** ("High Contrast / Field") — strong road
   hierarchy (black motorways, thick lines), larger labels
   (`label_scale = 1.3`), rail hidden entirely, minor roads/buildings
   pushed to higher zoom than the other styles. For outdoor/field radio/
   emergency-response use, not OrcSDR-specific.
4. **`night-red-safe`** ("Night / Red-Safe") — black background, every
   color red-dominant with low-or-zero blue (enforced by a host test —
   `tests/host/test_style.cpp`'s `TestNightStyleHasNoBlueLight`), no
   bright-white anywhere, park deliberately rendered in dark red rather
   than green. For astronomy/night field work/night vehicle use.

## Zoom behavior

Every `FeatureRule` carries `min_zoom`/`max_zoom`. The shared policy
(`ApplyStandardZoomGating()` in `style.cpp`) used by Dark/Light/Night:
major roads visible from zoom 0, secondary roads from zoom 8, minor roads
from zoom 12, buildings from zoom 15, labels split into an always-visible
primary tier and an zoom-8+ secondary tier. High-Contrast/Field overrides
several of these (minor roads at zoom 14+, buildings effectively disabled
at zoom 17+, rail off) to actively show *less* at once, per its "reduced
clutter" design goal — it is not the same policy with different colors.

This is deliberately simple today. The `FeatureRule`/`MapStyle` shape does
not prevent a later per-feature-attribute or interpolated (rather than
step-function) zoom resolver — see `ResolveFeatureStyle()`'s doc comment.

## Renderer relationship

`orcmap::Color` (`include/orcmap/color.hpp`) is a plain RGBA8 struct with
no dependency on M5GFX, LovyanGFX, LVGL, or any display API. Converting to
a display's native pixel format (e.g. RGB565 for M5GFX/LovyanGFX) is an
**target's** job, not the style system's. The host framebuffer target
proves this; `adapters/m5gfx` converts Color to RGB565 at draw time.
`Color.a` is stored on the generic type; the M5GFX adapter currently
renders opaque RGB565 and does not blend. Overlay compositing is future
engine work.
`MapPaint::width_px` is now rasterized by every `RenderTarget` via
`include/orcmap/stroke.hpp` (≤1 px is one pixel; >1 is a centered
square brush). Label FeatureKinds are skipped until text rendering
exists; they are not drawn as placeholder dots.
This keeps the same
`MapStyle`/`ResolveFeatureStyle()` API usable by a future LVGL or host-side
test renderer without change.

## Overlays are NOT base-map styles

This separation is load-bearing, not incidental:

```
Geographic map data
        |
Base map style   (this document)
        |
Rendered map
        |
Generic OrcMaps overlays   (markers, lines, polygons, circles, sectors,
        |                    vectors -- include/orcmap/overlay.hpp)
Application-specific overlays   (aircraft, LoRa nodes, RF bearings, P25
                                  sites, transmitters -- OrcSDR's own code,
                                  never OrcMaps')
```

`MapStyle` has no fields for aircraft color, node color, RF alarm color, or
any other application concept. OrcSDR's semantic overlay colors (cyan/
green/amber/red — see the OrcSDR Dark description above) are drawn by
OrcSDR's own overlay code using OrcSDR's own color choices, entirely
independent of which `MapStyle` is active. Adding an OrcSDR-specific field
to `MapStyle` would be a violation of this project's core separation
(`docs/ORCMAP1_AUDIT.md`'s "what OrcMaps inherits vs. replaces": style
lives in the caller, not the engine) and should be treated as a bug if it
ever happens.

## Rendered-tile cache interaction

If/when OrcMaps caches pre-rendered tiles (e.g. as RGB565 on SD — see
`docs/FORMAT_DECISION.md`'s hybrid-rendering decision), the cache key
**must** include style identity, or a style switch would incorrectly
redisplay a tile rendered under the previous style.
`include/orcmap/cache_key.hpp` defines `RenderedTileCacheKey`:

```
pack_id_hash, z, x, y, style_id_hash, style_version, renderer_version
```

`style_version` exists specifically so that *editing* a built-in style
(e.g. retuning OrcSDR Dark's road color) invalidates old cached renders
even though the style's `id` didn't change — bump `MapStyle::version`
whenever a style's visuals change. `renderer_version`
(`orcmap::kRendererVersion`) exists for the same reason on the decode/draw
side: bump it whenever tile decode or draw logic changes in a way that
changes rendered pixel output. `tests/host/test_style.cpp`'s
`TestCacheKeyChangesWithStyle` verifies all of this: same style -> same
key (cache hit), different style/version/pack/tile -> different key.

## External style files — not yet implemented, but not blocked

Today's four styles are compiled C++ (`BuildOrcSdrDark()` etc. in
`style.cpp`), which is acceptable for the first milestone. Nothing in the
`MapStyle`/`FeatureRule` shape prevents loading an equivalent structure
from an external file later (e.g. `/orcsdr/maps/styles/orcsdr-dark.orcstyle`
holding style id, display name, colors, visibility, widths, zoom
thresholds, and a version/metadata block) — `MapStyle` is already a plain,
serializable data structure with no behavior baked into its fields, and
`id` is already the stable string an external file would use to identify
itself. This is deliberately deferred, not designed away.

## OrcSDR-specific styling guidance

OrcSDR ships with `BuiltinStyle::kOrcSdrDark` as its default — that
selection happens in OrcSDR's own integration code (`map.SetStyle(orcmap::
BuiltinStyle::kOrcSdrDark)` at startup), not inside OrcMaps. OrcMaps'
own engine-level default (`orcmap::DefaultStyle()`) is `standard-light`,
since OrcMaps must remain useful to consumers with no reason to want a
tactical/RF aesthetic. A future OrcSDR Settings → Maps → Map Style screen
would just call `StyleManager::SetStyle()`/`SetStyleById()` — not yet built
(tracked in `ROADMAP.md`), since the ask was to get the underlying API
right first.

## Testing

`tests/host/test_style.cpp`, all passing against the host build
(`cmake -S tests/host -B build && cmake --build build && ctest --test-dir
build`):

- all four built-in style ids resolve, including via `FindBuiltinStyleById`
- `DefaultStyle()` is `standard-light`
- `StyleManager` switches styles at runtime; an unknown id leaves the
  previous style active rather than crashing or defaulting silently
- zoom-visibility logic: minor roads hidden at low zoom / visible once
  zoomed in, major roads visible at low zoom, buildings only at high zoom,
  High-Contrast/Field's stricter thresholds and disabled rail
- Night/Red-Safe's every color is red-dominant with no bright-white
- the four styles' background colors are pairwise distinct (catches a
  copy-paste that never changed a color)
- cache keys differ across style, style version, tile coordinate, and pack
  id, and are identical for the same (pack, tile, style) triple

Not yet testable: actual rendered output (no renderer exists yet — see
`STATUS.md`). Once a renderer exists, the natural next test is
rendering the same viewport under multiple styles and asserting the outputs
differ (a hash-of-pixels comparison), per the original design brief.

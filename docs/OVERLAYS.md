# Generic geographic overlays

OrcMaps supplies generic map graphics. Your application owns the meaning of each
object: moving device, navigation point, sensor, boundary, route, or anything else.
There are no application protocol types in the library. The overlay subsystem
requires neither M5GFX nor networking, tile decoding, or storage access.

## API

Public header: `#include "orcmap/overlay.hpp"`.

```cpp
#include "orcmap/overlay.hpp"

orcmap::OverlayScene scene({4, 100, 1024});
scene.AddLayer(100, 0);  // Opaque application layer ID, bottom draw order.
scene.AddLayer(200, 20); // Later layer draws on top.

orcmap::OverlayObject marker;
marker.id = 101;                         // Opaque ID within the layer.
marker.kind = orcmap::OverlayKind::kMarker;
marker.position = {44.0521, -123.0868};
marker.style.marker_shape = orcmap::MarkerShape::kArrow;
marker.style.marker_size_px = 14.0f;    // Pixel diameter, independent of zoom.
marker.bearing_deg = 45.0;              // Degrees clockwise from geographic north.
scene.Upsert(100, marker);

orcmap::OverlayObject trail;
trail.id = 102;
trail.kind = orcmap::OverlayKind::kPolyline;
trail.vertices = {{44.0521, -123.0868}, {44.0540, -123.0800}};
trail.style.stroke_width_px = 2.0f;
scene.Upsert(100, trail);

// Paint map content with existing OrcMaps APIs before this pass.
orcmap::OverlayScratch scratch;
orcmap::RenderOverlays(scene, viewport, &target, &scratch);

orcmap::OverlayHit hit;
if (orcmap::HitTestOverlays(scene, viewport, touch_x, touch_y, &hit, &scratch)) {
  // Your application resolves (hit.layer_id, hit.object_id) into its own data.
}
```

For ESP-IDF consumption, pin a tested OrcMaps **commit** as an external managed
component in the application `idf_component.yml`, as already described in
`docs/ORCSDR_INTEGRATION.md`. Do not vendor or copy `src/overlays/` into the
consumer project. The CMake registration in OrcMaps includes the overlay source.
The interface does not require an OrcSDR header, controller, or global state.

## Shapes and coordinates

- `kMarker`: circle, square, diamond, triangle or arrow; heading rotates around
  screen center; selection visibly enlarges and highlights it. `position` is
  geographic, but size is pixels.
- `kPolyline`: ordered geographic vertices, optionally closed, screen-stroked.
- `kPolygon`: flattened vertex array with `ring_sizes`. Exterior is first;
  subsequent rings are holes (even-odd fill). Complex multipolygons are
  represented with separate polygon objects. A target implementing
  `FillPolygonRings` correctly renders holes; targets with only the legacy
  default implementation can only paint the first ring. Host/M5GFX targets
  implement ring support.
- `kCircle`: geographic radius (`radius_m`), spherical-earth approximation
  with 48 segments (not a fixed pixel radius).
- `kSector`: geographic radius and clockwise `start_bearing_deg` /
  `sweep_deg` in `(0, 360]`; 48 arc segments plus center.
- `kVector`: bearing and length in meters from `position`.

Every input latitude must fall within the supported Web Mercator range
`[-85.05112878, +85.05112878]` and longitude within `[-180, +180]`.
Invalid or NaN/Inf coordinates are **rejected**, not silently clamped.
Paths crossing the antimeridian use the short wrapped segment. Very large
shapes spanning more than half the world are not guaranteed to choose the
intended edges. Circles and sectors use a spherical Earth radius of
6,371,008.8 m; they are approximations, especially over long distances or
near the Mercator latitude limit. Bearings are true-north-based.

## Layers, IDs and hit testing

`AddLayer`, `SetLayerVisible`, `SetLayerOrder`, `Upsert`, `Remove`,
`ClearLayer`, and `RemoveLayer` are synchronous. Layers are ordered by
`draw_order`, then stable insertion order for equal priorities; later
objects in a layer paint on top. `HitTestOverlays` traverses the same
order backwards and returns the topmost `layer_id` and `object_id`.
It ignores hidden or unselectable objects. A caller can set an additional
0–32-pixel touch tolerance; the default is 6. Markers use an enclosing
circular touch area, while paths use distance-to-segment and filled polygons
use even-odd containment (holes excluded). Selection is a property of a
marker; OrcMaps does not interpret the ID or show an application popup.

## Ownership, performance and drawing lifecycle

`OverlayScene` **owns copies** of all object coordinates. Input vectors may
be temporary. All access is synchronous; consumers serialize calls to
`Upsert`, `RenderOverlays`, and `HitTestOverlays` themselves. No background
threads or network operations are created. No per-frame allocation is
expected after scratch warm-up; provide one `OverlayScratch` instance per
simultaneous renderer. The implementation uses existing viewport math,
`RenderTarget`, and line clipping, and has no new renderer dependency.

The scene sets `Dirty()` after an update and increments `Revision()`.
`RenderOverlays` is an **overlay-only** pass and never clears or repaints the
basemap. When an object moves/disappears, its old pixels are not erased by
repainting only the new object. The application must restore the affected
basemap area or recompose the entire map before rendering the overlay pass.
`AcknowledgeRedraw()` clears the dirty flag only after the caller performs
that work. Region-based invalidation and automatic base restoration are
**not implemented**. Existing attribution and control drawing remain the
application's responsibility and must be composed above/beside overlays.
Missing tiles do not prevent overlay rendering.

Hard absolute limits per scene: 16 layers, 512 objects, 4,096 caller-owned
vertices, 256 vertices per individual object, 32 rings per polygon, 48
segments per geodesic arc. Each scene can request **lower** limits through
`OverlayLimits`; defaults are 8 layers, 128 objects and 2,048 vertices.
Overflow and invalid updates return false without changing the scene.
Each object carries dynamic vectors (owned; allocations occur during updates),
so memory is *bounded by these limits*, but exact RAM usage also depends on
the C++ allocator and vector capacity. `OverlayScratch` reserves bounded
geometry buffers at creation. Up to 32 neighboring world copies on either side may be
rendered on exceptionally wide low-zoom viewports; repeated copies are not
unbounded. `Color.a` is stored, but the current M5GFX RGB565 adapter ignores
alpha; translucent overlay blending remains a later integration task.

## Verification and limitations

`tests/host/test_overlay.cpp` exercises validation, updates, bounds,
marker shapes, layer ordering, picking, pan/zoom projection, antimeridian,
polygons with holes, circles, sectors, vectors, and repeated rendering.
`tests/consumer/overlay_consumer_test.cpp` checks that the public API can be
compiled and linked from a project outside the OrcMaps internals.

Initial sandbox compilation and assertions were run against a minimal
public-API-compatible host harness with GCC 14, C++17, `-Wall -Wextra
-Wpedantic -Werror`, AddressSanitizer and UndefinedBehaviorSanitizer.
The full host (7 CTest targets), external-consumer (2 CTest targets),
ESP32-P4 and ESP32-S3 ESP-IDF 5.5.4 compile/link gates passed in GitHub
Actions. Physical hardware render/touch, CPU and peak RAM remain unverified.

## Review hardening (2026-11-07)

Repeated-world polyline and polygon projection now unwraps each vertex relative
to the previous vertex *in the same copy*. Previously, applying a world-copy
offset on every vertex could stretch tracks and create false segments on
low-zoom repeated maps. The host regression test covers a three-point track
with a map wider than the world.

An overlay pass requiring a narrower clip rectangle now returns `false` without
drawing if the target declines `SetClipRect`. This avoids painting into an
unclipped neighboring UI region, particularly for thick strokes. Targets that
render into a full-size viewport need no additional scissor. Marker fills are
polygon-clipped to the visible region and their outlines are line-clipped.

Markers, circles, sectors and vectors reject stray polygon ring metadata.
Marker rotation computes sine/cosine once per marker draw rather than for
every vertex.

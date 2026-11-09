#include "orcmap/overlay.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

#include "orcmap/clip.hpp"

namespace orcmap {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kEarthRadiusM = 6371008.8;
constexpr double kMaxDistanceM = 20000000.0;
constexpr size_t kMaxWorldCopies = 32;
using SP = OverlayScratch::ScreenPoint;

/** Check numeric values before projection and geometry arithmetic. */
bool Finite(double x) { return std::isfinite(x); }
/** Validate pixel dimensions and marker shape before storing an object. */
bool ValidStyle(const OverlayStyle& s) {
  return Finite(s.marker_size_px) && s.marker_size_px > 0 &&
         s.marker_size_px <= 256 && Finite(s.stroke_width_px) &&
         s.stroke_width_px >= 0 && s.stroke_width_px <= 64 &&
         static_cast<unsigned>(s.marker_shape) <= static_cast<unsigned>(MarkerShape::kArrow);
}
/** Count caller-owned points; generated circle/sector vertices use scratch. */
size_t VertexUse(const OverlayObject& o) { return o.vertices.size(); }
/** Enforce kind-specific geometry contracts and per-object limits. */
bool ValidObject(const OverlayObject& o) {
  if (!ValidStyle(o.style)) return false;
  if (o.vertices.size() > kOverlayMaxVerticesPerObject ||
      o.ring_sizes.size() > kOverlayMaxRings) return false;
  for (const auto& p : o.vertices) if (!OverlayCoordinateValid(p)) return false;
  switch (o.kind) {
    case OverlayKind::kMarker: return OverlayCoordinateValid(o.position) && o.vertices.empty() && o.ring_sizes.empty() && Finite(o.bearing_deg);
    case OverlayKind::kPolyline: return o.vertices.size() >= 2 && o.ring_sizes.empty();
    case OverlayKind::kPolygon: {
      if (o.ring_sizes.empty()) return false;
      size_t count = 0;
      for (size_t n : o.ring_sizes) {
        if (n < 3 || n > o.vertices.size() || count > o.vertices.size() - n) return false;
        count += n;
      }
      return count == o.vertices.size();
    }
    case OverlayKind::kCircle:
    case OverlayKind::kVector:
    case OverlayKind::kSector:
      if (!OverlayCoordinateValid(o.position) || !o.vertices.empty() ||
          !o.ring_sizes.empty() || !Finite(o.radius_m) || o.radius_m <= 0 ||
          o.radius_m > kMaxDistanceM || !Finite(o.bearing_deg)) return false;
      if (o.kind != OverlayKind::kSector) return true;
      return Finite(o.start_bearing_deg) && Finite(o.sweep_deg) &&
             o.sweep_deg > 0 && o.sweep_deg <= 360;
  }
  return false;
}

/** Reject unusable map viewports before drawing or hit testing. */
bool ValidViewport(const Viewport& v) {
  return ZoomIsValid(v.zoom) && v.width_px > 0 && v.height_px > 0 &&
         v.tile_size_px > 0 && Finite(v.center_lat_deg) &&
         Finite(v.center_lon_deg) && OverlayCoordinateValid({v.center_lat_deg, v.center_lon_deg});
}

// Great-circle destination from initial bearing. This is a spherical-earth
// approximation, NOT a geodesic ellipsoid calculation.
/** Approximate a spherical-Earth destination and wrap its longitude. */
LatLon Destination(LatLon p, double distance_m, double bearing_deg) {
  const double lat = p.lat_deg * kPi / 180.0;
  const double lon = p.lon_deg * kPi / 180.0;
  const double bearing = std::remainder(bearing_deg, 360.0) * kPi / 180.0;
  const double arc = distance_m / kEarthRadiusM;
  const double lat2 = std::asin(std::clamp(
      std::sin(lat) * std::cos(arc) + std::cos(lat) * std::sin(arc) * std::cos(bearing),
      -1.0, 1.0));
  const double lon2 = lon + std::atan2(std::sin(bearing) * std::sin(arc) * std::cos(lat),
                                     std::cos(arc) - std::sin(lat) * std::sin(lat2));
  return {std::clamp(lat2 * 180.0 / kPi, -kMercatorMaxLatDeg, kMercatorMaxLatDeg),
          WrapLongitudeDeg(lon2 * 180.0 / kPi)};
}

// Generate dynamic geographic shapes in provisioned scratch storage.
/** Get explicit path vertices or generate geodesic shapes in reusable scratch. */
void ShapeVertices(const OverlayObject& o, OverlayScratch& scratch,
                   const std::vector<LatLon>** points, size_t* count) {
  scratch.generated.clear();
  if (o.kind == OverlayKind::kPolyline || o.kind == OverlayKind::kPolygon) {
    *points = &o.vertices;
    *count = o.vertices.size();
    return;
  }
  if (o.kind == OverlayKind::kVector) {
    scratch.generated.push_back(o.position);
    scratch.generated.push_back(Destination(o.position, o.radius_m, o.bearing_deg));
  } else if (o.kind == OverlayKind::kCircle) {
    for (size_t i = 0; i < kOverlayArcSegments; ++i)
      scratch.generated.push_back(Destination(o.position, o.radius_m,
          (360.0 * i) / kOverlayArcSegments));
  } else if (o.kind == OverlayKind::kSector) {
    // A full revolution is a circle: adding the center would create a
    // spurious radial outline and touch target along the closing spoke.
    const bool full_circle = o.sweep_deg == 360.0;
    if (!full_circle) scratch.generated.push_back(o.position);
    // Reduce the starting angle before interpolation: at a huge (but finite)
    // bearing, adding one small sweep step can lose all floating-point bits.
    const double start = std::remainder(o.start_bearing_deg, 360.0);
    const size_t steps = full_circle ? kOverlayArcSegments : kOverlayArcSegments + 1;
    for (size_t i = 0; i < steps; ++i)
      scratch.generated.push_back(Destination(o.position, o.radius_m,
          start + o.sweep_deg * i / kOverlayArcSegments));
  }
  *points = &scratch.generated;
  *count = scratch.generated.size();
}

// The viewport's projection picks the closest world copy. Unwrap successive
// vertices against their predecessor so antimeridian crossings are short.
/** Project a path while unwrapping the antimeridian in one world copy. */
bool ProjectPath(const Viewport& v, const LatLon* points, size_t n,
                 double shift, std::vector<SP>* out, double anchor = NAN) {
  out->clear();
  const double world_px = static_cast<double>(TilesPerAxis(v.zoom)) * v.tile_size_px;
  for (size_t i = 0; i < n; ++i) {
    double x, y;
    if (!ProjectLatLon(v, points[i], &x, &y) || !Finite(x) || !Finite(y)) return false;
    if (i == 0 && Finite(anchor)) x += world_px * std::round((anchor - x) / world_px);
    if (i > 0) x += world_px * std::round((out->back().x - shift - x) / world_px);
    out->push_back({x + shift, y});
  }
  return true;
}

/** Round and bound projected pixel coordinates after geometric clipping. */
int SafeInt(double x) {
  return static_cast<int>(std::clamp(std::round(x), -1000000.0, 1000000.0));
}

// Clip in projected double precision. Integer saturation before clipping can
// change a segment's slope at high zoom and move a visible crossing.
/** Clip a projected line to a stroke-expanded pixel rectangle in double precision. */
bool ClipProjectedLine(SP* a, SP* b, PixelRect clip, float stroke_width_px) {
  const double pad = std::ceil(stroke_width_px * 0.5);
  const double left = static_cast<double>(clip.x) - pad;
  const double top = static_cast<double>(clip.y) - pad;
  const double right = static_cast<double>(clip.x) + clip.width - 1 + pad;
  const double bottom = static_cast<double>(clip.y) + clip.height - 1 + pad;
  const double dx = b->x - a->x;
  const double dy = b->y - a->y;
  double enter = 0.0, leave = 1.0;
  const auto bound = [&](double p, double q) {
    if (p == 0.0) return q >= 0.0;
    const double t = q / p;
    if (p < 0.0) {
      if (t > leave) return false;
      enter = std::max(enter, t);
    } else {
      if (t < enter) return false;
      leave = std::min(leave, t);
    }
    return enter <= leave;
  };
  if (!bound(-dx, a->x - left) || !bound(dx, right - a->x) ||
      !bound(-dy, a->y - top) || !bound(dy, bottom - a->y)) return false;
  const SP old = *a;
  *a = {old.x + enter * dx, old.y + enter * dy};
  *b = {old.x + leave * dx, old.y + leave * dy};
  return true;
}

// Draw and touch extents must include rotated corners and visible outlines.
/** Conservatively bound rotated marker corners and their centered stroke. */
double MarkerExtent(const OverlayObject& o) {
  const double half_size = o.style.marker_size_px * (o.selected ? 0.65 : 0.5);
  double shape_radius = 1.0;
  if (o.style.marker_shape == MarkerShape::kSquare) shape_radius = std::sqrt(2.0);
  if (o.style.marker_shape == MarkerShape::kArrow)
    shape_radius = std::sqrt(1.0 + 0.22 * 0.22);
  const double stroke = (o.style.outline || o.selected)
      ? (o.selected ? 2.0 : o.style.stroke_width_px) * 0.5 : 0.0;
  // Square stroke rasterization can extend diagonally beyond its corner.
  return half_size * shape_radius + stroke*std::sqrt(2.0) + 1.0;
}

// Sutherland-Hodgman polygon clipping to the framebuffer/view rectangle.
// No target is asked to scan-convert huge offscreen polygons.
/** Clip polygon vertices against one viewport boundary. */
void ClipEdge(const std::vector<SP>& in, std::vector<SP>* out, int edge,
              double bound) {
  out->clear();
  if (in.empty()) return;
  auto inside = [&](SP p) {
    switch(edge) { case 0: return p.x >= bound; case 1: return p.x <= bound;
                   case 2: return p.y >= bound; default: return p.y <= bound; }
  };
  auto intersect = [&](SP a, SP b) {
    const bool vertical = edge < 2;
    const double da = vertical ? a.x : a.y;
    const double db = vertical ? b.x : b.y;
    const double t = (bound - da) / (db - da);
    return SP{a.x + t * (b.x - a.x), a.y + t * (b.y - a.y)};
  };
  SP prev = in.back();
  bool prev_in = inside(prev);
  for (const SP cur : in) {
    const bool cur_in = inside(cur);
    if (prev_in != cur_in) out->push_back(intersect(prev, cur));
    if (cur_in) out->push_back(cur);
    prev = cur;
    prev_in = cur_in;
  }
}

/** Clip a complete polygon ring against the four viewport boundaries. */
void ClipPolygon(OverlayScratch& scratch, PixelRect clip) {
  ClipEdge(scratch.path, &scratch.clip_a, 0, clip.x);
  ClipEdge(scratch.clip_a, &scratch.clip_b, 1, clip.x + clip.width - 1);
  ClipEdge(scratch.clip_b, &scratch.clip_a, 2, clip.y);
  ClipEdge(scratch.clip_a, &scratch.clip_b, 3, clip.y + clip.height - 1);
}

/** Use even-odd crossings to classify a point inside one ring. */
bool PixelInPolygon(const std::vector<SP>& polygon, double x, double y) {
  bool inside = false;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
    const SP a = polygon[i], b = polygon[j];
    if ((a.y > y) != (b.y > y) &&
        x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) inside = !inside;
  }
  return inside;
}

/** Compute squared touch distance to a projected line segment. */
double SegmentDistSq(SP a, SP b, double x, double y) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double l2 = dx * dx + dy * dy;
  const double t = l2 <= 0 ? 0 : std::clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.0, 1.0);
  const double ex = a.x + t * dx - x, ey = a.y + t * dy - y;
  return ex * ex + ey * ey;
}

// A screen-space drawing pass for one object at a single world-copy shift.
/** Render one overlay object at one world-copy shift without clearing the base map. */
void DrawOne(const OverlayObject& o, const Viewport& v, RenderTarget* target,
             OverlayScratch& scratch, PixelRect clip, double shift) {
  if (o.kind == OverlayKind::kMarker) {
    double x, y;
    if (!ProjectLatLon(v, o.position, &x, &y)) return;
    x += shift;
    const double r = o.style.marker_size_px * (o.selected ? 0.65 : 0.5);
    const double extent = MarkerExtent(o);
    if (x < clip.x - extent || x > clip.x + clip.width - 1 + extent ||
        y < clip.y - extent || y > clip.y + clip.height - 1 + extent) return;
    constexpr size_t kMarkerPoints = 16;
    const bool circular = o.style.marker_shape == MarkerShape::kCircle;
    const size_t n = circular ? kMarkerPoints :
        (o.style.marker_shape == MarkerShape::kArrow ? 7 :
         (o.style.marker_shape == MarkerShape::kTriangle ? 3 : 4));
    int xy[2 * kMarkerPoints];
    const double heading = std::remainder(o.bearing_deg, 360.0) * kPi / 180.0;
    const double sine = std::sin(heading), cosine = std::cos(heading);
    for (size_t i = 0; i < n; ++i) {
      double px = 0, py = 0;
      if (circular) {
        const double a = 2 * kPi * i / n;
        px = std::sin(a); py = -std::cos(a);
      } else if (o.style.marker_shape == MarkerShape::kSquare) {
        constexpr double corners[][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
        px = corners[i][0]; py = corners[i][1];
      } else if (o.style.marker_shape == MarkerShape::kDiamond) {
        constexpr double corners[][2] = {{0,-1},{1,0},{0,1},{-1,0}};
        px = corners[i][0]; py = corners[i][1];
      } else if (o.style.marker_shape == MarkerShape::kTriangle) {
        constexpr double corners[][2] = {{0,-1},{0.866,0.5},{-0.866,0.5}};
        px = corners[i][0]; py = corners[i][1];
      } else {
        constexpr double corners[][2] = {{0,-1},{0.65,0.7},{0.22,0.48},
                                         {0.22,1},{-0.22,1},{-0.22,0.48},{-0.65,0.7}};
        px = corners[i][0]; py = corners[i][1];
      }
      const double rx = px * cosine - py * sine;
      const double ry = px * sine + py * cosine;
      xy[2*i] = SafeInt(x + rx * r); xy[2*i+1] = SafeInt(y + ry * r);
    }
    if (o.style.fill) {
      scratch.path.clear();
      for (size_t i = 0; i < n; ++i)
        scratch.path.push_back({static_cast<double>(xy[2*i]),
                                static_cast<double>(xy[2*i+1])});
      ClipPolygon(scratch, clip);
      if (scratch.clip_b.size() >= 3) {
        scratch.polygon_xy.clear();
        for (SP p : scratch.clip_b) {
          scratch.polygon_xy.push_back(SafeInt(p.x));
          scratch.polygon_xy.push_back(SafeInt(p.y));
        }
        target->FillPolygon(scratch.polygon_xy.data(), scratch.clip_b.size(), o.style.color);
      }
    }
    if (o.style.outline || o.selected) {
      for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        int x0 = xy[2*i], y0 = xy[2*i+1];
        int x1 = xy[2*j], y1 = xy[2*j+1];
        const float width = o.selected ? 2.0f : o.style.stroke_width_px;
        const int half = static_cast<int>(std::ceil(width * 0.5f));
        if (ClipLineToPixelRect(&x0, &y0, &x1, &y1,
            clip.x-half, clip.y-half, clip.x+clip.width-1+half,
            clip.y+clip.height-1+half)) {
          target->DrawLine(x0, y0, x1, y1,
                           o.selected ? Color::Rgb(255, 230, 0) : o.style.outline_color,
                           width);
        }
      }
    }
    return;
  }
  const std::vector<LatLon>* points;
  size_t n;
  ShapeVertices(o, scratch, &points, &n);
  const bool area = o.kind == OverlayKind::kPolygon || o.kind == OverlayKind::kCircle ||
                    o.kind == OverlayKind::kSector;
  const bool closed = area || o.closed;
  if (!area) {
    if (!ProjectPath(v, points->data(), n, shift, &scratch.path)) return;
    const size_t count = closed ? n : n - 1;
    for (size_t i = 0; i < count; ++i) {
      SP a = scratch.path[i], b = scratch.path[(i + 1) % n];
      if (ClipProjectedLine(&a, &b, clip, o.style.stroke_width_px)) {
        target->DrawLine(SafeInt(a.x), SafeInt(a.y), SafeInt(b.x), SafeInt(b.y),
                         o.style.color, o.style.stroke_width_px);
      }
    }
    return;
  }
  scratch.polygon_xy.clear();
  scratch.polygon_ring_sizes.clear();
  const size_t ring_count = o.kind == OverlayKind::kPolygon ? o.ring_sizes.size() : 1;
  size_t offset = 0;
  double anchor = NAN;
  for (size_t ring = 0; ring < ring_count; ++ring) {
    const size_t len = o.kind == OverlayKind::kPolygon ? o.ring_sizes[ring] : n;
    if (!ProjectPath(v, points->data() + offset, len, shift, &scratch.path, anchor)) return;
    if (ring == 0 && !scratch.path.empty()) anchor = scratch.path.front().x - shift;
    offset += len;
    ClipPolygon(scratch, clip);
    if (scratch.clip_b.size() < 3) continue;
    scratch.polygon_ring_sizes.push_back(scratch.clip_b.size());
    for (SP p : scratch.clip_b) {
      scratch.polygon_xy.push_back(SafeInt(p.x));
      scratch.polygon_xy.push_back(SafeInt(p.y));
    }
  }
  if (o.style.fill && !scratch.polygon_ring_sizes.empty()) {
    target->FillPolygonRings(scratch.polygon_xy.data(), scratch.polygon_ring_sizes.data(),
                             scratch.polygon_ring_sizes.size(), o.style.color);
  }
  if (o.style.outline || !o.style.fill) {
    // Outline original edges, not artificial polygon-clip edges. An overlay
    // intersecting the screen boundary must not paint a false border there.
    offset = 0;
    anchor = NAN;
    for (size_t ring = 0; ring < ring_count; ++ring) {
      const size_t len = o.kind == OverlayKind::kPolygon ? o.ring_sizes[ring] : n;
      if (!ProjectPath(v, points->data() + offset, len, shift, &scratch.path, anchor)) return;
      if (ring == 0) anchor = scratch.path.front().x - shift;
      offset += len;
      for (size_t i = 0; i < len; ++i) {
        SP a = scratch.path[i], b = scratch.path[(i+1)%len];
        if (ClipProjectedLine(&a, &b, clip, o.style.stroke_width_px)) {
          target->DrawLine(SafeInt(a.x), SafeInt(a.y), SafeInt(b.x), SafeInt(b.y),
                           o.style.outline_color, o.style.stroke_width_px);
        }
      }
    }
  }
}

/** Hit-test a single object against the geometry used for rendering. */
bool HitOne(const OverlayObject& o, const Viewport& v, double x, double y,
            OverlayScratch& scratch, float tolerance, double shift) {
  if (o.kind == OverlayKind::kMarker) {
    double sx, sy;
    if (!ProjectLatLon(v, o.position, &sx, &sy)) return false;
    sx += shift;
    const double r = std::max(4.0, MarkerExtent(o)) + tolerance;
    return (x - sx) * (x - sx) + (y - sy) * (y - sy) <= r * r;
  }
  const std::vector<LatLon>* points;
  size_t n;
  ShapeVertices(o, scratch, &points, &n);
  const bool area = o.kind == OverlayKind::kPolygon || o.kind == OverlayKind::kCircle ||
                    o.kind == OverlayKind::kSector;
  if (!area) {
    if (!ProjectPath(v, points->data(), n, shift, &scratch.path)) return false;
    const double r = tolerance + o.style.stroke_width_px * 0.5;
    for (size_t i = 1; i < n; ++i)
      if (SegmentDistSq(scratch.path[i-1], scratch.path[i], x, y) <= r*r) return true;
    return o.closed && SegmentDistSq(scratch.path[n-1],scratch.path[0],x,y)<=r*r;
  }
  const size_t ring_count = o.kind == OverlayKind::kPolygon ? o.ring_sizes.size() : 1;
  size_t offset = 0;
  double anchor = NAN;
  bool inside = false;
  for (size_t ring = 0; ring < ring_count; ++ring) {
    const size_t len = o.kind == OverlayKind::kPolygon ? o.ring_sizes[ring] : n;
    if (!ProjectPath(v, points->data() + offset, len, shift, &scratch.path, anchor)) return false;
    if (ring == 0) anchor = scratch.path.front().x - shift;
    offset += len;
    if (o.style.fill && PixelInPolygon(scratch.path, x, y)) inside = !inside;
    const double r = tolerance + o.style.stroke_width_px * 0.5;
    if (o.style.outline || !o.style.fill) {
      for (size_t i = 0; i < len; ++i)
        if (SegmentDistSq(scratch.path[i],scratch.path[(i+1)%len],x,y)<=r*r) return true;
    }
  }
  return inside;
}

/** Visit visible layers in stable painter order or reverse picking order. */
template <typename Fn>
void VisitDrawOrder(const OverlayScene& scene, bool reverse, Fn&& fn) {
  std::array<const OverlayLayer*, kOverlayMaxLayers> ordered{};
  size_t count = 0;
  for (const auto& layer : scene.Layers()) {
    if (!layer.visible) continue;
    size_t at = count++;
    while (at > 0 && ordered[at-1]->draw_order > layer.draw_order) {
      ordered[at] = ordered[at-1]; --at;
    }
    ordered[at] = &layer;
  }
  if (reverse) { while (count > 0) fn(*ordered[--count]); }
  else { for (size_t i=0;i<count;++i) fn(*ordered[i]); }
}

// Include copies with a partially visible marker, stroke or touch footprint,
// even when their center lies just outside the nominal world-copy threshold.
/** Visit bounded repeated-world copies whose extents may overlap the viewport. */
template <typename Fn>
void VisitWorldCopies(const Viewport& v, double extra_px, Fn&& fn) {
  const double world = static_cast<double>(TilesPerAxis(v.zoom))*v.tile_size_px;
  const double reach = static_cast<double>(v.width_px)*0.5 + world*0.5 + extra_px;
  const int copies = static_cast<int>(std::min<double>(
      kMaxWorldCopies, std::ceil(reach / world) + 1.0));
  for (int i = -copies; i <= copies; ++i) {
    if (std::abs(i*world) <= reach) fn(i*world);
  }
}
}  // namespace

/** Validate caller coordinates against strict supported Web Mercator bounds. */
bool OverlayCoordinateValid(LatLon p) {
  return Finite(p.lat_deg) && Finite(p.lon_deg) &&
         p.lat_deg >= -kMercatorMaxLatDeg && p.lat_deg <= kMercatorMaxLatDeg &&
         p.lon_deg >= -180.0 && p.lon_deg <= 180.0;
}

/** Clamp requested scene budgets to fixed maximum capacities. */
OverlayScene::OverlayScene(OverlayLimits limits) : limits_(limits) {
  limits_.layers = std::min(limits_.layers, kOverlayMaxLayers);
  limits_.objects = std::min(limits_.objects, kOverlayMaxObjects);
  limits_.vertices = std::min(limits_.vertices, kOverlayMaxVertices);
  layers_.reserve(limits_.layers);
}
/** Find a mutable layer by its application-defined identifier. */
OverlayLayer* OverlayScene::FindLayer(uint32_t id) {
  for (auto& l : layers_) if (l.id == id) return &l;
  return nullptr;
}
/** Find a read-only layer without changing scene state. */
const OverlayLayer* OverlayScene::FindLayer(uint32_t id) const {
  for (const auto& l : layers_) if (l.id == id) return &l;
  return nullptr;
}
/** Sum stored object counts across all layers. */
size_t OverlayScene::ObjectCount() const {
  size_t n = 0; for (const auto& l : layers_) n += l.objects.size(); return n;
}
/** Sum input geometry vertices across the scene for capacity checks. */
size_t OverlayScene::VertexCount() const {
  size_t n = 0; for (const auto& l : layers_) for (const auto& o : l.objects) n += VertexUse(o);
  return n;
}
/** Create a uniquely identified layer within the configured limit. */
bool OverlayScene::AddLayer(uint32_t id, int order) {
  if (FindLayer(id) || layers_.size() >= limits_.layers) return false;
  OverlayLayer l; l.id = id; l.draw_order = order;
  layers_.push_back(std::move(l)); Change(); return true;
}
/** Toggle a layer without deleting its geographic objects. */
bool OverlayScene::SetLayerVisible(uint32_t id, bool visible) {
  auto* l = FindLayer(id); if (!l) return false;
  if (l->visible != visible) { l->visible = visible; Change(); }
  return true;
}
/** Set the layer's stable painter-order priority. */
bool OverlayScene::SetLayerOrder(uint32_t id, int order) {
  auto* l = FindLayer(id); if (!l) return false;
  if (l->draw_order != order) { l->draw_order = order; Change(); }
  return true;
}
/** Erase a layer and its objects, invalidating the scene revision. */
bool OverlayScene::RemoveLayer(uint32_t id) {
  for (size_t i=0;i<layers_.size();++i) if (layers_[i].id==id) {
    layers_.erase(layers_.begin()+i); Change(); return true;
  }
  return false;
}
/** Remove all objects from one existing layer. */
bool OverlayScene::ClearLayer(uint32_t id) {
  auto* l=FindLayer(id); if (!l) return false;
  if (!l->objects.empty()) { l->objects.clear(); Change(); }
  return true;
}
/** Validate and replace or append an object without exceeding budgets. */
bool OverlayScene::Upsert(uint32_t id, const OverlayObject& o) {
  auto* l=FindLayer(id); if (!l || !ValidObject(o)) return false;
  for (auto& existing : l->objects) if (existing.id == o.id) {
    if (VertexCount() - VertexUse(existing) + VertexUse(o) > limits_.vertices) return false;
    existing = o; Change(); return true;
  }
  if (ObjectCount() >= limits_.objects || VertexCount() + VertexUse(o) > limits_.vertices) return false;
  l->objects.push_back(o); Change(); return true;
}
/** Remove a single overlay object by its opaque ID. */
bool OverlayScene::Remove(uint32_t id, uint64_t object_id) {
  auto* l=FindLayer(id); if (!l) return false;
  for (size_t i=0;i<l->objects.size();++i) if (l->objects[i].id == object_id) {
    l->objects.erase(l->objects.begin()+i); Change(); return true;
  }
  return false;
}

/** Provision reusable buffers for the largest supported clipping workload. */
OverlayScratch::OverlayScratch() {
  generated.reserve(kOverlayArcSegments + 2);
  path.reserve(kOverlayMaxVerticesPerObject);
  // A concave ring may gain up to two boundary intersections per original
  // edge plus rectangle corners. Reserve for worst-case four-edge clipping;
  // polygon_xy also accounts for each supported ring's corner vertices.
  constexpr size_t kClippedRingCapacity = 3*kOverlayMaxVerticesPerObject + 4;
  clip_a.reserve(kClippedRingCapacity);
  clip_b.reserve(kClippedRingCapacity);
  polygon_xy.reserve(2*(3*kOverlayMaxVerticesPerObject + 4*kOverlayMaxRings));
  polygon_ring_sizes.reserve(kOverlayMaxRings);
}

/** Draw visible overlays and restore the target's original clipping state. */
bool RenderOverlays(const OverlayScene& scene, const Viewport& viewport,
                    RenderTarget* target, OverlayScratch* scratch) {
  if (!ValidViewport(viewport) || !target || !scratch || target->Width() <= 0 || target->Height() <= 0) return false;
  const PixelRect prior = target->ClipRect();
  const PixelRect clip = IntersectPixelRects(prior, {0,0,viewport.width_px,viewport.height_px});
  if (clip.width == 0 || clip.height == 0) return true;
  const bool need_clip = clip.x != prior.x || clip.y != prior.y ||
      clip.width != prior.width || clip.height != prior.height;
  // A target that cannot accept a narrower scissor must not draw outside
  // the requested viewport (including thick strokes and polygon fills).
  if (need_clip && !target->SetClipRect(clip)) return false;
  VisitDrawOrder(scene, false, [&](const OverlayLayer& layer) {
    for (const auto& o : layer.objects) {
      if (!o.visible) continue;
      const double extra = o.kind == OverlayKind::kMarker
          ? MarkerExtent(o) : std::ceil(o.style.stroke_width_px*0.5);
      VisitWorldCopies(viewport, extra, [&](double shift) {
        DrawOne(o,viewport,target,*scratch,clip,shift);
      });
    }
  });
  if (need_clip) target->SetClipRect(prior);
  return true;
}

/** Return the topmost visible selectable object within the touch tolerance. */
bool HitTestOverlays(const OverlayScene& scene, const Viewport& viewport,
                     double screen_x, double screen_y, OverlayHit* hit,
                     OverlayScratch* scratch, float tolerance_px) {
  if (!ValidViewport(viewport) || !hit || !scratch || !Finite(screen_x) || !Finite(screen_y) ||
      !Finite(tolerance_px) || screen_x < 0 || screen_y < 0 ||
      screen_x >= viewport.width_px || screen_y >= viewport.height_px) return false;
  const float tolerance = std::clamp(tolerance_px, 0.0f, 32.0f);
  bool found = false;
  VisitDrawOrder(scene, true, [&](const OverlayLayer& layer) {
    if (found) return;
    for (size_t i=layer.objects.size();i>0;--i) {
      const auto& o=layer.objects[i-1];
      if (!o.visible || !o.selectable) continue;
      const double extra = (o.kind == OverlayKind::kMarker
          ? MarkerExtent(o) : std::ceil(o.style.stroke_width_px*0.5)) + tolerance;
      VisitWorldCopies(viewport, extra, [&](double shift) {
        if (!found && HitOne(o,viewport,screen_x,screen_y,*scratch,tolerance,shift)) {
          *hit = {layer.id, o.id}; found = true;
        }
      });
      if (found) return;
    }
  });
  return found;
}
}  // namespace orcmap

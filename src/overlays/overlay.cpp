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

bool Finite(double x) { return std::isfinite(x); }
bool ValidStyle(const OverlayStyle& s) {
  return Finite(s.marker_size_px) && s.marker_size_px > 0 &&
         s.marker_size_px <= 256 && Finite(s.stroke_width_px) &&
         s.stroke_width_px >= 0 && s.stroke_width_px <= 64 &&
         static_cast<unsigned>(s.marker_shape) <= static_cast<unsigned>(MarkerShape::kArrow);
}
size_t VertexUse(const OverlayObject& o) { return o.vertices.size(); }
bool ValidObject(const OverlayObject& o) {
  if (!ValidStyle(o.style)) return false;
  if (o.vertices.size() > kOverlayMaxVerticesPerObject ||
      o.ring_sizes.size() > kOverlayMaxRings) return false;
  for (const auto& p : o.vertices) if (!OverlayCoordinateValid(p)) return false;
  switch (o.kind) {
    case OverlayKind::kMarker: return OverlayCoordinateValid(o.position) && o.vertices.empty() && Finite(o.bearing_deg);
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
          !Finite(o.radius_m) || o.radius_m <= 0 ||
          o.radius_m > kMaxDistanceM || !Finite(o.bearing_deg)) return false;
      if (o.kind != OverlayKind::kSector) return true;
      return Finite(o.start_bearing_deg) && Finite(o.sweep_deg) &&
             o.sweep_deg > 0 && o.sweep_deg <= 360;
  }
  return false;
}

bool ValidViewport(const Viewport& v) {
  return ZoomIsValid(v.zoom) && v.width_px > 0 && v.height_px > 0 &&
         v.tile_size_px > 0 && Finite(v.center_lat_deg) &&
         Finite(v.center_lon_deg) && OverlayCoordinateValid({v.center_lat_deg, v.center_lon_deg});
}

// Great-circle destination from initial bearing. This is a spherical-earth
// approximation, NOT a geodesic ellipsoid calculation.
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
    scratch.generated.push_back(o.position);
    for (size_t i = 0; i <= kOverlayArcSegments; ++i)
      scratch.generated.push_back(Destination(o.position, o.radius_m,
          o.start_bearing_deg + o.sweep_deg * i / kOverlayArcSegments));
  }
  *points = &scratch.generated;
  *count = scratch.generated.size();
}

// The viewport's projection picks the closest world copy. Unwrap successive
// vertices against their predecessor so antimeridian crossings are short.
bool ProjectPath(const Viewport& v, const LatLon* points, size_t n,
                 double shift, std::vector<SP>* out, double anchor = NAN) {
  out->clear();
  const double world_px = static_cast<double>(TilesPerAxis(v.zoom)) * v.tile_size_px;
  for (size_t i = 0; i < n; ++i) {
    double x, y;
    if (!ProjectLatLon(v, points[i], &x, &y) || !Finite(x) || !Finite(y)) return false;
    if (i == 0 && Finite(anchor)) x += world_px * std::round((anchor - x) / world_px);
    if (i > 0) x += world_px * std::round((out->back().x - x) / world_px);
    out->push_back({x + shift, y});
  }
  return true;
}

int SafeInt(double x) {
  return static_cast<int>(std::clamp(std::round(x), -1000000.0, 1000000.0));
}

// Sutherland-Hodgman polygon clipping to the framebuffer/view rectangle.
// No target is asked to scan-convert huge offscreen polygons.
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

void ClipPolygon(OverlayScratch& scratch, PixelRect clip) {
  ClipEdge(scratch.path, &scratch.clip_a, 0, clip.x);
  ClipEdge(scratch.clip_a, &scratch.clip_b, 1, clip.x + clip.width - 1);
  ClipEdge(scratch.clip_b, &scratch.clip_a, 2, clip.y);
  ClipEdge(scratch.clip_a, &scratch.clip_b, 3, clip.y + clip.height - 1);
}

bool PixelInPolygon(const std::vector<SP>& polygon, double x, double y) {
  bool inside = false;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
    const SP a = polygon[i], b = polygon[j];
    if ((a.y > y) != (b.y > y) &&
        x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) inside = !inside;
  }
  return inside;
}

double SegmentDistSq(SP a, SP b, double x, double y) {
  const double dx = b.x - a.x, dy = b.y - a.y;
  const double l2 = dx * dx + dy * dy;
  const double t = l2 <= 0 ? 0 : std::clamp(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.0, 1.0);
  const double ex = a.x + t * dx - x, ey = a.y + t * dy - y;
  return ex * ex + ey * ey;
}

// A screen-space drawing pass for one object at a single world-copy shift.
void DrawOne(const OverlayObject& o, const Viewport& v, RenderTarget* target,
             OverlayScratch& scratch, PixelRect clip, double shift) {
  if (o.kind == OverlayKind::kMarker) {
    double x, y;
    if (!ProjectLatLon(v, o.position, &x, &y)) return;
    x += shift;
    const double r = o.style.marker_size_px * (o.selected ? 0.65 : 0.5);
    if (x < clip.x - r - 2 || x > clip.x + clip.width + r + 2 ||
        y < clip.y - r - 2 || y > clip.y + clip.height + r + 2) return;
    constexpr size_t kMarkerPoints = 16;
    const bool circular = o.style.marker_shape == MarkerShape::kCircle;
    const size_t n = circular ? kMarkerPoints :
        (o.style.marker_shape == MarkerShape::kArrow ? 7 :
         (o.style.marker_shape == MarkerShape::kTriangle ? 3 : 4));
    int xy[2 * kMarkerPoints];
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
      const double heading = std::remainder(o.bearing_deg, 360.0) * kPi / 180.0;
      const double rx = px * std::cos(heading) - py * std::sin(heading);
      const double ry = px * std::sin(heading) + py * std::cos(heading);
      xy[2*i] = SafeInt(x + rx * r); xy[2*i+1] = SafeInt(y + ry * r);
    }
    if (o.style.fill) target->FillPolygon(xy, n, o.style.color);
    if (o.style.outline || o.selected) {
      for (size_t i = 0; i < n; ++i) {
        const size_t j = (i + 1) % n;
        target->DrawLine(xy[2*i], xy[2*i+1], xy[2*j], xy[2*j+1],
                         o.selected ? Color::Rgb(255, 230, 0) : o.style.outline_color,
                         o.selected ? 2 : o.style.stroke_width_px);
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
      int x0 = SafeInt(a.x), y0 = SafeInt(a.y), x1 = SafeInt(b.x), y1 = SafeInt(b.y);
      const int half = static_cast<int>(std::ceil(o.style.stroke_width_px * 0.5f));
      if (ClipLineToPixelRect(&x0, &y0, &x1, &y1,
          clip.x-half, clip.y-half, clip.x+clip.width-1+half, clip.y+clip.height-1+half)) {
        target->DrawLine(x0, y0, x1, y1, o.style.color, o.style.stroke_width_px);
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
      const int half = static_cast<int>(std::ceil(o.style.stroke_width_px * 0.5f));
      for (size_t i = 0; i < len; ++i) {
        const SP a = scratch.path[i], b = scratch.path[(i+1)%len];
        int x0 = SafeInt(a.x), y0 = SafeInt(a.y), x1 = SafeInt(b.x), y1 = SafeInt(b.y);
        if (ClipLineToPixelRect(&x0,&y0,&x1,&y1,
            clip.x-half,clip.y-half,clip.x+clip.width-1+half,clip.y+clip.height-1+half)) {
          target->DrawLine(x0,y0,x1,y1,o.style.outline_color,o.style.stroke_width_px);
        }
      }
    }
  }
}

bool HitOne(const OverlayObject& o, const Viewport& v, double x, double y,
            OverlayScratch& scratch, float tolerance, double shift) {
  if (o.kind == OverlayKind::kMarker) {
    double sx, sy;
    if (!ProjectLatLon(v, o.position, &sx, &sy)) return false;
    sx += shift;
    const double r = std::max<double>(o.style.marker_size_px * (o.selected ? 0.65 : 0.5), 4) + tolerance;
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

// Draw the nearest world and repeat it only when the viewport is wide enough
// to show another copy. In normal zoom this visits one copy.
template <typename Fn>
void VisitWorldCopies(const Viewport& v, Fn&& fn) {
  const double world = static_cast<double>(TilesPerAxis(v.zoom))*v.tile_size_px;
  const int copies = static_cast<int>(std::min<double>(kMaxWorldCopies,
      std::ceil(static_cast<double>(v.width_px) / world) + 2.0));
  for (int i = -copies; i <= copies; ++i) {
    if (std::abs(i*world) < v.width_px * 0.5 + world * 0.5) fn(i*world);
  }
}
}  // namespace

bool OverlayCoordinateValid(LatLon p) {
  return Finite(p.lat_deg) && Finite(p.lon_deg) &&
         p.lat_deg >= -kMercatorMaxLatDeg && p.lat_deg <= kMercatorMaxLatDeg &&
         p.lon_deg >= -180.0 && p.lon_deg <= 180.0;
}

OverlayScene::OverlayScene(OverlayLimits limits) : limits_(limits) {
  limits_.layers = std::min(limits_.layers, kOverlayMaxLayers);
  limits_.objects = std::min(limits_.objects, kOverlayMaxObjects);
  limits_.vertices = std::min(limits_.vertices, kOverlayMaxVertices);
  layers_.reserve(limits_.layers);
}
OverlayLayer* OverlayScene::FindLayer(uint32_t id) {
  for (auto& l : layers_) if (l.id == id) return &l;
  return nullptr;
}
const OverlayLayer* OverlayScene::FindLayer(uint32_t id) const {
  for (const auto& l : layers_) if (l.id == id) return &l;
  return nullptr;
}
size_t OverlayScene::ObjectCount() const {
  size_t n = 0; for (const auto& l : layers_) n += l.objects.size(); return n;
}
size_t OverlayScene::VertexCount() const {
  size_t n = 0; for (const auto& l : layers_) for (const auto& o : l.objects) n += VertexUse(o);
  return n;
}
bool OverlayScene::AddLayer(uint32_t id, int order) {
  if (FindLayer(id) || layers_.size() >= limits_.layers) return false;
  OverlayLayer l; l.id = id; l.draw_order = order;
  layers_.push_back(std::move(l)); Change(); return true;
}
bool OverlayScene::SetLayerVisible(uint32_t id, bool visible) {
  auto* l = FindLayer(id); if (!l) return false;
  if (l->visible != visible) { l->visible = visible; Change(); }
  return true;
}
bool OverlayScene::SetLayerOrder(uint32_t id, int order) {
  auto* l = FindLayer(id); if (!l) return false;
  if (l->draw_order != order) { l->draw_order = order; Change(); }
  return true;
}
bool OverlayScene::RemoveLayer(uint32_t id) {
  for (size_t i=0;i<layers_.size();++i) if (layers_[i].id==id) {
    layers_.erase(layers_.begin()+i); Change(); return true;
  }
  return false;
}
bool OverlayScene::ClearLayer(uint32_t id) {
  auto* l=FindLayer(id); if (!l) return false;
  if (!l->objects.empty()) { l->objects.clear(); Change(); }
  return true;
}
bool OverlayScene::Upsert(uint32_t id, const OverlayObject& o) {
  auto* l=FindLayer(id); if (!l || !ValidObject(o)) return false;
  for (auto& existing : l->objects) if (existing.id == o.id) {
    if (VertexCount() - VertexUse(existing) + VertexUse(o) > limits_.vertices) return false;
    existing = o; Change(); return true;
  }
  if (ObjectCount() >= limits_.objects || VertexCount() + VertexUse(o) > limits_.vertices) return false;
  l->objects.push_back(o); Change(); return true;
}
bool OverlayScene::Remove(uint32_t id, uint64_t object_id) {
  auto* l=FindLayer(id); if (!l) return false;
  for (size_t i=0;i<l->objects.size();++i) if (l->objects[i].id == object_id) {
    l->objects.erase(l->objects.begin()+i); Change(); return true;
  }
  return false;
}

OverlayScratch::OverlayScratch() {
  generated.reserve(kOverlayArcSegments + 2);
  path.reserve(kOverlayMaxVerticesPerObject);
  clip_a.reserve(kOverlayMaxVerticesPerObject + 8);
  clip_b.reserve(kOverlayMaxVerticesPerObject + 8);
  polygon_xy.reserve((kOverlayMaxVerticesPerObject + kOverlayMaxRings*4)*2);
  polygon_ring_sizes.reserve(kOverlayMaxRings);
}

bool RenderOverlays(const OverlayScene& scene, const Viewport& viewport,
                    RenderTarget* target, OverlayScratch* scratch) {
  if (!ValidViewport(viewport) || !target || !scratch || target->Width() <= 0 || target->Height() <= 0) return false;
  const PixelRect prior = target->ClipRect();
  const PixelRect clip = IntersectPixelRects(prior, {0,0,viewport.width_px,viewport.height_px});
  if (clip.width == 0 || clip.height == 0) return true;
  const bool clipped = target->SetClipRect(clip);
  VisitDrawOrder(scene, false, [&](const OverlayLayer& layer) {
    for (const auto& o : layer.objects) {
      if (!o.visible) continue;
      VisitWorldCopies(viewport, [&](double shift) { DrawOne(o,viewport,target,*scratch,clip,shift); });
    }
  });
  if (clipped) target->SetClipRect(prior);
  return true;
}

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
      VisitWorldCopies(viewport, [&](double shift) {
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

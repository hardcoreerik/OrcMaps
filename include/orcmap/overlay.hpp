#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "orcmap/color.hpp"
#include "orcmap/geo.hpp"
#include "orcmap/render_target.hpp"
#include "orcmap/viewport.hpp"

namespace orcmap {

// Application data never enters the base map. Object IDs are opaque to OrcMaps.
// Storage limits are hard upper bounds, not just initial reserve hints.
inline constexpr size_t kOverlayMaxLayers = 16;
inline constexpr size_t kOverlayMaxObjects = 512;
inline constexpr size_t kOverlayMaxVertices = 4096;
inline constexpr size_t kOverlayMaxVerticesPerObject = 256;
inline constexpr size_t kOverlayMaxRings = 32;
inline constexpr size_t kOverlayArcSegments = 48;

enum class OverlayKind : uint8_t {
  kMarker, kPolyline, kPolygon, kCircle, kSector, kVector
};
enum class MarkerShape : uint8_t {
  kCircle, kSquare, kDiamond, kTriangle, kArrow
};

struct OverlayStyle {
  Color color = Color::Rgb(0, 205, 225);
  Color outline_color = Color::Rgb(255, 255, 255);
  float stroke_width_px = 1.0f;
  float marker_size_px = 12.0f;  // Diameter, independent of geographic scale.
  MarkerShape marker_shape = MarkerShape::kCircle;
  bool fill = true;
  bool outline = true;
};

struct OverlayObject {
  uint64_t id = 0;                         // Unique within its layer.
  OverlayKind kind = OverlayKind::kMarker;
  LatLon position{0.0, 0.0};             // Marker/sector/circle/vector origin.
  std::vector<LatLon> vertices;           // Ordered polyline or polygon vertices.
  std::vector<size_t> ring_sizes;         // Polygon rings, exterior first, holes next.
  double radius_m = 0.0;                // Circle/sector radius or vector length.
  double bearing_deg = 0.0;             // Marker heading or vector direction, clockwise from north.
  double start_bearing_deg = 0.0;       // Sector start, clockwise from north.
  double sweep_deg = 90.0;              // Sector arc clockwise, >0 through <=360.
  OverlayStyle style;
  bool closed = false;                   // Polyline closure (polygons always closed).
  bool visible = true;
  bool selectable = true;
  bool selected = false;
};

struct OverlayLayer {
  uint32_t id = 0;
  int draw_order = 0;
  bool visible = true;
  std::vector<OverlayObject> objects;
};

struct OverlayLimits {
  size_t layers = 8;
  size_t objects = 128;
  size_t vertices = 2048;
};

// Scene owns the supplied coordinates. Mutate only via methods, not references.
// APIs fail without mutation when limits or geometry validation fail.
// Not thread safe: caller serializes updates, draw and hit test.
class OverlayScene {
 public:
  explicit OverlayScene(OverlayLimits limits = {});
  bool AddLayer(uint32_t id, int draw_order = 0);
  bool SetLayerVisible(uint32_t id, bool visible);
  bool SetLayerOrder(uint32_t id, int draw_order);
  bool RemoveLayer(uint32_t id);
  bool ClearLayer(uint32_t id);
  bool Upsert(uint32_t layer_id, const OverlayObject& object);
  bool Remove(uint32_t layer_id, uint64_t object_id);
  const std::vector<OverlayLayer>& Layers() const { return layers_; }
  const OverlayLimits& Limits() const { return limits_; }
  size_t ObjectCount() const;
  size_t VertexCount() const;
  uint64_t Revision() const { return revision_; }
  bool Dirty() const { return dirty_; }
  void AcknowledgeRedraw() { dirty_ = false; }

 private:
  OverlayLayer* FindLayer(uint32_t id);
  const OverlayLayer* FindLayer(uint32_t id) const;
  void Change() { ++revision_; dirty_ = true; }
  OverlayLimits limits_;
  std::vector<OverlayLayer> layers_;
  uint64_t revision_ = 0;
  bool dirty_ = false;
};

struct OverlayHit {
  uint32_t layer_id = 0;
  uint64_t object_id = 0;
};

// Reuse overlay geometry scratch to avoid repeat allocations once capacities
// are warmed. RenderTarget implementations may separately allocate; reuse and
// warm the display target as well. One scratch per simultaneous renderer/thread.
struct OverlayScratch {
  struct ScreenPoint { double x = 0, y = 0; };
  std::vector<LatLon> generated;
  std::vector<ScreenPoint> path;
  std::vector<ScreenPoint> clip_a;
  std::vector<ScreenPoint> clip_b;
  std::vector<int> polygon_xy;
  std::vector<size_t> polygon_ring_sizes;
  OverlayScratch();
};

// Overlay pass NEVER clears or repaints the basemap. Caller must recompose
// its original map pixels when clearing/moving/removing an old overlay.
// Return false for invalid viewport/target or an unsupported narrower scissor;
// callers should skip hit testing for a frame that failed to render.
bool RenderOverlays(const OverlayScene& scene, const Viewport& viewport,
                    RenderTarget* target, OverlayScratch* scratch);

// Returns topmost selectable visible overlay, using the same layer order as draw.
// 'tolerance_px' is additional touch radius (clamped to 0..32 px).
bool HitTestOverlays(const OverlayScene& scene, const Viewport& viewport,
                     double screen_x, double screen_y, OverlayHit* hit,
                     OverlayScratch* scratch, float tolerance_px = 6.0f);

// Strict geographic domain: -85.05112878..85.05112878, -180..180.
// No silent latitude clamping for consumer-provided coordinates.
bool OverlayCoordinateValid(LatLon point);

}  // namespace orcmap

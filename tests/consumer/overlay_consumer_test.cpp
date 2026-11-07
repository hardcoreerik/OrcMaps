// No src/** or third_party/** includes: this is a standalone external consumer.
#include "orcmap/overlay.hpp"

#include <cstdio>

class ConsumerTarget final : public orcmap::RenderTarget {
 public:
  int Width() const override { return 320; }
  int Height() const override { return 240; }
  void FillRect(int,int,int,int,orcmap::Color) override {}
  void DrawPoint(int,int,orcmap::Color) override {}
  void DrawLine(int,int,int,int,orcmap::Color,float) override { ++lines; }
  void FillPolygon(const int*,size_t,orcmap::Color) override { ++polygons; }
  int lines = 0;
  int polygons = 0;
};

int main() {
  orcmap::OverlayScene scene;
  if (!scene.AddLayer(42)) return 1;
  orcmap::OverlayObject marker;
  marker.id = 0x123456789ULL;
  marker.position = {44.05, -123.08};
  marker.style.marker_shape = orcmap::MarkerShape::kArrow;
  marker.bearing_deg = 90.0;
  if (!scene.Upsert(42, marker)) return 2;
  orcmap::Viewport viewport;
  viewport.center_lat_deg = 44.05;
  viewport.center_lon_deg = -123.08;
  viewport.zoom = 10;
  viewport.width_px = 320;
  viewport.height_px = 240;
  orcmap::OverlayScratch scratch;
  ConsumerTarget target;
  if (!orcmap::RenderOverlays(scene, viewport, &target, &scratch)) return 3;
  if (target.polygons != 1) return 4;
  orcmap::OverlayHit hit;
  if (!orcmap::HitTestOverlays(scene, viewport, 160, 120, &hit, &scratch)) return 5;
  if (hit.layer_id != 42 || hit.object_id != marker.id) return 6;
  std::puts("PASS: external component overlay interface");
  return 0;
}

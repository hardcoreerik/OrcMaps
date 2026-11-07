// Generic, synthetic overlay example. No real locations or observations.
// Does not depend on OrcSDR, M5GFX, or storage/network services.
#include "orcmap/overlay.hpp"
#include <cstdio>

class CountingTarget : public orcmap::RenderTarget {
 public:
  int Width() const override { return 320; }
  int Height() const override { return 240; }
  void FillRect(int,int,int,int,orcmap::Color) override {}
  void DrawPoint(int,int,orcmap::Color) override {}
  void DrawLine(int,int,int,int,orcmap::Color,float) override { ++lines; }
  void FillPolygon(const int*,size_t,orcmap::Color) override { ++polygons; }
  void FillPolygonRings(const int*,const size_t*,size_t,orcmap::Color) override {
    ++polygons;
  }
  int lines = 0, polygons = 0;
};

int main() {
  orcmap::Viewport v;
  v.center_lat_deg = 0;
  v.center_lon_deg = 0;
  v.zoom = 7;
  v.width_px = 320;
  v.height_px = 240;
  orcmap::OverlayScene overlays({2, 32, 256});
  overlays.AddLayer(1);
  overlays.AddLayer(2, 5);

  orcmap::OverlayObject moving;
  moving.id = 1001;
  moving.kind = orcmap::OverlayKind::kMarker;
  moving.position = {0, 0};
  moving.bearing_deg = 125;
  moving.style.marker_shape = orcmap::MarkerShape::kArrow;
  overlays.Upsert(2, moving);

  orcmap::OverlayObject path;
  path.id = 2001;
  path.kind = orcmap::OverlayKind::kPolyline;
  path.vertices = {{-0.1, -0.25}, {0, 0}, {0.15, 0.2}};
  overlays.Upsert(1, path);

  orcmap::OverlayScratch scratch;
  CountingTarget target;
  if (!orcmap::RenderOverlays(overlays, v, &target, &scratch)) return 1;
  orcmap::OverlayHit hit;
  if (!orcmap::HitTestOverlays(overlays, v, 160, 120, &hit, &scratch)) return 2;
  std::printf("Synthetic map: %d lines, %d polygons; selected layer=%u object=%llu\n",
              target.lines, target.polygons, hit.layer_id,
              static_cast<unsigned long long>(hit.object_id));
  return hit.layer_id == 2 && hit.object_id == 1001 ? 0 : 3;
}

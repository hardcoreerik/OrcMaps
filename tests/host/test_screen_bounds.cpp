#include <cstdio>

#include "framebuffer_target.hpp"
#include "orcmap/renderer.hpp"

int main() {
  orcmap::Viewport vp;
  vp.width_px = 64;
  vp.height_px = 64;
  vp.zoom = 0;
  orcmap::TilePlacement placement;
  placement.tile = {0, 0, 0};
  placement.unwrapped_x = 0;
  orcmap::TileScreenMap map;
  if (!orcmap::MakeTilePlacementScreenMap(vp, placement, 4096, &map)) return 2;

  int failures = 0;
  for (float width : {1.0f, 2.0f, 3.0f, 4.0f, 7.5f}) {
    for (bool vertical : {false, true}) {
      orcmap::MapStyle style = orcmap::styles::OrcSdrDark();
      auto& rule = style.rules[static_cast<size_t>(orcmap::FeatureKind::kMotorway)];
      rule.width_px = width;
      rule.color = orcmap::Color::Rgb(255, 255, 255);
      orcmap::Feature feature;
      feature.kind = orcmap::FeatureKind::kMotorway;
      feature.kind_assigned = true;
      feature.geometry.type = orcmap::GeomType::kLineString;
      feature.geometry.paths.push_back(vertical
          ? orcmap::Path{{2048, 512}, {2048, 3584}}
          : orcmap::Path{{512, 2048}, {3584, 2048}});
      orcmap::ScreenFeature screen;
      if (!orcmap::ProjectFeatureToScreen(feature, map, vp, style, &screen)) return 2;
      orcmap::host::FramebufferTarget frame(64, 64);
      orcmap::DrawScreenFeature(screen, &frame);
      int escaped = 0;
      int painted = 0;
      for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
          if (frame.At(x, y) != rule.color) continue;
          ++painted;
          if (x < screen.min_x || x > screen.max_x ||
              y < screen.min_y || y > screen.max_y) ++escaped;
        }
      }
      if (!painted || escaped) {
        std::printf("FAIL width=%.1f vertical=%d painted=%d outside_bounds=%d\n",
                    width, vertical, painted, escaped);
        ++failures;
      }
    }
  }
  std::printf("screen bounds: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}

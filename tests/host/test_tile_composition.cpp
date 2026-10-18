#include <cstdio>

#include "framebuffer_target.hpp"
#include "orcmap/renderer.hpp"

orcmap::Feature Polygon(orcmap::FeatureKind kind, int buffer) {
  orcmap::Feature f;
  f.kind = kind;
  f.kind_assigned = true;
  f.geometry.type = orcmap::GeomType::kPolygon;
  f.geometry.paths.push_back({{-buffer, -buffer}, {4096 + buffer, -buffer},
                              {4096 + buffer, 4096 + buffer},
                              {-buffer, 4096 + buffer}});
  return f;
}

int main() {
  orcmap::Viewport view;
  view.zoom = 1;
  view.width_px = 128;
  view.height_px = 64;
  view.tile_size_px = 64;
  orcmap::MapStyle style = orcmap::styles::OrcSdrDark();
  style.rules[static_cast<size_t>(orcmap::FeatureKind::kPark)].min_zoom = 0;
  const auto green = style.Rule(orcmap::FeatureKind::kPark).color;
  orcmap::FeatureTile tile;
  // Real vector tiles buffer the base polygon beyond the nominal tile, while
  // individual cover polygons can stop at its edge. A later base must not
  // erase cover already drawn in the neighboring tile.
  tile.features.push_back(Polygon(orcmap::FeatureKind::kLand, 256));
  tile.features.push_back(Polygon(orcmap::FeatureKind::kPark, 0));
  int failures = 0;
  for (int mode = 0; mode < 3; ++mode) {
    for (bool reverse : {false, true}) {
      orcmap::host::FramebufferTarget frame(128, 64);
      orcmap::ClearMapBackground(view, style, &frame);
      for (int i = 0; i < 2; ++i) {
        const int x = reverse ? 1 - i : i;
        orcmap::TilePlacement placement;
        placement.tile = {1, static_cast<uint32_t>(x), 1};
        placement.unwrapped_x = x;
        if (mode == 0) {
          if (!orcmap::RenderFeatureTileAt(tile, placement, view, style, &frame)) return 2;
        } else {
          orcmap::TileScreenMap map;
          if (!orcmap::MakeTilePlacementScreenMap(view, placement, 4096, &map)) return 2;
          orcmap::RenderScratch scratch;
          for (const auto& feature : tile.features) {
            if (mode == 1) {
              if (!orcmap::RenderFeatureAt(feature, placement, view, style, &frame, &scratch)) return 2;
            } else {
              orcmap::ScreenFeature screen;
              if (orcmap::ProjectFeatureToScreen(feature, map, view, style, &screen))
                orcmap::DrawScreenFeature(screen, &frame);
            }
          }
        }
      }
      int wrong = 0;
      for (int y = 33; y < 63; ++y)
        for (int x = 1; x < 127; ++x)
          if (frame.At(x, y) != green) ++wrong;
      if (wrong) {
        std::printf("FAIL mode=%d reverse=%d seam_pixels=%d\n", mode, reverse, wrong);
        ++failures;
      }
    }
  }
  // A map renderer may already be inside a dashboard scissor. Tile drawing
  // must intersect it and restore it, so later overlays keep their clip.
  orcmap::host::FramebufferTarget nested(128, 64);
  nested.SetClipRect({10, 35, 100, 20});
  orcmap::TilePlacement placement;
  placement.tile = {1, 0, 1};
  placement.unwrapped_x = 0;
  if (!orcmap::RenderFeatureTileAt(tile, placement, view, style, &nested)) return 2;
  const auto saved = nested.ClipRect();
  if (saved.x != 10 || saved.y != 35 || saved.width != 100 || saved.height != 20 ||
      nested.At(9, 40) == green || nested.At(10, 40) != green ||
      nested.At(63, 40) != green || nested.At(64, 40) == green) {
    std::printf("FAIL parent clip intersection/restoration\n"); ++failures;
  }
  nested.SetClipRect({0, 0, 0, 0});
  nested.FillRect(0, 0, 128, 64, green);
  if (nested.At(0, 0) == green || nested.ClipRect().width != 0) {
    std::printf("FAIL empty clip\n"); ++failures;
  }
  nested.SetClipRect({-5, -5, 15, 15});
  if (nested.ClipRect().x != 0 || nested.ClipRect().width != 10 ||
      nested.SetClipRect({0, 0, -1, 20}) || nested.ClipRect().width != 10) {
    std::printf("FAIL clipped/invalid rectangle\n"); ++failures;
  }
  std::printf("tile composition: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}

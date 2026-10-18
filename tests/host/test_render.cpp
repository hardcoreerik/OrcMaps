#include "framebuffer_target.hpp"
#include "orcmap/experimental/mvt_classify.hpp"
#include "orcmap/feature.hpp"
#include "orcmap/mvt.hpp"
#include "orcmap/mvt_translate.hpp"
#include "orcmap/renderer.hpp"
#include "orcmap/style.hpp"
#include "orcmap/viewport.hpp"

#include <fstream>
#include <initializer_list>
#include <string>
#include <vector>

#include "test_util.hpp"

namespace {

orcmap::Viewport Z0Frame(int size) {
  orcmap::Viewport v;
  v.center_lat_deg = 0.0;
  v.center_lon_deg = 0.0;
  v.zoom = 0;
  v.width_px = size;
  v.height_px = size;
  v.tile_size_px = size;
  return v;
}

orcmap::Feature MakePoly(orcmap::FeatureKind kind, bool assigned,
                         std::initializer_list<orcmap::Point> ring) {
  orcmap::Feature f;
  f.kind = kind;
  f.kind_assigned = assigned;
  f.extent = 4096;
  f.geometry.type = orcmap::GeomType::kPolygon;
  f.geometry.paths.emplace_back(ring);
  return f;
}

orcmap::Path Rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
  return {orcmap::Point{x0, y0}, orcmap::Point{x1, y0}, orcmap::Point{x1, y1},
          orcmap::Point{x0, y1}};
}

void TestClearMapBackgroundFillsOnce() {
  orcmap::host::FramebufferTarget fb(32, 32, orcmap::Color::Rgb(255, 0, 255));
  ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(Z0Frame(32),
                                                orcmap::styles::OrcSdrDark(),
                                                &fb));
  const orcmap::Color bg = orcmap::Color::Rgb(8, 10, 12);
  ORCMAP_EXPECT_TRUE(fb.At(0, 0) == bg);
  ORCMAP_EXPECT_TRUE(fb.At(16, 16) == bg);
  ORCMAP_EXPECT_TRUE(fb.At(31, 31) == bg);
}

void TestRenderFeatureTileDoesNotClear() {
  orcmap::host::FramebufferTarget fb(16, 16, orcmap::Color::Rgb(255, 0, 255));
  orcmap::FeatureTile empty;
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(
      empty, orcmap::TileId{0, 0, 0}, Z0Frame(16), orcmap::styles::OrcSdrDark(),
      &fb));
  ORCMAP_EXPECT_TRUE(fb.At(0, 0) == orcmap::Color::Rgb(255, 0, 255));
  ORCMAP_EXPECT_TRUE(fb.At(8, 8) == orcmap::Color::Rgb(255, 0, 255));
}

void TestUnclassifiedFeatureIsSkipped() {
  orcmap::FeatureTile tile;
  tile.features.push_back(MakePoly(orcmap::FeatureKind::kWater, false,
                                   {orcmap::Point{0, 0}, orcmap::Point{4096, 0},
                                    orcmap::Point{4096, 4096},
                                    orcmap::Point{0, 4096}}));
  orcmap::host::FramebufferTarget fb(32, 32);
  ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(Z0Frame(32),
                                                orcmap::styles::OrcSdrDark(),
                                                &fb));
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(
      tile, orcmap::TileId{0, 0, 0}, Z0Frame(32), orcmap::styles::OrcSdrDark(),
      &fb));
  ORCMAP_EXPECT_TRUE(fb.At(16, 16) == orcmap::Color::Rgb(8, 10, 12));
}

void TestClassifiedPolygonFillsExpectedRegion() {
  orcmap::FeatureTile tile;
  // Left half of the z0 tile.
  tile.features.push_back(MakePoly(orcmap::FeatureKind::kWater, true,
                                   {orcmap::Point{0, 0}, orcmap::Point{2048, 0},
                                    orcmap::Point{2048, 4096},
                                    orcmap::Point{0, 4096}}));
  orcmap::host::FramebufferTarget fb(32, 32);
  const orcmap::Viewport vp = Z0Frame(32);
  ORCMAP_EXPECT_TRUE(
      orcmap::ClearMapBackground(vp, orcmap::styles::OrcSdrDark(), &fb));
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(
      tile, orcmap::TileId{0, 0, 0}, vp, orcmap::styles::OrcSdrDark(), &fb));
  const orcmap::Color water =
      orcmap::ResolveFeatureStyle(orcmap::FeatureKind::kWater, 0,
                                  orcmap::styles::OrcSdrDark())
          .color;
  const orcmap::Color bg = orcmap::Color::Rgb(8, 10, 12);
  ORCMAP_EXPECT_TRUE(fb.At(4, 16) == water);
  ORCMAP_EXPECT_TRUE(fb.At(28, 16) == bg);
}

void TestDifferentStylesProduceDifferentPixels() {
  orcmap::host::FramebufferTarget dark(16, 16);
  orcmap::host::FramebufferTarget light(16, 16);
  ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(Z0Frame(16),
                                                orcmap::styles::OrcSdrDark(),
                                                &dark));
  ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(
      Z0Frame(16), orcmap::styles::StandardLight(), &light));
  ORCMAP_EXPECT_TRUE(dark.At(0, 0) != light.At(0, 0));
  ORCMAP_EXPECT_TRUE(dark.At(0, 0) == orcmap::Color::Rgb(8, 10, 12));
  ORCMAP_EXPECT_TRUE(light.At(0, 0) == orcmap::Color::Rgb(247, 247, 245));
}

void TestNullTargetIsSafe() {
  orcmap::FeatureTile tile;
  ORCMAP_EXPECT_TRUE(!orcmap::ClearMapBackground(Z0Frame(8),
                                                 orcmap::styles::OrcSdrDark(),
                                                 nullptr));
  ORCMAP_EXPECT_TRUE(!orcmap::RenderFeatureTile(
      tile, orcmap::TileId{0, 0, 0}, Z0Frame(8), orcmap::styles::OrcSdrDark(),
      nullptr));
}

void TestTwoTilesDoNotEraseEachOther() {
  const orcmap::Viewport vp = Z0Frame(32);
  orcmap::host::FramebufferTarget fb(32, 32);
  ORCMAP_EXPECT_TRUE(
      orcmap::ClearMapBackground(vp, orcmap::styles::OrcSdrDark(), &fb));

  orcmap::FeatureTile left;
  left.features.push_back(MakePoly(orcmap::FeatureKind::kWater, true,
                                   {orcmap::Point{0, 0}, orcmap::Point{2048, 0},
                                    orcmap::Point{2048, 4096},
                                    orcmap::Point{0, 4096}}));
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(
      left, orcmap::TileId{0, 0, 0}, vp, orcmap::styles::OrcSdrDark(), &fb));

  orcmap::FeatureTile right;
  right.features.push_back(MakePoly(
      orcmap::FeatureKind::kLand, true,
      {orcmap::Point{2048, 0}, orcmap::Point{4096, 0}, orcmap::Point{4096, 4096},
       orcmap::Point{2048, 4096}}));
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(
      right, orcmap::TileId{0, 0, 0}, vp, orcmap::styles::OrcSdrDark(), &fb));

  const orcmap::Color water =
      orcmap::ResolveFeatureStyle(orcmap::FeatureKind::kWater, 0,
                                  orcmap::styles::OrcSdrDark())
          .color;
  const orcmap::Color land =
      orcmap::ResolveFeatureStyle(orcmap::FeatureKind::kLand, 0,
                                  orcmap::styles::OrcSdrDark())
          .color;
  ORCMAP_EXPECT_TRUE(fb.At(4, 16) == water);
  ORCMAP_EXPECT_TRUE(fb.At(28, 16) == land);
}

void TestZoomMismatchIsRejected() {
  orcmap::host::FramebufferTarget fb(16, 16, orcmap::Color::Rgb(1, 2, 3));
  orcmap::FeatureTile tile;
  tile.features.push_back(MakePoly(orcmap::FeatureKind::kWater, true,
                                   {orcmap::Point{0, 0}, orcmap::Point{4096, 0},
                                    orcmap::Point{4096, 4096},
                                    orcmap::Point{0, 4096}}));
  ORCMAP_EXPECT_TRUE(!orcmap::RenderFeatureTile(
      tile, orcmap::TileId{1, 0, 0}, Z0Frame(16), orcmap::styles::OrcSdrDark(),
      &fb));
  ORCMAP_EXPECT_TRUE(fb.At(8, 8) == orcmap::Color::Rgb(1, 2, 3));
}

void TestInvalidViewportZoomIsRejected() {
  orcmap::Viewport v = Z0Frame(16);
  v.zoom = 32;
  orcmap::host::FramebufferTarget fb(16, 16, orcmap::Color::Rgb(9, 9, 9));
  ORCMAP_EXPECT_TRUE(
      !orcmap::ClearMapBackground(v, orcmap::styles::OrcSdrDark(), &fb));
  orcmap::FeatureTile empty;
  ORCMAP_EXPECT_TRUE(!orcmap::RenderFeatureTile(
      empty, orcmap::TileId{32, 0, 0}, v, orcmap::styles::OrcSdrDark(), &fb));
  ORCMAP_EXPECT_TRUE(fb.At(0, 0) == orcmap::Color::Rgb(9, 9, 9));
}

void TestZeroSizeTargetIsSafe() {
  orcmap::host::FramebufferTarget fb(0, 0);
  ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(Z0Frame(0),
                                                orcmap::styles::OrcSdrDark(),
                                                &fb));
  orcmap::FeatureTile empty;
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(
      empty, orcmap::TileId{0, 0, 0}, Z0Frame(0), orcmap::styles::OrcSdrDark(),
      &fb));
  ORCMAP_EXPECT_EQ(fb.Width(), 0);
  ORCMAP_EXPECT_EQ(fb.Height(), 0);
}

void TestPolygonWithTwoExteriorRingsFillsBoth() {
  // A multipolygon feature: two exterior rings with the same winding are two parts and both are filled. (Before
  // this, only the first ring was filled, which left whole ocean and land areas unpainted in world-overview tiles.)
  orcmap::Feature f;
  f.kind = orcmap::FeatureKind::kWater;
  f.kind_assigned = true;
  f.extent = 4096;
  f.geometry.type = orcmap::GeomType::kPolygon;
  f.geometry.paths.push_back(Rect(0, 0, 2048, 4096));      // left half
  f.geometry.paths.push_back(Rect(2048, 0, 4096, 4096));   // right half
  orcmap::FeatureTile tile;
  tile.features.push_back(f);
  orcmap::host::FramebufferTarget fb(32, 32);
  const orcmap::Viewport vp = Z0Frame(32);
  ORCMAP_EXPECT_TRUE(
      orcmap::ClearMapBackground(vp, orcmap::styles::OrcSdrDark(), &fb));
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(
      tile, orcmap::TileId{0, 0, 0}, vp, orcmap::styles::OrcSdrDark(), &fb));
  const orcmap::Color water =
      orcmap::ResolveFeatureStyle(orcmap::FeatureKind::kWater, 0,
                                  orcmap::styles::OrcSdrDark())
          .color;
  ORCMAP_EXPECT_TRUE(fb.At(4, 16) == water);
  ORCMAP_EXPECT_TRUE(fb.At(28, 16) == water);
}

orcmap::Path ReverseRing(orcmap::Path ring) {
  return orcmap::Path(ring.rbegin(), ring.rend());
}

void TestPolygonHoleIsSubtracted() {
  // An exterior ring with a hole of the opposite winding: the hole shows the background and the surrounding ring is
  // filled. A second, separate exterior part is still filled.
  orcmap::Feature f;
  f.kind = orcmap::FeatureKind::kWater;
  f.kind_assigned = true;
  f.extent = 4096;
  f.geometry.type = orcmap::GeomType::kPolygon;
  f.geometry.paths.push_back(Rect(0, 0, 3072, 4096));                    // exterior
  f.geometry.paths.push_back(ReverseRing(Rect(512, 1536, 2048, 2560)));  // hole inside it
  f.geometry.paths.push_back(Rect(3328, 0, 4096, 4096));                 // a second exterior part
  orcmap::FeatureTile tile;
  tile.features.push_back(f);
  orcmap::host::FramebufferTarget fb(64, 64);
  const orcmap::Viewport vp = Z0Frame(64);
  ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(vp, orcmap::styles::OrcSdrDark(), &fb));
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(tile, orcmap::TileId{0, 0, 0}, vp,
                                               orcmap::styles::OrcSdrDark(), &fb));
  const orcmap::Color water = orcmap::ResolveFeatureStyle(orcmap::FeatureKind::kWater, 0,
                                                          orcmap::styles::OrcSdrDark())
                                  .color;
  const orcmap::Color bg = orcmap::Color::Rgb(8, 10, 12);
  ORCMAP_EXPECT_TRUE(fb.At(4, 4) == water);     // exterior, away from the hole
  ORCMAP_EXPECT_TRUE(fb.At(20, 32) == bg);      // inside the hole: not filled
  ORCMAP_EXPECT_TRUE(fb.At(58, 32) == water);   // the second part
  ORCMAP_EXPECT_TRUE(fb.At(50, 32) == bg);      // the gap between the parts stays background
}

void TestPolygonWindingConventionDoesNotMatter() {
  // The exterior winding is taken from the first ring, so data with the opposite convention still renders.
  orcmap::Feature f;
  f.kind = orcmap::FeatureKind::kWater;
  f.kind_assigned = true;
  f.extent = 4096;
  f.geometry.type = orcmap::GeomType::kPolygon;
  f.geometry.paths.push_back(ReverseRing(Rect(0, 0, 4096, 4096)));
  f.geometry.paths.push_back(Rect(1024, 1024, 3072, 3072));  // opposite winding to the first: a hole
  orcmap::FeatureTile tile;
  tile.features.push_back(f);
  orcmap::host::FramebufferTarget fb(64, 64);
  const orcmap::Viewport vp = Z0Frame(64);
  ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(vp, orcmap::styles::OrcSdrDark(), &fb));
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(tile, orcmap::TileId{0, 0, 0}, vp,
                                               orcmap::styles::OrcSdrDark(), &fb));
  const orcmap::Color water = orcmap::ResolveFeatureStyle(orcmap::FeatureKind::kWater, 0,
                                                          orcmap::styles::OrcSdrDark())
                                  .color;
  const orcmap::Color bg = orcmap::Color::Rgb(8, 10, 12);
  ORCMAP_EXPECT_TRUE(fb.At(4, 4) == water);
  ORCMAP_EXPECT_TRUE(fb.At(32, 32) == bg);
}

void TestPointAndLineString(const orcmap::MapStyle& style) {
  orcmap::Feature point;
  point.kind = orcmap::FeatureKind::kLabelPrimary;
  point.kind_assigned = true;
  point.extent = 4096;
  point.geometry.type = orcmap::GeomType::kPoint;
  point.geometry.paths.push_back({orcmap::Point{2048, 2048}});

  orcmap::Feature line;
  line.kind = orcmap::FeatureKind::kMotorway;
  line.kind_assigned = true;
  line.extent = 4096;
  line.geometry.type = orcmap::GeomType::kLineString;
  line.geometry.paths.push_back(
      {orcmap::Point{0, 2048}, orcmap::Point{4096, 2048}});

  orcmap::FeatureTile tile;
  tile.features.push_back(point);
  tile.features.push_back(line);

  orcmap::host::FramebufferTarget fb(32, 32);
  const orcmap::Viewport vp = Z0Frame(32);
  ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(vp, style, &fb));
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(tile, orcmap::TileId{0, 0, 0}, vp,
                                               style, &fb));
  const orcmap::Color bg =
      orcmap::ResolveFeatureStyle(orcmap::FeatureKind::kBackground, 0, style)
          .color;
  bool changed = false;
  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      if (fb.At(x, y) != bg) changed = true;
    }
  }
  ORCMAP_EXPECT_TRUE(changed);
}

void TestTinyMvtPipelineExperimentalClassifier(const std::string& path) {
  // Semantic classification here is EXPERIMENTAL. tiny.mvt geometry is a
  // few units in a 4096 extent, so it may not cover a whole pixel; this
  // test proves the pipeline does not crash and fills a deterministic
  // background.
  std::ifstream f(path, std::ios::binary);
  const std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)),
                                   std::istreambuf_iterator<char>());
  ORCMAP_EXPECT_TRUE(!data.empty());
  orcmap::MvtTile mvt;
  ORCMAP_EXPECT_TRUE(orcmap::DecodeMvtTile(data.data(), data.size(), &mvt));
  orcmap::FeatureTile features;
  ORCMAP_EXPECT_TRUE(orcmap::TranslateMvtToFeatureTile(mvt, &features));
  ORCMAP_EXPECT_TRUE(orcmap::experimental::AssignFeatureKinds(&features));
  orcmap::host::FramebufferTarget fb(32, 32);
  const orcmap::Viewport vp = Z0Frame(32);
  ORCMAP_EXPECT_TRUE(
      orcmap::ClearMapBackground(vp, orcmap::styles::OrcSdrDark(), &fb));
  ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(
      features, orcmap::TileId{0, 0, 0}, vp, orcmap::styles::OrcSdrDark(),
      &fb));
  ORCMAP_EXPECT_TRUE(fb.At(0, 0) == orcmap::Color::Rgb(8, 10, 12));
}

void TestRenderScratchMatchesFreshScratch() {
  const orcmap::MapStyle& style = orcmap::styles::OrcSdrDark();
  orcmap::FeatureTile tile;
  tile.features.push_back(MakePoly(
      orcmap::FeatureKind::kWater, true,
      {orcmap::Point{0, 0}, orcmap::Point{2048, 0}, orcmap::Point{2048, 2048},
       orcmap::Point{0, 2048}}));
  orcmap::Feature line;
  line.kind = orcmap::FeatureKind::kMotorway;
  line.kind_assigned = true;
  line.extent = 4096;
  line.geometry.type = orcmap::GeomType::kLineString;
  line.geometry.paths.push_back({orcmap::Point{0, 1024},
                                 orcmap::Point{4096, 2048},
                                 orcmap::Point{4096, 4096}});
  tile.features.push_back(line);

  // A reused scratch must be indistinguishable from a fresh one, including
  // after the viewport changes -- the cached screen map is keyed on every
  // input it depends on.
  const orcmap::Viewport first = Z0Frame(32);
  orcmap::Viewport second = first;
  second.center_lon_deg = 2.5;

  orcmap::host::FramebufferTarget fresh(32, 32);
  orcmap::host::FramebufferTarget shared(32, 32);
  orcmap::RenderScratch scratch;

  const orcmap::Viewport* viewports[2] = {&first, &second};
  for (const orcmap::Viewport* vp : viewports) {
    ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(*vp, style, &fresh));
    ORCMAP_EXPECT_TRUE(orcmap::ClearMapBackground(*vp, style, &shared));
    orcmap::TilePlacement placement;
    placement.tile = orcmap::TileId{0, 0, 0};
    placement.unwrapped_x = 0;
    for (const orcmap::Feature& feature : tile.features) {
      orcmap::RenderScratch one_shot;
      ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureAt(
          feature, placement, *vp, style, &fresh, &one_shot));
      ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureAt(
          feature, placement, *vp, style, &shared, &scratch));
    }
  }

  for (int y = 0; y < 32; ++y) {
    for (int x = 0; x < 32; ++x) {
      ORCMAP_EXPECT_TRUE(fresh.At(x, y) == shared.At(x, y));
    }
  }
}

}  // namespace

void RunRenderTests(const std::string& mvt_fixture_path) {
  TestClearMapBackgroundFillsOnce();
  TestRenderFeatureTileDoesNotClear();
  TestUnclassifiedFeatureIsSkipped();
  TestClassifiedPolygonFillsExpectedRegion();
  TestDifferentStylesProduceDifferentPixels();
  TestNullTargetIsSafe();
  TestTwoTilesDoNotEraseEachOther();
  TestZoomMismatchIsRejected();
  TestInvalidViewportZoomIsRejected();
  TestZeroSizeTargetIsSafe();
  TestPolygonWithTwoExteriorRingsFillsBoth();
  TestPolygonHoleIsSubtracted();
  TestPolygonWindingConventionDoesNotMatter();
  TestPointAndLineString(orcmap::styles::OrcSdrDark());
  TestRenderScratchMatchesFreshScratch();
  TestTinyMvtPipelineExperimentalClassifier(mvt_fixture_path);
}

#include "framebuffer_target.hpp"
#include "orcmap/renderer.hpp"
#include "test_util.hpp"
#include <cstdlib>
#include <new>

namespace { int allocation_countdown = -1; }
void* operator new(size_t bytes) {
  if (allocation_countdown == 0) {
    allocation_countdown = -1;
    throw std::bad_alloc();
  }
  if (allocation_countdown > 0) --allocation_countdown;
  if (void* p=std::malloc(bytes ? bytes : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

namespace {
orcmap::Feature Land() {
  orcmap::Feature f;
  f.kind = orcmap::FeatureKind::kLand;
  f.kind_assigned = true;
  f.geometry.type = orcmap::GeomType::kPolygon;
  f.geometry.paths.push_back({{0,0},{4096,0},{4096,4096},{0,4096}});
  return f;
}
orcmap::Feature Line(orcmap::FeatureKind kind) {
  orcmap::Feature f;
  f.kind = kind;
  f.kind_assigned = true;
  f.geometry.type = orcmap::GeomType::kLineString;
  f.geometry.paths.push_back({{0,2048},{4096,2048}});
  return f;
}
int Count(const orcmap::host::FramebufferTarget& target, orcmap::Color color) {
  int count = 0;
  for (auto pixel : target.Pixels()) if (pixel == color) ++count;
  return count;
}
void BatchOrder() {
  orcmap::Viewport view;
  view.width_px = view.height_px = view.tile_size_px = 64;
  auto style = orcmap::styles::OrcSdrDark();
  style.rules[static_cast<size_t>(orcmap::FeatureKind::kBoundaryRegion)].min_zoom = 0;
  for (auto kind : {orcmap::FeatureKind::kBoundaryRegion, orcmap::FeatureKind::kMotorway}) {
    auto fill = Land();
    if (kind == orcmap::FeatureKind::kMotorway) fill.kind = orcmap::FeatureKind::kWater;
    orcmap::FeatureTile tile;
    tile.features = {Line(kind), fill};
    orcmap::host::FramebufferTarget frame(64,64);
    ORCMAP_EXPECT_TRUE(orcmap::RenderFeatureTile(tile, {0,0,0}, view, style, &frame));
    ORCMAP_EXPECT_TRUE(Count(frame, style.Rule(kind).color) > 0);
  }
}

void StreamParity() {
  using namespace orcmap;
  Viewport view;
  view.width_px=192; view.height_px=view.tile_size_px=64;
  auto style=styles::OrcSdrDark();
  style.rules[static_cast<size_t>(FeatureKind::kBoundaryRegion)].min_zoom=0;
  // Mixed extents, two strokes in source order, and a point under a later fill.
  auto road=Line(FeatureKind::kMotorway);
  road.extent=8192;
  road.geometry.paths={{{0,4096},{8192,4096}}};
  auto point=Line(FeatureKind::kMotorway);
  point.geometry.type=GeomType::kPoint;
  point.geometry.paths={{{1024,1024}}};
  FeatureTile tile;
  tile.features={road,point,Land(),Line(FeatureKind::kBoundaryRegion)};
  host::FramebufferTarget batch(192,64),stream(192,64);
  batch.SetClipRect({10,5,160,45});
  stream.SetClipRect({10,5,160,45});
  OrderedRenderScratch queue;
  for (int x=-1; x<=1; ++x) {
    TilePlacement placement{{0,0,0},x};
    ORCMAP_EXPECT_TRUE(RenderFeatureTileAt(tile,placement,view,style,&batch));
  }
  for (const auto& f:tile.features) {
    for (int x=-1; x<=1; ++x) {
      TilePlacement placement{{0,0,0},x};
      ORCMAP_EXPECT_TRUE(RenderOrderedFeatureAt(f,placement,view,style,&stream,&queue));
    }
  }
  ORCMAP_EXPECT_TRUE(FinishOrderedTile(&queue,&stream));
  ORCMAP_EXPECT_TRUE(batch.Pixels()==stream.Pixels());
  ORCMAP_EXPECT_TRUE(Count(stream,style.Rule(FeatureKind::kBoundaryRegion).color)>0);
  ORCMAP_EXPECT_TRUE(Count(stream,style.Rule(FeatureKind::kMotorway).color)>0);
  ORCMAP_EXPECT_TRUE(queue.paths.empty() && queue.xy.empty());
  ORCMAP_EXPECT_TRUE(queue.peak_bytes>0 && queue.RetainedBytes()>=queue.peak_bytes);
  const auto clip=stream.ClipRect();
  ORCMAP_EXPECT_TRUE(clip.x==10 && clip.y==5 && clip.width==160 && clip.height==45);
  const auto retained=queue.RetainedBytes();
  queue.render.poly_xy.reserve(100000);
  queue.Reset();
  ORCMAP_EXPECT_TRUE(queue.RetainedBytes()==retained && queue.peak_bytes==0);
  ORCMAP_EXPECT_TRUE(queue.render.poly_xy.capacity()==0);
}

void LimitsAndCancellation() {
  using namespace orcmap;
  Viewport view;
  view.width_px=view.height_px=view.tile_size_px=64;
  const auto& style=styles::OrcSdrDark();
  TilePlacement placement{{0,0,0},0};
  host::FramebufferTarget frame(64,64);
  OrderedRenderScratch queue;
  auto road=Line(FeatureKind::kMotorway);
  ORCMAP_EXPECT_TRUE(RenderOrderedFeatureAt(road,placement,view,style,&frame,&queue));
  const auto previous_points=queue.xy.size(), previous_paths=queue.paths.size();
  auto too_many=road;
  too_many.geometry.paths[0].resize(OrderedRenderScratch::kMaxPoints);
  ORCMAP_EXPECT_TRUE(!RenderOrderedFeatureAt(too_many,placement,view,style,&frame,&queue));
  ORCMAP_EXPECT_TRUE(queue.status==OrderedRenderStatus::kCapacityExceeded);
  ORCMAP_EXPECT_TRUE(queue.xy.size()==previous_points && queue.paths.size()==previous_paths);
  ORCMAP_EXPECT_TRUE(!FinishOrderedTile(&queue,&frame));
  ORCMAP_EXPECT_TRUE(queue.xy.empty() && queue.paths.empty());
  ORCMAP_EXPECT_TRUE(Count(frame,style.Rule(FeatureKind::kMotorway).color)==0);
  queue.Reset();
  too_many=road;
  too_many.geometry.paths.resize(OrderedRenderScratch::kMaxPaths+1,road.geometry.paths[0]);
  ORCMAP_EXPECT_TRUE(!RenderOrderedFeatureAt(too_many,placement,view,style,&frame,&queue));
  ORCMAP_EXPECT_TRUE(queue.status==OrderedRenderStatus::kCapacityExceeded);
  ORCMAP_EXPECT_TRUE(queue.xy.empty() && queue.paths.empty());
  queue.Reset();
  road.geometry.paths[0].clear();
  for(int x=0;x<514;++x) road.geometry.paths[0].push_back({x*8,2048});
  frame.SetClipRect({5,5,50,50});
  ORCMAP_EXPECT_TRUE(RenderOrderedFeatureAt(road,placement,view,style,&frame,&queue));
  int calls=0;
  ORCMAP_EXPECT_TRUE(!FinishOrderedTile(&queue,&frame,[](void* p) {
    return ++*static_cast<int*>(p)<3;
  },&calls));
  ORCMAP_EXPECT_TRUE(calls==3 && queue.status==OrderedRenderStatus::kCancelled);
  ORCMAP_EXPECT_TRUE(queue.xy.empty() && queue.paths.empty());
  const auto clip=frame.ClipRect();
  ORCMAP_EXPECT_TRUE(clip.x==5 && clip.y==5 && clip.width==50 && clip.height==50);
  queue.Reset();
  ORCMAP_EXPECT_TRUE(RenderOrderedFeatureAt(road,placement,view,style,&frame,&queue));
  ORCMAP_EXPECT_TRUE(FinishOrderedTile(&queue,&frame));
}

void SkipAndInvalidInput() {
  using namespace orcmap;
  Viewport view;
  view.width_px=view.height_px=view.tile_size_px=64;
  const auto& style=styles::OrcSdrDark();
  TilePlacement placement{{0,0,0},0};
  host::FramebufferTarget frame(64,64);
  OrderedRenderScratch queue;
  auto f=Line(FeatureKind::kMotorway);
  f.kind_assigned=false;
  ORCMAP_EXPECT_TRUE(RenderOrderedFeatureAt(f,placement,view,style,&frame,&queue));
  f.kind_assigned=true; f.kind=FeatureKind::kLabelPrimary;
  ORCMAP_EXPECT_TRUE(RenderOrderedFeatureAt(f,placement,view,style,&frame,&queue));
  f.kind=FeatureKind::kMotorway;
  f.geometry.paths={{{1,1}}, {}}; // A degenerate line never queues.
  ORCMAP_EXPECT_TRUE(RenderOrderedFeatureAt(f,placement,view,style,&frame,&queue));
  ORCMAP_EXPECT_TRUE(queue.paths.empty() && queue.xy.empty());
  ORCMAP_EXPECT_TRUE(!RenderOrderedFeatureAt(f,placement,view,style,nullptr,&queue));
  ORCMAP_EXPECT_TRUE(queue.status==OrderedRenderStatus::kInvalidInput);
  ORCMAP_EXPECT_TRUE(!FinishOrderedTile(&queue,&frame));
  queue.Reset();
  ORCMAP_EXPECT_TRUE(FinishOrderedTile(&queue,&frame));
}

void AllocationFailure() {
  using namespace orcmap;
  Viewport view;
  view.width_px=view.height_px=view.tile_size_px=64;
  const auto& style=styles::OrcSdrDark();
  const auto road=Line(FeatureKind::kMotorway);
  for (int fail_at : {0,1}) { // Coordinate allocation, then descriptor allocation.
    OrderedRenderScratch queue;
    host::FramebufferTarget frame(64,64);
    bool threw=false;
    allocation_countdown=fail_at;
    try { RenderOrderedFeatureAt(road,{{0,0,0},0},view,style,&frame,&queue); }
    catch (const std::bad_alloc&) { threw=true; }
    allocation_countdown=-1;
    ORCMAP_EXPECT_TRUE(threw && queue.status==OrderedRenderStatus::kInvalidInput);
    ORCMAP_EXPECT_TRUE(!FinishOrderedTile(&queue,&frame));
    ORCMAP_EXPECT_TRUE(queue.xy.empty() && queue.paths.empty());
    queue.Reset();
    ORCMAP_EXPECT_TRUE(RenderOrderedFeatureAt(road,{{0,0,0},0},view,style,&frame,&queue));
    ORCMAP_EXPECT_TRUE(FinishOrderedTile(&queue,&frame));
    ORCMAP_EXPECT_TRUE(Count(frame,style.Rule(FeatureKind::kMotorway).color)>0);
  }
}
}
int main() {
  BatchOrder();
  StreamParity();
  LimitsAndCancellation();
  SkipAndInvalidInput();
  AllocationFailure();
  std::printf("paint order: %s\n", orcmap::test::g_failures ? "FAIL" : "PASS");
  return orcmap::test::g_failures ? 1 : 0;
}

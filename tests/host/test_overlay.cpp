// Standalone public-API test. Built against a minimal API-compatible local
// harness here; tests/host should also compile this against the real core.
#include "orcmap/overlay.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include "test_util.hpp"
#include <limits>
#include <vector>
using namespace orcmap;

struct CaptureTarget : RenderTarget {
  int w=320,h=240; int fills=0, lines=0, polygons=0, rings=0, points=0;
  int first_x=0,first_y=0; double max_len=0;
  std::vector<int> xy; std::vector<size_t> counts;
  std::vector<std::array<int,4>> segments;
  int Width()const override {return w;} int Height()const override {return h;}
  void FillRect(int,int,int,int,Color) override {++fills;}
  void DrawPoint(int,int,Color) override {++points;}
  void DrawLine(int x0,int y0,int x1,int y1,Color,float) override {
    ++lines; segments.push_back({x0,y0,x1,y1});
    max_len=std::max(max_len,std::hypot(double(x1)-x0,double(y1)-y0));
  }
  void FillPolygon(const int* p,size_t n,Color) override {
    ++polygons;xy.assign(p,p+2*n);counts={n};
    if(n){first_x=p[0];first_y=p[1];}
  }
  void FillPolygonRings(const int* p,const size_t* ns,size_t n,Color) override {
    ++polygons;rings=int(n);counts.assign(ns,ns+n);
    size_t total=0;for(size_t i=0;i<n;i++)total+=ns[i];
    xy.assign(p,p+2*total);
  }
};

OverlayObject Marker(uint64_t id,double lat=0,double lon=0){
 OverlayObject o;o.id=id;o.position={lat,lon};return o;
}
Viewport V(int zoom=5,double lon=0){Viewport v;v.zoom=zoom;v.width_px=320;v.height_px=240;v.tile_size_px=256;v.center_lon_deg=lon;return v;}

void TestSceneBasics(){
 OverlayScene s({2,2,5});ORCMAP_EXPECT_TRUE(s.AddLayer(1));ORCMAP_EXPECT_TRUE(s.AddLayer(2));ORCMAP_EXPECT_TRUE(!s.AddLayer(3));
 ORCMAP_EXPECT_TRUE(!s.AddLayer(1));ORCMAP_EXPECT_TRUE(s.Upsert(1,Marker(0)));ORCMAP_EXPECT_TRUE(s.Upsert(2,Marker(0)));
 ORCMAP_EXPECT_TRUE(s.ObjectCount()==2);ORCMAP_EXPECT_TRUE(!s.Upsert(2,Marker(1)));
 ORCMAP_EXPECT_TRUE(s.SetLayerVisible(2,false));ORCMAP_EXPECT_TRUE(!s.SetLayerVisible(3,true));
 ORCMAP_EXPECT_TRUE(s.Dirty());auto rev=s.Revision();s.AcknowledgeRedraw();ORCMAP_EXPECT_TRUE(!s.Dirty());
 ORCMAP_EXPECT_TRUE(s.Upsert(1,Marker(0,1)));ORCMAP_EXPECT_TRUE(s.ObjectCount()==2&&s.Revision()>rev);
 ORCMAP_EXPECT_TRUE(s.Remove(2,0));ORCMAP_EXPECT_TRUE(s.RemoveLayer(2));ORCMAP_EXPECT_TRUE(s.ClearLayer(1));
 ORCMAP_EXPECT_TRUE(s.ObjectCount()==0);ORCMAP_EXPECT_TRUE(!s.Remove(1,0));
}
void TestInvalidAndLimits(){
 OverlayScene s({1,4,4});ORCMAP_EXPECT_TRUE(s.AddLayer(1));
 auto m=Marker(1,90);ORCMAP_EXPECT_TRUE(!s.Upsert(1,m));
 m=Marker(1);m.bearing_deg=std::numeric_limits<double>::quiet_NaN();ORCMAP_EXPECT_TRUE(!s.Upsert(1,m));
 m=Marker(1);m.position.lon_deg=181;ORCMAP_EXPECT_TRUE(!s.Upsert(1,m));
 m=Marker(1);m.ring_sizes={3};ORCMAP_EXPECT_TRUE(!s.Upsert(1,m));
 m.ring_sizes.clear();ORCMAP_EXPECT_TRUE(s.Upsert(1,m));
 OverlayObject p;p.id=2;p.kind=OverlayKind::kPolyline;p.vertices={{0,0},{0,1},{1,1},{1,0}};
 ORCMAP_EXPECT_TRUE(s.Upsert(1,p));ORCMAP_EXPECT_TRUE(s.VertexCount()==4);
 p.vertices.push_back({2,2});ORCMAP_EXPECT_TRUE(!s.Upsert(1,p));ORCMAP_EXPECT_TRUE(s.VertexCount()==4);
 p.vertices[0]={999,1};ORCMAP_EXPECT_TRUE(!s.Upsert(1,p));
 OverlayObject a;a.id=3;a.kind=OverlayKind::kPolygon;a.vertices={{0,0},{1,0},{1,1}};
 a.ring_sizes={4};ORCMAP_EXPECT_TRUE(!s.Upsert(1,a));a.ring_sizes={3};ORCMAP_EXPECT_TRUE(!s.Upsert(1,a));
 a.id=2;a.ring_sizes={3};ORCMAP_EXPECT_TRUE(s.Upsert(1,a));ORCMAP_EXPECT_TRUE(s.VertexCount()==3);
 OverlayObject c;c.kind=OverlayKind::kCircle;c.radius_m=-1;ORCMAP_EXPECT_TRUE(!s.Upsert(1,c));
 c.radius_m=1000;c.ring_sizes={3};ORCMAP_EXPECT_TRUE(!s.Upsert(1,c));
}
void TestMarkersAndSelection(){
 OverlayScene s;OverlayScratch scratch;CaptureTarget target;
 ORCMAP_EXPECT_TRUE(s.AddLayer(5,-1));ORCMAP_EXPECT_TRUE(s.AddLayer(9,10));
 auto a=Marker(88);a.style.marker_shape=MarkerShape::kArrow;a.bearing_deg=90;
 ORCMAP_EXPECT_TRUE(s.Upsert(5,a));ORCMAP_EXPECT_TRUE(s.Upsert(9,Marker(99)));
 ORCMAP_EXPECT_TRUE(RenderOverlays(s,V(),&target,&scratch));ORCMAP_EXPECT_TRUE(target.polygons>0);
 ORCMAP_EXPECT_TRUE(target.fills==0); // no clear of existing basemap
 OverlayHit h;ORCMAP_EXPECT_TRUE(HitTestOverlays(s,V(),160,120,&h,&scratch));
 ORCMAP_EXPECT_TRUE(h.layer_id==9&&h.object_id==99);
 ORCMAP_EXPECT_TRUE(s.SetLayerVisible(9,false));ORCMAP_EXPECT_TRUE(HitTestOverlays(s,V(),160,120,&h,&scratch));
 ORCMAP_EXPECT_TRUE(h.layer_id==5&&h.object_id==88);
 ORCMAP_EXPECT_TRUE(s.SetLayerOrder(5,50));ORCMAP_EXPECT_TRUE(s.SetLayerVisible(9,true));
 ORCMAP_EXPECT_TRUE(HitTestOverlays(s,V(),160,120,&h,&scratch));ORCMAP_EXPECT_TRUE(h.object_id==88);
 ORCMAP_EXPECT_TRUE(!HitTestOverlays(s,V(),-1,120,&h,&scratch));
 ORCMAP_EXPECT_TRUE(!HitTestOverlays(s,V(),200,200,&h,&scratch));
 a.style.marker_shape=MarkerShape::kTriangle;a.selected=true;
 ORCMAP_EXPECT_TRUE(s.Upsert(5,a));ORCMAP_EXPECT_TRUE(RenderOverlays(s,V(),&target,&scratch));
 ORCMAP_EXPECT_TRUE(target.lines>0);
}
void TestPolylineAndAntimeridian(){
 OverlayScene s;OverlayScratch scratch;CaptureTarget t;
 ORCMAP_EXPECT_TRUE(s.AddLayer(1));OverlayObject p;p.id=1;p.kind=OverlayKind::kPolyline;
 p.vertices={{0,179.6},{0,-179.6}};p.style.stroke_width_px=2;
 ORCMAP_EXPECT_TRUE(s.Upsert(1,p));ORCMAP_EXPECT_TRUE(RenderOverlays(s,V(6,179.8),&t,&scratch));
 ORCMAP_EXPECT_TRUE(t.lines>=1&&t.max_len<100);
 OverlayHit h;ORCMAP_EXPECT_TRUE(HitTestOverlays(s,V(6,179.8),160,120,&h,&scratch,8));
 ORCMAP_EXPECT_TRUE(h.object_id==1);
 auto off=V(6,0);CaptureTarget out;
 ORCMAP_EXPECT_TRUE(RenderOverlays(s,off,&out,&scratch));ORCMAP_EXPECT_TRUE(out.lines==0);
}
void TestPolygonsAndHoles(){
 OverlayScene s;OverlayScratch scratch;CaptureTarget t;ORCMAP_EXPECT_TRUE(s.AddLayer(5));
 OverlayObject p;p.id=45;p.kind=OverlayKind::kPolygon;
 // Outer ring surrounds the viewport center, hole excludes its center.
 p.vertices={{-2,-2},{-2,2},{2,2},{2,-2},
             {-0.5,-0.5},{-0.5,0.5},{0.5,0.5},{0.5,-0.5}};
 p.ring_sizes={4,4};ORCMAP_EXPECT_TRUE(s.Upsert(5,p));
 ORCMAP_EXPECT_TRUE(RenderOverlays(s,V(),&t,&scratch));ORCMAP_EXPECT_TRUE(t.rings==2);
 OverlayHit h;ORCMAP_EXPECT_TRUE(!HitTestOverlays(s,V(),160,120,&h,&scratch,0));
 double sx,sy;ORCMAP_EXPECT_TRUE(ProjectLatLon(V(),{1.5,0},&sx,&sy));
 ORCMAP_EXPECT_TRUE(HitTestOverlays(s,V(),sx,sy,&h,&scratch,0));ORCMAP_EXPECT_TRUE(h.object_id==45);
 ORCMAP_EXPECT_TRUE(s.Remove(5,45));ORCMAP_EXPECT_TRUE(!HitTestOverlays(s,V(),sx,sy,&h,&scratch,0));
}
void TestPanZoomAndWorldCopies(){
 OverlayScene s; OverlayScratch sc; CaptureTarget t; ORCMAP_EXPECT_TRUE(s.AddLayer(7));
 auto m=Marker(321,0,1);ORCMAP_EXPECT_TRUE(s.Upsert(7,m));
 auto v=V(4);double x4,y4;ORCMAP_EXPECT_TRUE(ProjectLatLon(v,m.position,&x4,&y4));
 OverlayHit h;ORCMAP_EXPECT_TRUE(HitTestOverlays(s,v,x4,y4,&h,&sc,1));
 v.zoom=5;double x5,y5;ORCMAP_EXPECT_TRUE(ProjectLatLon(v,m.position,&x5,&y5));
 ORCMAP_EXPECT_TRUE(std::abs(x5-160)>std::abs(x4-160));
 ORCMAP_EXPECT_TRUE(HitTestOverlays(s,v,x5,y5,&h,&sc,1));
 v.center_lon_deg=1;ORCMAP_EXPECT_TRUE(HitTestOverlays(s,v,160,120,&h,&sc,1));
 v.zoom=0;v.tile_size_px=64;v.width_px=320;v.height_px=240;
 CaptureTarget repeats;ORCMAP_EXPECT_TRUE(RenderOverlays(s,v,&repeats,&sc));
 ORCMAP_EXPECT_TRUE(repeats.polygons>=3); // Basemap repeats at zoom zero.
}
void TestGeographyAndRedraw(){
 OverlayScene s;OverlayScratch scratch;CaptureTarget t;ORCMAP_EXPECT_TRUE(s.AddLayer(1));
 OverlayObject c;c.id=13;c.kind=OverlayKind::kCircle;c.radius_m=10000;
 ORCMAP_EXPECT_TRUE(s.Upsert(1,c));ORCMAP_EXPECT_TRUE(RenderOverlays(s,V(10),&t,&scratch));
 ORCMAP_EXPECT_TRUE(t.polygons>=1);OverlayHit hit;
 ORCMAP_EXPECT_TRUE(HitTestOverlays(s,V(10),160,120,&hit,&scratch));
 c.kind=OverlayKind::kSector;c.start_bearing_deg=0;c.sweep_deg=90;
 ORCMAP_EXPECT_TRUE(s.Upsert(1,c));ORCMAP_EXPECT_TRUE(RenderOverlays(s,V(10),&t,&scratch));
 c.kind=OverlayKind::kVector;c.bearing_deg=90;ORCMAP_EXPECT_TRUE(s.Upsert(1,c));
 const int before=t.lines;ORCMAP_EXPECT_TRUE(RenderOverlays(s,V(10),&t,&scratch));ORCMAP_EXPECT_TRUE(t.lines>before);
 // Repeated redraw: bounded scratch vectors, stable capacity after warmup.
 auto cap=scratch.path.capacity(),cap2=scratch.clip_a.capacity(),cap3=scratch.polygon_xy.capacity();
 for(int i=0;i<1000;++i)ORCMAP_EXPECT_TRUE(RenderOverlays(s,V(10),&t,&scratch));
 ORCMAP_EXPECT_TRUE(cap==scratch.path.capacity()&&cap2==scratch.clip_a.capacity()&&cap3==scratch.polygon_xy.capacity());
}
void TestRepeatedWorldPathsStayShort(){
 OverlayScene s;OverlayScratch scratch;CaptureTarget target;
 ORCMAP_EXPECT_TRUE(s.AddLayer(1));
 OverlayObject p;p.id=8;p.kind=OverlayKind::kPolyline;
 p.vertices={{0,0},{0,4},{0,8}};
 ORCMAP_EXPECT_TRUE(s.Upsert(1,p));
 auto v=V(0);v.tile_size_px=64;v.width_px=320;
 ORCMAP_EXPECT_TRUE(RenderOverlays(s,v,&target,&scratch));
 ORCMAP_EXPECT_TRUE(target.lines>2);
 ORCMAP_EXPECT_TRUE(target.max_len < 8);
 // The copies must remain individually touch-selectable.
 OverlayHit hit;
 ORCMAP_EXPECT_TRUE(HitTestOverlays(s,v,96,120,&hit,&scratch,6));
 ORCMAP_EXPECT_TRUE(hit.object_id==8u);
}
void TestMarkerFillStaysInViewport(){
 OverlayScene s;OverlayScratch scratch;CaptureTarget target;
 ORCMAP_EXPECT_TRUE(s.AddLayer(1));
 auto m=Marker(1,0,-7); // At the west edge of the viewport at zoom 5.
 m.style.marker_size_px=30;m.style.outline=false;
 ORCMAP_EXPECT_TRUE(s.Upsert(1,m));
 ORCMAP_EXPECT_TRUE(RenderOverlays(s,V(),&target,&scratch));
 ORCMAP_EXPECT_TRUE(target.polygons>0);
 for (size_t i=0;i+1<target.xy.size();i+=2) {
  ORCMAP_EXPECT_TRUE(target.xy[i]>=0 && target.xy[i]<target.w);
  ORCMAP_EXPECT_TRUE(target.xy[i+1]>=0 && target.xy[i+1]<target.h);
 }
}
void TestClippingRequiresScissor(){
 OverlayScene s;OverlayScratch scratch;CaptureTarget target;
 ORCMAP_EXPECT_TRUE(s.AddLayer(1));
 ORCMAP_EXPECT_TRUE(s.Upsert(1,Marker(1)));
 auto v=V();v.width_px=200; // Target is 320 wide, no SetClipRect support.
 ORCMAP_EXPECT_TRUE(!RenderOverlays(s,v,&target,&scratch));
 ORCMAP_EXPECT_TRUE(target.polygons==0);
}
// A complete sector must not contain or hit a radial center-to-arc spoke.
void TestFullSectorHasNoRadialSpokes(){
 OverlayScene scene; OverlayScratch scratch; CaptureTarget target;
 ORCMAP_EXPECT_TRUE(scene.AddLayer(1));
 OverlayObject arc;arc.id=71;arc.kind=OverlayKind::kSector;
 arc.position={0,0};arc.radius_m=1000;arc.sweep_deg=360;
 arc.style.fill=false;arc.style.outline=true;
 ORCMAP_EXPECT_TRUE(scene.Upsert(1,arc));
 const auto v=V(12);
 ORCMAP_EXPECT_TRUE(RenderOverlays(scene,v,&target,&scratch));
 ORCMAP_EXPECT_EQ(target.lines,int(kOverlayArcSegments));
 ORCMAP_EXPECT_EQ(scratch.generated.size(),kOverlayArcSegments);
 OverlayHit hit;
 ORCMAP_EXPECT_TRUE(!HitTestOverlays(scene,v,160,120,&hit,&scratch,0));
 ORCMAP_EXPECT_TRUE(HitTestOverlays(scene,v,186,120,&hit,&scratch,3));
}
// Normalize the start before adding sub-degree steps at huge finite bearings.
void TestSectorHugeBearingPreservesSweep(){
 OverlayScene scene; OverlayScratch scratch; CaptureTarget target;
 ORCMAP_EXPECT_TRUE(scene.AddLayer(1));
 OverlayObject arc;arc.id=72;arc.kind=OverlayKind::kSector;
 arc.position={0,0};arc.radius_m=10000;
 arc.start_bearing_deg=1e18;arc.sweep_deg=0.5;
 ORCMAP_EXPECT_TRUE(scene.Upsert(1,arc));
 ORCMAP_EXPECT_TRUE(RenderOverlays(scene,V(12),&target,&scratch));
 ORCMAP_EXPECT_EQ(scratch.generated.size(),kOverlayArcSegments+2);
 ORCMAP_EXPECT_TRUE(std::abs(scratch.generated[1].lat_deg -
                             scratch.generated.back().lat_deg)>1e-5 ||
                    std::abs(scratch.generated[1].lon_deg -
                             scratch.generated.back().lon_deg)>1e-5);
}
// Rotated square corners and thick outlines must render and be selectable.
void TestRotatedMarkerCullingAndOutlinePick(){
 OverlayScene scene; OverlayScratch scratch; CaptureTarget target;
 ORCMAP_EXPECT_TRUE(scene.AddLayer(1));
 auto marker=Marker(73,0,-8.15);
 marker.style.marker_shape=MarkerShape::kSquare;
 marker.style.marker_size_px=40;marker.bearing_deg=45;
 ORCMAP_EXPECT_TRUE(scene.Upsert(1,marker));
 ORCMAP_EXPECT_TRUE(RenderOverlays(scene,V(),&target,&scratch));
 ORCMAP_EXPECT_TRUE(target.polygons>0); // Its center is outside x=0.
 marker.position={0,0}; marker.bearing_deg=0;
 marker.style.stroke_width_px=8;
 ORCMAP_EXPECT_TRUE(scene.Upsert(1,marker));
 OverlayHit hit;
 ORCMAP_EXPECT_TRUE(HitTestOverlays(scene,V(),184,144,&hit,&scratch,0));
 ORCMAP_EXPECT_EQ(hit.object_id,73u);
}
// A large marker can overlap the edge from the next world copy.
void TestWorldCopyTraversesOverlappingExtent(){
 OverlayScene scene; OverlayScratch scratch; CaptureTarget target;
 ORCMAP_EXPECT_TRUE(scene.AddLayer(1));
 auto marker=Marker(74,0,180);
 marker.style.marker_size_px=140;
 ORCMAP_EXPECT_TRUE(scene.Upsert(1,marker));
 auto v=V(0);v.tile_size_px=64;v.width_px=240;target.w=240;
 ORCMAP_EXPECT_TRUE(RenderOverlays(scene,v,&target,&scratch));
 ORCMAP_EXPECT_TRUE(target.polygons>=6);
}
// High-zoom projected coordinates must not saturate before clipping.
void TestHighZoomPolylinePreservesSlope(){
 OverlayScene scene; OverlayScratch scratch; CaptureTarget target;
 ORCMAP_EXPECT_TRUE(scene.AddLayer(1));
 OverlayObject path;path.kind=OverlayKind::kPolyline;path.id=75;
 path.vertices={{0,0},{1,10}};
 ORCMAP_EXPECT_TRUE(scene.Upsert(1,path));
 auto v=V(20,5);v.center_lat_deg=0.5;
 ORCMAP_EXPECT_TRUE(RenderOverlays(scene,v,&target,&scratch));
 ORCMAP_EXPECT_TRUE(target.lines>=1);
 if(!target.segments.empty()){
  const auto edge=target.segments.front();
  ORCMAP_EXPECT_TRUE(std::abs(edge[2]-edge[0])>=300);
  ORCMAP_EXPECT_TRUE(std::abs(edge[3]-edge[1])>=10);
  ORCMAP_EXPECT_TRUE(std::abs(edge[3]-edge[1])<80);
 }
}
// Polygon outlines must obey the same projected-double clipping as paths.
void TestHighZoomPolygonOutlinePreservesSlope(){
 OverlayScene scene; OverlayScratch scratch; CaptureTarget target;
 ORCMAP_EXPECT_TRUE(scene.AddLayer(1));
 OverlayObject area;area.kind=OverlayKind::kPolygon;area.id=76;
 area.vertices={{0,0},{1,10},{5,12}};area.ring_sizes={3};
 area.style.fill=false;area.style.outline=true;
 ORCMAP_EXPECT_TRUE(scene.Upsert(1,area));
 auto v=V(20,5);v.center_lat_deg=0.5;
 ORCMAP_EXPECT_TRUE(RenderOverlays(scene,v,&target,&scratch));
 ORCMAP_EXPECT_TRUE(target.lines>=1);
 if(!target.segments.empty()){
  const auto edge=target.segments.front();
  ORCMAP_EXPECT_TRUE(std::abs(edge[2]-edge[0])>=300);
  ORCMAP_EXPECT_TRUE(std::abs(edge[3]-edge[1])<80);
 }
}
// Repeated clipping of dense concave rings must stay inside warm scratch capacity.
void TestConcavePolygonWarmScratch(){
 OverlayScene scene;OverlayScratch scratch;CaptureTarget target;
 ORCMAP_EXPECT_TRUE(scene.AddLayer(1));
 OverlayObject area;area.kind=OverlayKind::kPolygon;area.id=77;
 for(int i=0;i<128;i++){
  const double lat=4.0-8.0*i/127.0;
  area.vertices.push_back({lat,(i%2==0)?-10.0:0.0});
 }
 for(int i=127;i>=0;i--)area.vertices.push_back({4.0-8.0*i/127.0,10.0});
 area.ring_sizes={256};
 ORCMAP_EXPECT_TRUE(scene.Upsert(1,area));
 ORCMAP_EXPECT_TRUE(scratch.clip_a.capacity()>=381);
 ORCMAP_EXPECT_TRUE(scratch.clip_b.capacity()>=381);
 ORCMAP_EXPECT_TRUE(scratch.polygon_xy.capacity()>=2*381);
 const auto ca=scratch.clip_a.capacity(), cb=scratch.clip_b.capacity();
 const auto cx=scratch.polygon_xy.capacity();
 for(int i=0;i<12;i++){
  auto v=V(5,0.25*i);
  ORCMAP_EXPECT_TRUE(RenderOverlays(scene,v,&target,&scratch));
  ORCMAP_EXPECT_EQ(scratch.clip_a.capacity(),ca);
  ORCMAP_EXPECT_EQ(scratch.clip_b.capacity(),cb);
  ORCMAP_EXPECT_EQ(scratch.polygon_xy.capacity(),cx);
 }
}

void RunOverlayTests(){
 TestSceneBasics();TestInvalidAndLimits();TestMarkersAndSelection();
 TestRepeatedWorldPathsStayShort();TestClippingRequiresScissor();TestMarkerFillStaysInViewport();
 TestPolylineAndAntimeridian();TestPolygonsAndHoles();TestPanZoomAndWorldCopies();TestGeographyAndRedraw();
 TestFullSectorHasNoRadialSpokes();TestSectorHugeBearingPreservesSweep();
 TestRotatedMarkerCullingAndOutlinePick();TestWorldCopyTraversesOverlappingExtent();
 TestHighZoomPolylinePreservesSlope();TestHighZoomPolygonOutlinePreservesSlope();
 TestConcavePolygonWarmScratch();
 puts("PASS: OrcMaps generic overlay tests complete");
}

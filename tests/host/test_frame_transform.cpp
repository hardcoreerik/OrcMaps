#include "orcmap/frame_transform.hpp"
#include <cassert>
#include <cmath>
#include <limits>
int main() {
  using namespace orcmap;
  Viewport from;from.width_px=1248;from.height_px=568;from.zoom=8;
  from.center_lat_deg=44.05;from.center_lon_deg=-123.02;
  FrameTransform result;
  assert(MakeFrameTransform(from,from,&result));
  assert(result.scale==1&&std::abs(result.dx)<1e-9&&std::abs(result.dy)<1e-9);
  auto to=from;assert(PanByPixels(&to,45,20));
  assert(MakeFrameTransform(from,to,&result));
  assert(std::abs(result.dx+45)<1e-7&&std::abs(result.dy+20)<1e-7);
  for(int zoom:{7,8,9}) {
    to.zoom=zoom;
    assert(MakeFrameTransform(from,to,&result));
    for(auto point:{LatLon{44.1,-123.1},LatLon{43.9,-122.9}}) {
      double x,y,nx,ny;
      assert(ProjectLatLon(from,point,&x,&y)&&ProjectLatLon(to,point,&nx,&ny));
      assert(std::abs(x*result.scale+result.dx-nx)<1e-6);
      assert(std::abs(y*result.scale+result.dy-ny)<1e-6);
    }
  }
  from.center_lon_deg=179.99;to=from;to.center_lon_deg=-179.99;
  assert(MakeFrameTransform(from,to,&result)&&std::abs(result.dx)<10);
  const auto saved=result;
  to.width_px=0;assert(!MakeFrameTransform(from,to,&result)&&result.dx==saved.dx);
  to=from;to.center_lat_deg=std::numeric_limits<double>::quiet_NaN();
  assert(!MakeFrameTransform(from,to,&result));
  assert(!MakeFrameTransform(from,from,nullptr));
}

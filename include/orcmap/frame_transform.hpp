#pragma once
#include <cmath>
#include "orcmap/viewport.hpp"

namespace orcmap {
// Reproject an immutable completed Mercator frame into a newer camera.
// Destination pixels = source pixels * scale + offset. Clip to destination;
// uncovered pixels need background/new tiles. This does not supply missing data.
struct FrameTransform {double scale=1,dx=0,dy=0;};
inline bool MakeFrameTransform(const Viewport& source,const Viewport& destination,
                                FrameTransform* output) {
  if(!output)return false;
  double sx,sy,dx,dy;
  const LatLon center{source.center_lat_deg,source.center_lon_deg};
  // Both projections validate their viewport. Using the source center chooses
  // the nearest wrapped world copy, including short antimeridian drags.
  if(!ProjectLatLon(source,center,&sx,&sy)||
     !ProjectLatLon(destination,center,&dx,&dy))return false;
  const double scale=std::ldexp(static_cast<double>(destination.tile_size_px)/source.tile_size_px,
                                static_cast<int>(destination.zoom)-source.zoom);
  if(!std::isfinite(scale)||scale<=0)return false;
  const FrameTransform result{scale,dx-sx*scale,dy-sy*scale};
  if(!std::isfinite(result.dx)||!std::isfinite(result.dy))return false;
  *output=result;return true;
}
}

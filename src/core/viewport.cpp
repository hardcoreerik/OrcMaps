#include "orcmap/viewport.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace orcmap {

namespace {

// Converts a screen-coordinate double to Q16.16 once, when the map is
// prepared; ProjectLocal then stays in integer arithmetic. Rounding here is
// the inverse of the shift-then-floor in FloorQ16ToInt, so a value that is
// exactly representable in Q16.16 round-trips unchanged.
int64_t ToQ16(double v) {
  if (!std::isfinite(v)) return 0;
  constexpr double kScale = 65536.0;
  const double scaled = std::floor(v * kScale + 0.5);
  // int64 in Q16.16 covers +-1.4e14 screen pixels; saturate rather than
  // overflow if a caller passes something absurd.
  if (scaled >= 9.2233720368547758e18) {
    return std::numeric_limits<int64_t>::max();
  }
  if (scaled <= -9.2233720368547758e18) {
    return std::numeric_limits<int64_t>::min();
  }
  return static_cast<int64_t>(scaled);
}

// Arithmetic right shift floors, which is what std::floor did for negatives.
int FloorQ16ToInt(int64_t v) {
  const int64_t whole = v >> 16;
  if (whole > static_cast<int64_t>(std::numeric_limits<int>::max())) {
    return std::numeric_limits<int>::max();
  }
  if (whole < static_cast<int64_t>(std::numeric_limits<int>::min())) {
    return std::numeric_limits<int>::min();
  }
  return static_cast<int>(whole);
}

// Shortest signed X delta in tile units, range [-n/2, n/2).
double WrappedTileDeltaX(double tile_x, double center_x, uint32_t n) {
  if (n == 0) return 0.0;
  const double nd = static_cast<double>(n);
  double dx = tile_x - center_x;
  dx -= nd * std::floor((dx + nd * 0.5) / nd);
  return dx;
}

uint32_t WrapTileX(int64_t x, uint32_t n) {
  const int64_t n64 = static_cast<int64_t>(n);
  int64_t w = x % n64;
  if (w < 0) w += n64;
  return static_cast<uint32_t>(w);
}

bool ValidViewport(const Viewport& viewport) {
  return ZoomIsValid(viewport.zoom) && viewport.width_px > 0 &&
         viewport.height_px > 0 && viewport.tile_size_px > 0 &&
         std::isfinite(viewport.center_lat_deg) &&
         std::isfinite(viewport.center_lon_deg);
}

bool ValidBounds(const GeoBounds& bounds) {
  return std::isfinite(bounds.min_lon_deg) &&
         std::isfinite(bounds.max_lon_deg) &&
         std::isfinite(bounds.min_lat_deg) &&
         std::isfinite(bounds.max_lat_deg) && bounds.min_lon_deg >= -180.0 &&
         bounds.min_lon_deg <= 180.0 && bounds.max_lon_deg >= -180.0 &&
         bounds.max_lon_deg <= 180.0 &&
         bounds.min_lat_deg >= -kMercatorMaxLatDeg &&
         bounds.max_lat_deg <= kMercatorMaxLatDeg &&
         bounds.min_lat_deg <= bounds.max_lat_deg;
}

}  // namespace

bool SetCenter(Viewport* viewport, double lat_deg, double lon_deg) {
  if (viewport == nullptr || !std::isfinite(lat_deg) ||
      !std::isfinite(lon_deg)) {
    return false;
  }
  viewport->center_lat_deg = ClampLatitudeDeg(lat_deg);
  viewport->center_lon_deg = WrapLongitudeDeg(lon_deg);
  return true;
}

LatLon GetCenter(const Viewport& viewport) {
  return {viewport.center_lat_deg, viewport.center_lon_deg};
}

bool SetZoom(Viewport* viewport, int zoom) {
  if (viewport == nullptr) return false;
  if (zoom < 0) zoom = 0;
  if (zoom > kMaxZoom) zoom = kMaxZoom;
  viewport->zoom = static_cast<uint8_t>(zoom);
  return true;
}

uint8_t GetZoom(const Viewport& viewport) { return viewport.zoom; }

bool ZoomIn(Viewport* viewport) {
  return viewport != nullptr && SetZoom(viewport, viewport->zoom + 1);
}

bool ZoomOut(Viewport* viewport) {
  return viewport != nullptr && SetZoom(viewport, viewport->zoom - 1);
}

bool SetViewportSize(Viewport* viewport, int width_px, int height_px) {
  if (viewport == nullptr || width_px <= 0 || height_px <= 0) return false;
  viewport->width_px = width_px;
  viewport->height_px = height_px;
  return true;
}

bool ScreenToLatLon(const Viewport& viewport, double screen_x,
                    double screen_y, LatLon* point) {
  if (point == nullptr || !ValidViewport(viewport) ||
      !std::isfinite(screen_x) || !std::isfinite(screen_y)) {
    return false;
  }
  TileCoord coord = LatLonToTileCoord(
      viewport.center_lat_deg, viewport.center_lon_deg, viewport.zoom);
  coord.x += (screen_x - static_cast<double>(viewport.width_px) * 0.5) /
             viewport.tile_size_px;
  coord.y += (screen_y - static_cast<double>(viewport.height_px) * 0.5) /
             viewport.tile_size_px;
  *point = TileCoordToLatLon(coord, viewport.zoom);
  return true;
}

bool ProjectLatLon(const Viewport& viewport, LatLon point, double* screen_x,
                   double* screen_y) {
  if (screen_x == nullptr || screen_y == nullptr || !ValidViewport(viewport) ||
      !std::isfinite(point.lat_deg) || !std::isfinite(point.lon_deg)) {
    return false;
  }
  const uint32_t n = TilesPerAxis(viewport.zoom);
  const TileCoord center = LatLonToTileCoord(
      viewport.center_lat_deg, viewport.center_lon_deg, viewport.zoom);
  const TileCoord target =
      LatLonToTileCoord(point.lat_deg, point.lon_deg, viewport.zoom);
  *screen_x = static_cast<double>(viewport.width_px) * 0.5 +
              WrappedTileDeltaX(target.x, center.x, n) * viewport.tile_size_px;
  *screen_y = static_cast<double>(viewport.height_px) * 0.5 +
              (target.y - center.y) * viewport.tile_size_px;
  return true;
}

bool PanByPixels(Viewport* viewport, double dx, double dy) {
  if (viewport == nullptr || !std::isfinite(dx) || !std::isfinite(dy)) {
    return false;
  }
  LatLon center;
  if (!ScreenToLatLon(*viewport,
                      static_cast<double>(viewport->width_px) * 0.5 + dx,
                      static_cast<double>(viewport->height_px) * 0.5 + dy,
                      &center)) {
    return false;
  }
  return SetCenter(viewport, center.lat_deg, center.lon_deg);
}

bool GetVisibleBounds(const Viewport& viewport, GeoBounds* bounds) {
  if (bounds == nullptr || !ValidViewport(viewport)) return false;
  LatLon northwest;
  LatLon southeast;
  if (!ScreenToLatLon(viewport, 0.0, 0.0, &northwest) ||
      !ScreenToLatLon(viewport, viewport.width_px, viewport.height_px,
                      &southeast)) {
    return false;
  }
  const double world_px =
      static_cast<double>(TilesPerAxis(viewport.zoom)) * viewport.tile_size_px;
  if (viewport.width_px >= world_px) {
    bounds->min_lon_deg = -180.0;
    bounds->max_lon_deg = 180.0;
  } else {
    bounds->min_lon_deg = northwest.lon_deg;
    bounds->max_lon_deg = southeast.lon_deg;
  }
  bounds->min_lat_deg = southeast.lat_deg;
  bounds->max_lat_deg = northwest.lat_deg;
  return true;
}

bool FitBounds(Viewport* viewport, const GeoBounds& bounds, int padding_px) {
  if (viewport == nullptr || !ValidBounds(bounds) || padding_px < 0 ||
      viewport->tile_size_px <= 0) {
    return false;
  }
  const int64_t available_width =
      static_cast<int64_t>(viewport->width_px) - 2LL * padding_px;
  const int64_t available_height =
      static_cast<int64_t>(viewport->height_px) - 2LL * padding_px;
  if (available_width <= 0 || available_height <= 0) return false;

  double lon_span = bounds.max_lon_deg - bounds.min_lon_deg;
  if (lon_span < 0.0) lon_span += 360.0;
  if (bounds.min_lon_deg == -180.0 && bounds.max_lon_deg == 180.0) {
    lon_span = 360.0;
  }
  const double center_lon = WrapLongitudeDeg(bounds.min_lon_deg + lon_span * 0.5);
  const double north_y = LatLonToTileCoord(bounds.max_lat_deg, 0.0, 0).y;
  const double south_y = LatLonToTileCoord(bounds.min_lat_deg, 0.0, 0).y;
  const double y_span = south_y - north_y;

  int selected_zoom = 0;
  for (int zoom = kMaxZoom; zoom >= 0; --zoom) {
    const double scale =
        static_cast<double>(TilesPerAxis(static_cast<uint8_t>(zoom))) *
        viewport->tile_size_px;
    if (lon_span / 360.0 * scale <= available_width &&
        y_span * scale <= available_height) {
      selected_zoom = zoom;
      break;
    }
  }
  SetZoom(viewport, selected_zoom);
  const double n = static_cast<double>(TilesPerAxis(viewport->zoom));
  const LatLon center = TileCoordToLatLon(
      {LatLonToTileCoord(0.0, center_lon, viewport->zoom).x,
       (north_y + south_y) * 0.5 * n},
      viewport->zoom);
  return SetCenter(viewport, center.lat_deg, center_lon);
}

bool MinFillZoom(const Viewport& viewport, const GeoBounds& bounds,
                 uint8_t* out_zoom) {
  if (out_zoom == nullptr || !ValidBounds(bounds) ||
      viewport.tile_size_px <= 0 || viewport.width_px <= 0 ||
      viewport.height_px <= 0) {
    return false;
  }

  double lon_span = bounds.max_lon_deg - bounds.min_lon_deg;
  if (lon_span < 0.0) lon_span += 360.0;
  if (bounds.min_lon_deg == -180.0 && bounds.max_lon_deg == 180.0) {
    lon_span = 360.0;
  }
  const double north_y = LatLonToTileCoord(bounds.max_lat_deg, 0.0, 0).y;
  const double south_y = LatLonToTileCoord(bounds.min_lat_deg, 0.0, 0).y;
  const double y_span = south_y - north_y;

  // Ascending: the first zoom whose viewport fits inside the bounds is the
  // smallest one that fills the screen, so the map is as wide-area as it
  // can be without showing empty margin.
  // Bounds spanning the whole planet wrap around themselves, so the viewport
  // is covered horizontally at every zoom and only height can fail. Without
  // this, a global pack would be judged unable to fill any screen wider than
  // the world -- which is exactly the empty margin world-copy placement
  // exists to remove.
  const bool spans_world = lon_span >= 360.0;

  for (int zoom = 0; zoom <= kMaxZoom; ++zoom) {
    const double scale =
        static_cast<double>(TilesPerAxis(static_cast<uint8_t>(zoom))) *
        viewport.tile_size_px;
    const bool covers_width =
        spans_world ||
        static_cast<double>(viewport.width_px) <= lon_span / 360.0 * scale;
    const bool covers_height =
        static_cast<double>(viewport.height_px) <= y_span * scale;
    if (covers_width && covers_height) {
      *out_zoom = static_cast<uint8_t>(zoom);
      return true;
    }
  }
  return false;
}

bool WorldViewZoom(const Viewport& viewport, uint8_t* out_zoom) {
  if (out_zoom == nullptr || viewport.tile_size_px <= 0 ||
      viewport.width_px <= 0 || viewport.height_px <= 0) {
    return false;
  }
  // Width is deliberately not tested: world-copy placement repeats the map
  // horizontally, so any width is covered. Height is clamped, not wrapped.
  for (int zoom = 0; zoom <= kMaxZoom; ++zoom) {
    const double world_px =
        static_cast<double>(TilesPerAxis(static_cast<uint8_t>(zoom))) *
        viewport.tile_size_px;
    if (static_cast<double>(viewport.height_px) <= world_px) {
      *out_zoom = static_cast<uint8_t>(zoom);
      return true;
    }
  }
  return false;
}

bool FillBounds(Viewport* viewport, const GeoBounds& bounds) {
  if (viewport == nullptr) return false;
  uint8_t zoom = 0;
  if (!MinFillZoom(*viewport, bounds, &zoom)) return false;

  double lon_span = bounds.max_lon_deg - bounds.min_lon_deg;
  if (lon_span < 0.0) lon_span += 360.0;
  if (bounds.min_lon_deg == -180.0 && bounds.max_lon_deg == 180.0) {
    lon_span = 360.0;
  }
  const double center_lon = WrapLongitudeDeg(bounds.min_lon_deg + lon_span * 0.5);
  const double north_y = LatLonToTileCoord(bounds.max_lat_deg, 0.0, 0).y;
  const double south_y = LatLonToTileCoord(bounds.min_lat_deg, 0.0, 0).y;

  if (!SetZoom(viewport, zoom)) return false;
  const double n = static_cast<double>(TilesPerAxis(viewport->zoom));
  const LatLon center = TileCoordToLatLon(
      {LatLonToTileCoord(0.0, center_lon, viewport->zoom).x,
       (north_y + south_y) * 0.5 * n},
      viewport->zoom);
  return SetCenter(viewport, center.lat_deg, center_lon);
}

bool ZoomAtScreenPoint(Viewport* viewport, double screen_x, double screen_y,
                       int zoom_delta) {
  if (viewport == nullptr) return false;
  LatLon anchor;
  if (!ScreenToLatLon(*viewport, screen_x, screen_y, &anchor)) {
    return false;
  }
  const int current_zoom = viewport->zoom;
  const int target_zoom =
      zoom_delta > kMaxZoom - current_zoom
          ? kMaxZoom
          : (zoom_delta < -current_zoom ? 0 : current_zoom + zoom_delta);
  SetZoom(viewport, target_zoom);
  TileCoord center = LatLonToTileCoord(anchor.lat_deg, anchor.lon_deg,
                                       viewport->zoom);
  center.x -= (screen_x - static_cast<double>(viewport->width_px) * 0.5) /
              viewport->tile_size_px;
  center.y -= (screen_y - static_cast<double>(viewport->height_px) * 0.5) /
              viewport->tile_size_px;
  const LatLon adjusted = TileCoordToLatLon(center, viewport->zoom);
  return SetCenter(viewport, adjusted.lat_deg, adjusted.lon_deg);
}

bool NearestTilePlacement(const Viewport& viewport, TileId tile,
                          TilePlacement* out) {
  if (out == nullptr) return false;
  if (!ZoomIsValid(viewport.zoom)) return false;
  if (tile.z != viewport.zoom) return false;
  const uint32_t n = TilesPerAxis(viewport.zoom);
  if (n == 0 || tile.x >= n || tile.y >= n) return false;
  const TileCoord center = LatLonToTileCoord(
      viewport.center_lat_deg, viewport.center_lon_deg, viewport.zoom);
  const double dx = WrappedTileDeltaX(static_cast<double>(tile.x), center.x, n);
  out->tile = tile;
  // center.x + dx is an exact integer tile index in double form; the tile
  // index is what MakeTileScreenMap effectively used.
  out->unwrapped_x = static_cast<int64_t>(std::llround(center.x + dx));
  return true;
}

bool MakeTilePlacementScreenMap(const Viewport& viewport,
                                const TilePlacement& placement,
                                uint32_t extent, TileScreenMap* out) {
  if (out == nullptr) return false;
  out->valid = false;
  if (!ZoomIsValid(viewport.zoom)) return false;
  if (placement.tile.z != viewport.zoom) return false;
  const uint32_t n = TilesPerAxis(viewport.zoom);
  if (n == 0 || placement.tile.y >= n) return false;
  // The placement's own world copy is authoritative here, so there is no
  // nearest-copy wrapping: unwrapped_x already says where this instance is.
  if (WrapTileX(placement.unwrapped_x, n) != placement.tile.x) return false;

  const double ext = extent == 0 ? 1.0 : static_cast<double>(extent);
  const double tile_px = viewport.tile_size_px <= 0
                             ? 1.0
                             : static_cast<double>(viewport.tile_size_px);
  const TileCoord center = LatLonToTileCoord(
      viewport.center_lat_deg, viewport.center_lon_deg, viewport.zoom);
  out->zoom = viewport.zoom;
  out->origin_sx =
      (static_cast<double>(placement.unwrapped_x) - center.x) * tile_px +
      static_cast<double>(viewport.width_px) * 0.5;
  out->origin_sy =
      (static_cast<double>(placement.tile.y) - center.y) * tile_px +
      static_cast<double>(viewport.height_px) * 0.5;
  out->scale = tile_px / ext;
  out->origin_sx_q16 = ToQ16(out->origin_sx);
  out->origin_sy_q16 = ToQ16(out->origin_sy);
  out->scale_q16 = ToQ16(out->scale);
  out->valid = true;
  return true;
}

bool EnumerateVisibleTilePlacements(const Viewport& viewport,
                                    std::vector<TilePlacement>* out) {
  if (out == nullptr) return false;
  out->clear();
  if (!ZoomIsValid(viewport.zoom)) return false;
  if (viewport.width_px <= 0 || viewport.height_px <= 0) return false;
  if (viewport.tile_size_px <= 0) return false;
  const uint32_t n = TilesPerAxis(viewport.zoom);
  if (n == 0) return false;

  const TileCoord center = LatLonToTileCoord(
      viewport.center_lat_deg, viewport.center_lon_deg, viewport.zoom);
  const double ts = static_cast<double>(viewport.tile_size_px);
  const double half_w = static_cast<double>(viewport.width_px) * 0.5 / ts;
  const double half_h = static_cast<double>(viewport.height_px) * 0.5 / ts;

  int64_t x0 = static_cast<int64_t>(std::floor(center.x - half_w));
  int64_t x1 = static_cast<int64_t>(std::ceil(center.x + half_w)) - 1;
  int64_t y0 = static_cast<int64_t>(std::floor(center.y - half_h));
  int64_t y1 = static_cast<int64_t>(std::ceil(center.y + half_h)) - 1;
  if (x1 < x0) return true;

  const int64_t n64 = static_cast<int64_t>(n);
  if (y1 < 0 || y0 >= n64) return true;
  if (y0 < 0) y0 = 0;
  if (y1 >= n64) y1 = n64 - 1;
  if (y0 > y1) return true;

  // Unlike EnumerateVisibleTiles, the X range is NOT collapsed to one world:
  // every copy on screen is emitted, which is what fills a viewport wider
  // than the world. The range is still bounded by the screen, so this is not
  // an unbounded scene graph.
  const int64_t x_count = x1 - x0 + 1;
  out->reserve(static_cast<size_t>(x_count) *
               static_cast<size_t>(y1 - y0 + 1));
  for (int64_t y = y0; y <= y1; ++y) {
    for (int64_t ux = x0; ux <= x1; ++ux) {
      TilePlacement p;
      p.tile = TileId{viewport.zoom, WrapTileX(ux, n),
                      static_cast<uint32_t>(y)};
      p.unwrapped_x = ux;
      out->push_back(p);
    }
  }
  return true;
}

bool MakeTileScreenMap(const Viewport& viewport, TileId tile, uint32_t extent,
                       TileScreenMap* out) {
  if (out == nullptr) return false;
  out->valid = false;
  if (!ZoomIsValid(viewport.zoom)) return false;
  if (tile.z != viewport.zoom) return false;
  const uint32_t n = TilesPerAxis(viewport.zoom);
  if (n == 0 || tile.x >= n || tile.y >= n) return false;
  const double ext = extent == 0 ? 1.0 : static_cast<double>(extent);
  const double tile_px = viewport.tile_size_px <= 0
                             ? 1.0
                             : static_cast<double>(viewport.tile_size_px);
  const TileCoord center = LatLonToTileCoord(
      viewport.center_lat_deg, viewport.center_lon_deg, viewport.zoom);
  const double dx = WrappedTileDeltaX(static_cast<double>(tile.x), center.x, n);
  const double origin_sx =
      dx * tile_px + static_cast<double>(viewport.width_px) * 0.5;
  const double origin_sy =
      (static_cast<double>(tile.y) - center.y) * tile_px +
      static_cast<double>(viewport.height_px) * 0.5;
  out->zoom = viewport.zoom;
  out->origin_sx = origin_sx;
  out->origin_sy = origin_sy;
  out->scale = tile_px / ext;
  out->origin_sx_q16 = ToQ16(origin_sx);
  out->origin_sy_q16 = ToQ16(origin_sy);
  out->scale_q16 = ToQ16(tile_px / ext);
  out->valid = true;
  return true;
}

bool EnumerateVisibleTiles(const Viewport& viewport, std::vector<TileId>* out) {
  if (out == nullptr) return false;
  out->clear();
  if (!ZoomIsValid(viewport.zoom)) return false;
  if (viewport.width_px <= 0 || viewport.height_px <= 0) return false;
  if (viewport.tile_size_px <= 0) return false;
  const uint32_t n = TilesPerAxis(viewport.zoom);
  if (n == 0) return false;

  const TileCoord center = LatLonToTileCoord(
      viewport.center_lat_deg, viewport.center_lon_deg, viewport.zoom);
  const double ts = static_cast<double>(viewport.tile_size_px);
  const double half_w = static_cast<double>(viewport.width_px) * 0.5 / ts;
  const double half_h = static_cast<double>(viewport.height_px) * 0.5 / ts;
  const double left = center.x - half_w;
  const double right = center.x + half_w;
  const double top = center.y - half_h;
  const double bottom = center.y + half_h;

  // Tiles whose [i, i+1) overlaps [left, right) / [top, bottom).
  int64_t x0 = static_cast<int64_t>(std::floor(left));
  int64_t x1 = static_cast<int64_t>(std::ceil(right)) - 1;
  int64_t y0 = static_cast<int64_t>(std::floor(top));
  int64_t y1 = static_cast<int64_t>(std::ceil(bottom)) - 1;
  if (x1 < x0) return true;

  const int64_t n64 = static_cast<int64_t>(n);
  if (y1 < 0 || y0 >= n64) return true;
  if (y0 < 0) y0 = 0;
  if (y1 >= n64) y1 = n64 - 1;
  if (y0 > y1) return true;

  int64_t x_count = x1 - x0 + 1;
  if (x_count > n64) x_count = n64;

  out->reserve(static_cast<size_t>(x_count) * static_cast<size_t>(y1 - y0 + 1));
  for (int64_t y = y0; y <= y1; ++y) {
    for (int64_t k = 0; k < x_count; ++k) {
      out->push_back(TileId{viewport.zoom, WrapTileX(x0 + k, n),
                            static_cast<uint32_t>(y)});
    }
  }
  return true;
}

void ProjectLocal(const TileScreenMap& map, int32_t local_x, int32_t local_y,
                  int* screen_x, int* screen_y) {
  if (!map.valid) return;
  if (screen_x != nullptr) {
    *screen_x = FloorQ16ToInt(map.origin_sx_q16 +
                              static_cast<int64_t>(local_x) * map.scale_q16);
  }
  if (screen_y != nullptr) {
    *screen_y = FloorQ16ToInt(map.origin_sy_q16 +
                              static_cast<int64_t>(local_y) * map.scale_q16);
  }
}

void TileLocalToScreen(const Viewport& viewport, TileId tile, uint32_t extent,
                       int32_t local_x, int32_t local_y, int* screen_x,
                       int* screen_y) {
  if (screen_x == nullptr || screen_y == nullptr) return;
  TileScreenMap map;
  if (!MakeTileScreenMap(viewport, tile, extent, &map)) return;
  ProjectLocal(map, local_x, local_y, screen_x, screen_y);
}

}  // namespace orcmap

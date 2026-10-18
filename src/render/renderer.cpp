#include "orcmap/renderer.hpp"

#include <cstdint>
#include <vector>

namespace orcmap {

namespace {

// Twice the signed area of a projected ring (shoelace). The sign is the ring's winding.
int64_t SignedArea2(const int* xy, size_t n) {
  if (n < 3) return 0;
  int64_t sum = 0;
  for (size_t i = 0; i < n; ++i) {
    const size_t j = (i + 1) % n;
    sum += static_cast<int64_t>(xy[i * 2]) * xy[j * 2 + 1] -
           static_cast<int64_t>(xy[j * 2]) * xy[i * 2 + 1];
  }
  return sum;
}

void DrawPointMapped(RenderTarget* target, const TileScreenMap& map,
                     int32_t local_x, int32_t local_y, Color color) {
  int sx = 0;
  int sy = 0;
  ProjectLocal(map, local_x, local_y, &sx, &sy);
  target->DrawPoint(sx, sy, color);
}

// The skip rules and style lookup shared by both draw paths, so a feature is
// never drawn by one and skipped by the other.
bool ResolveDrawablePaint(const Feature& feature, const Viewport& viewport,
                          const MapStyle& style, MapPaint* out) {
  if (!feature.kind_assigned) return false;
  if (feature.kind == FeatureKind::kLabelPrimary ||
      feature.kind == FeatureKind::kLabelSecondary ||
      feature.kind == FeatureKind::kLabelMuted) {
    return false;
  }
  const MapPaint paint =
      ResolveFeatureStyle(feature.kind, viewport.zoom, style);
  if (!paint.visible) return false;
  *out = paint;
  return true;
}

void RenderOneFeature(const Feature& feature, const TileScreenMap& map,
                      const Viewport& viewport, const MapStyle& style,
                      RenderTarget* target, std::vector<int>* poly_xy) {
  MapPaint paint;
  if (!ResolveDrawablePaint(feature, viewport, style, &paint)) return;

  switch (feature.geometry.type) {
    case GeomType::kPoint:
      for (const Path& path : feature.geometry.paths) {
        for (const Point& p : path) {
          DrawPointMapped(target, map, p.x, p.y, paint.color);
        }
      }
      break;
    case GeomType::kLineString:
      for (const Path& path : feature.geometry.paths) {
        if (path.size() < 2) continue;
        // Project each vertex once and carry the previous screen point. A
        // projection used to run twice per segment; it is the expensive part
        // of this loop.
        int sx0 = 0;
        int sy0 = 0;
        ProjectLocal(map, path[0].x, path[0].y, &sx0, &sy0);
        for (size_t i = 1; i < path.size(); ++i) {
          int sx1 = 0;
          int sy1 = 0;
          ProjectLocal(map, path[i].x, path[i].y, &sx1, &sy1);
          target->DrawLine(sx0, sy0, sx1, sy1, paint.color, paint.width_px);
          sx0 = sx1;
          sy0 = sy1;
        }
      }
      break;
    case GeomType::kPolygon:
      // A polygon feature holds several rings. MVT (and GeoJSON-like) data encodes a multipolygon as one feature whose
      // rings are exterior rings (all with the winding of the first ring) and holes (the opposite winding). Each
      // exterior ring starts a new part, and the holes that follow belong to it; a part is filled with the even-odd
      // rule so its holes are subtracted. Natural Earth overview tiles rely on this: an ocean or a continent is often
      // several parts in one feature, and filling only the first ring leaves the rest of the tile unpainted.
      if (feature.geometry.paths.empty()) break;
      {
        std::vector<int>& xy = *poly_xy;
        xy.clear();
        std::vector<size_t> counts;       // vertices per ring of the part being built
        int64_t exterior_sign = 0;        // winding of the first non-degenerate ring
        auto flush = [&]() {
          if (!counts.empty()) {
            target->FillPolygonRings(xy.data(), counts.data(), counts.size(), paint.color);
          }
          xy.clear();
          counts.clear();
        };
        for (const Path& ring : feature.geometry.paths) {
          if (ring.size() < 3) continue;
          const size_t first = xy.size();
          for (const Point& p : ring) {
            int sx = 0;
            int sy = 0;
            ProjectLocal(map, p.x, p.y, &sx, &sy);
            xy.push_back(sx);
            xy.push_back(sy);
          }
          const int64_t area = SignedArea2(xy.data() + first, ring.size());
          const int64_t sign = area > 0 ? 1 : area < 0 ? -1 : 0;
          if (exterior_sign == 0) exterior_sign = sign;
          // A ring that winds like the first one is a new exterior ring; the opposite winding is a hole in the
          // current part. A degenerate (zero-area) ring is kept with the current part.
          const bool new_exterior = sign != 0 && sign == exterior_sign;
          if (new_exterior && !counts.empty()) {
            // Close out the previous part: move this ring's vertices aside, flush, then restore them.
            std::vector<int> pending(xy.begin() + static_cast<std::ptrdiff_t>(first), xy.end());
            xy.resize(first);
            flush();
            xy = pending;
          }
          counts.push_back(ring.size());
        }
        flush();
      }
      break;
    case GeomType::kUnknown:
    default:
      break;
  }
}

bool SameViewport(const Viewport& a, const Viewport& b) {
  return a.zoom == b.zoom && a.width_px == b.width_px &&
         a.height_px == b.height_px && a.tile_size_px == b.tile_size_px &&
         a.center_lat_deg == b.center_lat_deg &&
         a.center_lon_deg == b.center_lon_deg;
}

bool SameTile(TileId a, TileId b) {
  return a.z == b.z && a.x == b.x && a.y == b.y;
}

bool RenderFeatureAtImpl(const Feature& feature, const TilePlacement& placement,
                         const Viewport& viewport, const MapStyle& style,
                         RenderTarget* target, RenderScratch& scratch) {
  if (target == nullptr) return false;
  if (!ZoomIsValid(viewport.zoom)) return false;
  if (placement.tile.z != viewport.zoom) return false;
  const uint32_t extent = feature.extent == 0 ? 4096u : feature.extent;

  // Preparing the map runs Mercator center math, so a streaming caller must
  // not pay it per feature. Every input it depends on is part of the key.
  if (!scratch.have_map || scratch.extent != extent ||
      scratch.unwrapped_x != placement.unwrapped_x ||
      !SameTile(scratch.tile, placement.tile) ||
      !SameViewport(scratch.viewport, viewport)) {
    if (!MakeTilePlacementScreenMap(viewport, placement, extent,
                                    &scratch.map)) {
      scratch.have_map = false;
      return false;
    }
    scratch.have_map = true;
    scratch.tile = placement.tile;
    scratch.unwrapped_x = placement.unwrapped_x;
    scratch.extent = extent;
    scratch.viewport = viewport;
  }

  RenderOneFeature(feature, scratch.map, viewport, style, target,
                   &scratch.poly_xy);
  return true;
}

}  // namespace

bool ClearMapBackground(const Viewport& viewport, const MapStyle& style,
                        RenderTarget* target) {
  if (target == nullptr) return false;
  if (!ZoomIsValid(viewport.zoom)) return false;
  const MapPaint background =
      ResolveFeatureStyle(FeatureKind::kBackground, viewport.zoom, style);
  if (background.visible) {
    target->FillRect(0, 0, target->Width(), target->Height(), background.color);
  }
  return true;
}

bool ProjectFeatureToScreen(const Feature& feature, const TileScreenMap& map,
                            const Viewport& viewport, const MapStyle& style,
                            ScreenFeature* out) {
  if (out == nullptr) return false;
  MapPaint paint;
  if (!ResolveDrawablePaint(feature, viewport, style, &paint)) return false;

  out->xy.clear();
  out->path_counts.clear();
  out->ring_sign.clear();
  out->type = feature.geometry.type;
  out->color = paint.color;
  out->width_px = paint.width_px;

  const bool is_polygon = out->type == GeomType::kPolygon;
  const bool is_line = out->type == GeomType::kLineString;
  size_t kept = 0;
  for (const Path& path : feature.geometry.paths) {
    if (is_polygon && path.size() < 3) continue;
    if (is_line && path.size() < 2) continue;
    if (!is_polygon && !is_line && path.empty()) continue;
    const size_t first = out->xy.size();
    for (const Point& p : path) {
      int sx = 0;
      int sy = 0;
      ProjectLocal(map, p.x, p.y, &sx, &sy);
      out->xy.push_back(sx);
      out->xy.push_back(sy);
    }
    out->path_counts.push_back(path.size());
    if (is_polygon) {
      const int64_t area = SignedArea2(out->xy.data() + first, path.size());
      out->ring_sign.push_back(area > 0 ? 1 : (area < 0 ? -1 : 0));
    }
    ++kept;
  }
  if (kept == 0) return false;

  int64_t min_x = out->xy[0];
  int64_t max_x = out->xy[0];
  int64_t min_y = out->xy[1];
  int64_t max_y = out->xy[1];
  for (size_t i = 0; i * 2 + 1 < out->xy.size(); ++i) {
    const int64_t x = out->xy[i * 2];
    const int64_t y = out->xy[i * 2 + 1];
    if (x < min_x) min_x = x;
    if (x > max_x) max_x = x;
    if (y < min_y) min_y = y;
    if (y > max_y) max_y = y;
  }
  out->min_x = min_x;
  out->max_x = max_x;
  out->min_y = min_y;
  out->max_y = max_y;
  return true;
}

void DrawScreenFeature(const ScreenFeature& feature, RenderTarget* target) {
  if (target == nullptr || feature.path_counts.empty()) return;
  switch (feature.type) {
    case GeomType::kPoint: {
      size_t v = 0;
      for (const size_t count : feature.path_counts) {
        for (size_t i = 0; i < count; ++i) {
          target->DrawPoint(feature.xy[v + i * 2], feature.xy[v + i * 2 + 1],
                            feature.color);
        }
        v += count * 2;
      }
      break;
    }
    case GeomType::kLineString: {
      size_t v = 0;
      for (const size_t count : feature.path_counts) {
        for (size_t i = 1; i < count; ++i) {
          target->DrawLine(feature.xy[v + (i - 1) * 2],
                           feature.xy[v + (i - 1) * 2 + 1],
                           feature.xy[v + i * 2], feature.xy[v + i * 2 + 1],
                           feature.color, feature.width_px);
        }
        v += count * 2;
      }
      break;
    }
    case GeomType::kPolygon: {
      // Same grouping rule as the streaming path: a ring winding like the
      // first starts a new exterior part, the opposite winding is a hole in
      // the current part, and a degenerate ring stays with the current part.
      int8_t exterior_sign = 0;
      size_t vertex = 0;
      size_t part_ring = 0;
      size_t part_vertex = 0;
      size_t part_rings = 0;
      for (size_t r = 0; r < feature.ring_sign.size(); ++r) {
        const int8_t sign = feature.ring_sign[r];
        if (exterior_sign == 0) exterior_sign = sign;
        const bool new_exterior = sign != 0 && sign == exterior_sign;
        if (new_exterior && part_rings > 0) {
          target->FillPolygonRings(feature.xy.data() + part_vertex * 2,
                                   feature.path_counts.data() + part_ring,
                                   part_rings, feature.color);
          part_ring = r;
          part_vertex = vertex;
          part_rings = 0;
        }
        ++part_rings;
        vertex += feature.path_counts[r];
      }
      if (part_rings > 0) {
        target->FillPolygonRings(feature.xy.data() + part_vertex * 2,
                                 feature.path_counts.data() + part_ring,
                                 part_rings, feature.color);
      }
      break;
    }
    default:
      break;
  }
}

bool RenderFeatureTile(const FeatureTile& features, TileId source_tile,
                       const Viewport& viewport, const MapStyle& style,
                       RenderTarget* target) {
  // Unchanged behaviour: draw at the world copy nearest the centre. The
  // wrapping rule stays in viewport.cpp rather than being duplicated here.
  TilePlacement nearest;
  if (!NearestTilePlacement(viewport, source_tile, &nearest)) return false;
  return RenderFeatureTileAt(features, nearest, viewport, style, target);
}

bool RenderFeatureAt(const Feature& feature, const TilePlacement& placement,
                     const Viewport& viewport, const MapStyle& style,
                     RenderTarget* target) {
  RenderScratch scratch;
  return RenderFeatureAt(feature, placement, viewport, style, target,
                         &scratch);
}

bool RenderFeatureAt(const Feature& feature, const TilePlacement& placement,
                     const Viewport& viewport, const MapStyle& style,
                     RenderTarget* target, RenderScratch* scratch) {
  if (scratch != nullptr) {
    return RenderFeatureAtImpl(feature, placement, viewport, style, target,
                               *scratch);
  }
  RenderScratch one_shot;
  return RenderFeatureAtImpl(feature, placement, viewport, style, target,
                             one_shot);
}

bool RenderFeatureTileAt(const FeatureTile& features,
                         const TilePlacement& placement,
                         const Viewport& viewport, const MapStyle& style,
                         RenderTarget* target) {
  if (target == nullptr) return false;
  if (!ZoomIsValid(viewport.zoom)) return false;
  if (placement.tile.z != viewport.zoom) return false;

  TileScreenMap map;
  uint32_t prepared_extent = 0;
  bool have_map = false;
  RenderScratch scratch;
  for (const Feature& feature : features.features) {
    const uint32_t extent = feature.extent == 0 ? 4096u : feature.extent;
    if (!have_map || extent != prepared_extent) {
      if (!MakeTilePlacementScreenMap(viewport, placement, extent, &map)) {
        return false;
      }
      prepared_extent = extent;
      have_map = true;
    }
    RenderOneFeature(feature, map, viewport, style, target, &scratch.poly_xy);
  }
  return true;
}

}  // namespace orcmap

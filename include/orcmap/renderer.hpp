#pragma once

#include "orcmap/feature.hpp"
#include "orcmap/geo.hpp"
#include "orcmap/render_target.hpp"
#include "orcmap/style.hpp"
#include "orcmap/viewport.hpp"

namespace orcmap {

// Reusable per-frame state for a streaming caller that renders many features
// from the same tile. It removes the two costs the render path otherwise pays
// per feature: a heap allocation for the polygon vertex buffer, and a rebuild
// of the tile's screen map (which runs Mercator center math).
//
// The cached map is invalidated whenever the tile, its world copy, the
// feature extent, or the viewport changes, so a scratch may live for a whole
// frame. It is not thread-safe.
struct RenderScratch {
  std::vector<int> poly_xy;
  bool have_map = false;
  TileId tile{};
  int64_t unwrapped_x = 0;
  uint32_t extent = 0;
  Viewport viewport{};
  TileScreenMap map;
};

// Fills the whole target with the style background, once per map frame.
// Must be called before the per-tile RenderFeatureTile draws; tiles do
// not clear the target. Color comes from ResolveFeatureStyle(kBackground).
// Returns false if `target` is null or viewport.zoom is invalid.

bool ClearMapBackground(const Viewport& viewport, const MapStyle& style,
                        RenderTarget* target);

// Walks an OrcMaps FeatureTile, resolves paint via ResolveFeatureStyle,
// and issues primitives to `target`. No MVT types, no graphics-framework
// types. Does not clear the target.
//
// Returns false (and draws nothing) if:
// - target is null
// - viewport.zoom is not in 0..31
// - source_tile.z != viewport.zoom (overzoom is not implemented)
//
// Features with kind_assigned == false are skipped (not guessed).
// Label kinds (kLabelPrimary/Secondary/Muted) are skipped until text
// rendering exists -- they are not drawn as placeholder point pixels.
// A polygon feature's rings are grouped into parts (a ring that winds like the
// first starts a new exterior part; the opposite winding is a hole) and each
// part is filled even-odd, so holes are subtracted -- see ARCHITECTURE.md.
// Line strokes use MapPaint::width_px via RenderTarget::DrawLine.

bool RenderFeatureTile(const FeatureTile& features, TileId source_tile,
                       const Viewport& viewport, const MapStyle& style,
                       RenderTarget* target);

// As RenderFeatureTile, but draws the tile at a specific world copy. Pair it
// with EnumerateVisibleTilePlacements so a world narrower than the viewport
// extends around itself instead of leaving empty margin. Decode each distinct
// TileId once and call this per placement.
bool RenderFeatureTileAt(const FeatureTile& features,
                         const TilePlacement& placement,
                         const Viewport& viewport, const MapStyle& style,
                         RenderTarget* target);

// Draws ONE feature at one placement. This is what a streaming decoder needs
// (see MvtDecodeOptions::feature_sink): the caller never has to hold a whole
// FeatureTile, which is what made mid-zoom tiles impossible on a board
// without PSRAM. Same paint, clipping and skip rules as the batch calls.
bool RenderFeatureAt(const Feature& feature, const TilePlacement& placement,
                     const Viewport& viewport, const MapStyle& style,
                     RenderTarget* target);

// As above, reusing `scratch` across features. Pass the same scratch for
// every feature streamed from one tile; it allocates at most once for the
// polygon vertex buffer and prepares the tile map at most once.
bool RenderFeatureAt(const Feature& feature, const TilePlacement& placement,
                     const Viewport& viewport, const MapStyle& style,
                     RenderTarget* target, RenderScratch* scratch);

}  // namespace orcmap

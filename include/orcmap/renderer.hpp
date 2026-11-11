#pragma once

#include <cstdint>
#include <vector>

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
// Polygons are painted before strokes/points, preserving source order within
// each group. Archive layer order cannot hide borders/roads beneath later fills.
// Label kinds (kLabelPrimary/Secondary/Muted) are skipped until text
// rendering exists -- they are not drawn as placeholder point pixels.
// A polygon feature's rings are grouped into parts (a ring that winds like the
// first starts a new exterior part; the opposite winding is a hole) and each
// part is filled even-odd, so holes are subtracted -- see ARCHITECTURE.md.
// Line strokes use MapPaint::width_px via RenderTarget::DrawLine.
// Clipping-capable targets restrict buffered geometry to its tile's half-open
// screen rectangle, intersected with the caller's clip, then restore that clip.
// Custom targets without SetClipRect support must provide their own tile scissor.

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

// Ordered streaming composition: fills first, strokes/points last. Immediate
// RenderFeatureAt deliberately retains its caller-order contract. Reuse one
// queue for all placements of a source tile; flush only after successful decode.
// Reset on every tile and on decode failure/cancellation. Capacity is retained
// for reuse and released by destruction (no whole decoded tile is retained).
enum class OrderedRenderStatus { kReady, kCapacityExceeded, kCancelled, kInvalidInput };
struct OrderedRenderScratch {
  static constexpr size_t kMaxPoints = 65536;
  static constexpr size_t kMaxPaths = 4096;
  struct StrokePath {
    size_t first_point = 0, point_count = 0;
    Color color{};
    float width_px = 1;
    PixelRect clip;
    GeomType type = GeomType::kLineString;
  };
  RenderScratch render;
  std::vector<int> xy;
  std::vector<StrokePath> paths;
  OrderedRenderStatus status = OrderedRenderStatus::kReady;
  size_t peak_bytes = 0;
  void Reset();
  size_t RetainedBytes() const;
};

// Capacity failure rejects the entire feature, leaves the previous queue
// intact, poisons this tile, and returns false. bad_alloc propagates; discard
// the tile and Reset before reuse. Render into an unpublished frame throughout.
bool RenderOrderedFeatureAt(const Feature& feature, const TilePlacement& placement,
                            const Viewport& viewport, const MapStyle& style,
                            RenderTarget* target, OrderedRenderScratch* scratch);

// Restores the caller's clip and clears queued counts on every exit. The optional
// service callback is checked at least every 128 segments/points, including long
// paths; false cancels. A false result must never be published as a ready frame.
bool FinishOrderedTile(OrderedRenderScratch* scratch, RenderTarget* target,
                       bool (*service)(void*) = nullptr, void* context = nullptr);

// ---------------------------------------------------------------------------
// Screen-space cache
// ---------------------------------------------------------------------------
//
// A feature projected once, so the same frame can be painted more than once
// without repeating the per-vertex projection, the style lookup, or the
// polygon-winding grouping. A target that paints in horizontal bands walks
// this cache once per band and skips the features whose bounds miss that band,
// which is what makes banded painting cost close to the width of the bus
// instead of one SPI transaction per primitive.
//
// Pass the same ScreenFeature back in to reuse its buffers: after warm-up
// projection does not allocate.

struct ScreenFeature {
  GeomType type = GeomType::kUnknown;
  Color color{};
  float width_px = 1.0f;
  PixelRect tile_clip;
  // Screen x,y pairs, every path's vertices back to back.
  std::vector<int> xy;
  // Vertices per path: one entry per line string, or one per polygon ring.
  std::vector<size_t> path_counts;
  // Winding sign per polygon ring (+1, -1, or 0 for a degenerate ring).
  std::vector<int8_t> ring_sign;
  // Inclusive screen bounds of everything this feature would paint.
  int64_t min_x = 0;
  int64_t min_y = 0;
  int64_t max_x = 0;
  int64_t max_y = 0;
};

// Projects one feature into `out`, reusing its buffers. Returns false when the
// feature would paint nothing -- kind unassigned, a label kind, hidden by the
// style, or no drawable path -- which are exactly the rules RenderFeatureAt
// applies. `map` must be built for this feature's tile and placement.
bool ProjectFeatureToScreen(const Feature& feature, const TileScreenMap& map,
                            const Viewport& viewport, const MapStyle& style,
                            ScreenFeature* out);

// Paints a ScreenFeature produced by ProjectFeatureToScreen. A host test pins
// this to be pixel-identical to RenderFeatureAt for the same features, so the
// two paths cannot drift.
void DrawScreenFeature(const ScreenFeature& feature, RenderTarget* target);

}  // namespace orcmap

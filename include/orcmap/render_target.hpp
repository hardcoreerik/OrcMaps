#pragma once

#include <cstddef>
#include <algorithm>
#include <cstdint>

#include "orcmap/color.hpp"

namespace orcmap {

// Half-open raster rectangle. Use 64-bit intermediate sums for offscreen input.
struct PixelRect { int x = 0, y = 0, width = 0, height = 0; };
inline PixelRect IntersectPixelRects(PixelRect a, PixelRect b) {
  const int x = std::max(a.x, b.x), y = std::max(a.y, b.y);
  const int64_t right = std::min(int64_t{a.x} + std::max(0, a.width),
                                 int64_t{b.x} + std::max(0, b.width));
  const int64_t bottom = std::min(int64_t{a.y} + std::max(0, a.height),
                                  int64_t{b.y} + std::max(0, b.height));
  return {x, y, static_cast<int>(std::max(int64_t{0}, right - x)),
                static_cast<int>(std::max(int64_t{0}, bottom - y))};
}

// Graphics-library-independent draw surface. Understands pixels and
// primitives, not FeatureKind, MVT, PMTiles, or application overlays.
// Immediate: the renderer issues a primitive and the target draws it.
// There is no mandatory command buffer.
//
// Color is orcmap::Color (RGBA8). Conversion to a display's native pixel
// format is the target's job.

class RenderTarget {
 public:
  virtual ~RenderTarget() = default;

  virtual int Width() const = 0;
  virtual int Height() const = 0;

  // Optional native scissor support for composing buffered vector tiles.
  // A successful setter clips every primitive and permits restoring ClipRect().
  // Legacy targets keep compiling; false means they retain caller-owned clipping.
  virtual PixelRect ClipRect() const { return {0, 0, Width(), Height()}; }
  virtual bool SetClipRect(PixelRect) { return false; }

  virtual void FillRect(int x, int y, int w, int h, Color color) = 0;
  virtual void DrawPoint(int x, int y, Color color) = 0;

  // Centered stroke. `width_px` comes from MapPaint (the style), never
  // from FeatureKind inside an adapter. See include/orcmap/stroke.hpp.
  virtual void DrawLine(int x0, int y0, int x1, int y1, Color color,
                        float width_px) = 0;

  // Simple filled polygon, `n_points` vertices as [x0,y0,x1,y1,...].
  // One ring, no holes (use FillPolygonRings for those). n_points < 3 is a no-op. The target clips to its bounds.
  virtual void FillPolygon(const int* xy_pairs, size_t n_points, Color color) = 0;

  // One polygon made of several rings, filled with the even-odd rule: an exterior ring and the holes inside it.
  // `xy_pairs` holds every ring's vertices back to back as [x0,y0,x1,y1,...]; `ring_point_counts[i]` is the vertex
  // count of ring i. The default fills only the first ring, which is the behaviour a target had before this existed,
  // so an existing RenderTarget keeps compiling and drawing exactly as it did; targets that can do better override it.
  virtual void FillPolygonRings(const int* xy_pairs, const size_t* ring_point_counts,
                                size_t ring_count, Color color) {
    if (xy_pairs == nullptr || ring_point_counts == nullptr || ring_count == 0) return;
    FillPolygon(xy_pairs, ring_point_counts[0], color);
  }
};

}  // namespace orcmap

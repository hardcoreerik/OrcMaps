#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

// M5GFX is a LovyanGFX fork and this adapter is written against
// lgfx::v1::LovyanGFX, so either distribution satisfies it. Boards M5GFX
// does not carry a panel driver for (e.g. the CYD's ST7796) depend on
// LovyanGFX directly; prefer M5GFX when both are present so the existing
// M5Stack examples are unaffected.
#if __has_include(<M5GFX.h>)
#include <M5GFX.h>
#elif __has_include(<LovyanGFX.hpp>)
#include <LovyanGFX.hpp>
#else
#error "orcmap/m5gfx/display_target.hpp requires M5GFX or LovyanGFX"
#endif

#include "orcmap/clip.hpp"
#include "orcmap/m5gfx/color.hpp"
#include "orcmap/render_target.hpp"
#include "orcmap/stroke.hpp"

namespace orcmap {
namespace m5gfx_adapter {

// M5GFX/LovyanGFX RenderTarget. Map semantics stay in OrcMaps core.
// Optional: consumers include this header and supply their own M5GFX
// dependency. Core sources never include M5GFX.h.
//
// Alpha: ToRgb565 ignores Color.a (opaque RGB565).
// Lines: MapPaint::width_px via shared RasterizeCenteredStroke (1px uses
// LovyanGFX drawLine). FillPolygon: even-odd scanline of the given
// vertices (renderer already passes first path only).

class DisplayTarget : public RenderTarget {
 public:
  explicit DisplayTarget(lgfx::v1::LovyanGFX& display) : display_(display) {}

  int Width() const override { return display_.width(); }
  int Height() const override { return display_.height(); }

  void FillRect(int x, int y, int w, int h, Color color) override {
    if (w <= 0 || h <= 0) return;
    const int width = Width();
    const int height = Height();
    int64_t x0 = x < 0 ? 0 : static_cast<int64_t>(x);
    int64_t y0 = y < 0 ? 0 : static_cast<int64_t>(y);
    int64_t x1 = static_cast<int64_t>(x) + static_cast<int64_t>(w);
    int64_t y1 = static_cast<int64_t>(y) + static_cast<int64_t>(h);
    if (x1 > width) x1 = width;
    if (y1 > height) y1 = height;
    if (x0 >= x1 || y0 >= y1) return;
    display_.fillRect(static_cast<int>(x0), static_cast<int>(y0),
                      static_cast<int>(x1 - x0), static_cast<int>(y1 - y0),
                      ToRgb565(color));
  }

  void DrawPoint(int x, int y, Color color) override {
    if (x < 0 || y < 0 || x >= Width() || y >= Height()) return;
    display_.drawPixel(x, y, ToRgb565(color));
  }

  void DrawLine(int x0, int y0, int x1, int y1, Color color,
                float width_px = 1.0f) override {
    const int raster_w = RasterStrokeWidthPx(width_px);
    if (raster_w <= 0) return;
    // 1px keeps LovyanGFX drawLine (previous adapter behavior). Widths
    // > 1 use the shared square-brush stroke so host and M5GFX match.
    if (raster_w == 1) {
      if (!ClipLineToPixels(&x0, &y0, &x1, &y1, Width(), Height())) return;
      display_.drawLine(x0, y0, x1, y1, ToRgb565(color));
      return;
    }
    RasterizeCenteredStroke(x0, y0, x1, y1, width_px, Width(), Height(),
                            [&](int x, int y, int w, int h) {
                              FillRect(x, y, w, h, color);
                            });
  }

  void FillPolygon(const int* xy_pairs, size_t n_points, Color color) override {
    FillPolygonRings(xy_pairs, &n_points, 1, color);
  }

  // Even-odd scanline across every ring: an exterior ring and its holes, or one ring.
  void FillPolygonRings(const int* xy_pairs, const size_t* ring_point_counts, size_t ring_count,
                        Color color) override {
    if (xy_pairs == nullptr || ring_point_counts == nullptr || ring_count == 0) return;
    size_t n_points = 0;   // total vertices over all rings
    for (size_t r = 0; r < ring_count; ++r) n_points += ring_point_counts[r];
    if (n_points < 3) return;
    const int width = Width();
    const int height = Height();
    if (width <= 0 || height <= 0) return;
    int min_y = xy_pairs[1];
    int max_y = xy_pairs[1];
    int min_x = xy_pairs[0];
    int max_x = xy_pairs[0];
    for (size_t i = 0; i < n_points; ++i) {
      const int px = xy_pairs[i * 2];
      const int py = xy_pairs[i * 2 + 1];
      if (py < min_y) min_y = py;
      if (py > max_y) max_y = py;
      if (px < min_x) min_x = px;
      if (px > max_x) max_x = px;
    }
    if (max_y < 0 || min_y >= height || max_x < 0 || min_x >= width) return;
    if (min_y < 0) min_y = 0;
    if (max_y >= height) max_y = height - 1;
    const uint16_t rgb = ToRgb565(color);
    crossings_.clear();
    crossings_.reserve(n_points);
    display_.startWrite();
    for (int y = min_y; y <= max_y; ++y) {
      crossings_.clear();
      size_t base = 0;   // index of the first vertex of the current ring
      for (size_t r = 0; r < ring_count; ++r) {
        const size_t ring_n = ring_point_counts[r];
        if (ring_n >= 3) {
          for (size_t i = 0; i < ring_n; ++i) {
            const size_t j = (i + 1) % ring_n;
            const int y0 = xy_pairs[(base + i) * 2 + 1];
            const int y1 = xy_pairs[(base + j) * 2 + 1];
            const int x0 = xy_pairs[(base + i) * 2];
            const int x1 = xy_pairs[(base + j) * 2];
            if (y0 == y1) continue;
            const int lo = y0 < y1 ? y0 : y1;
            const int hi = y0 < y1 ? y1 : y0;
            if (y < lo || y >= hi) continue;
            const int64_t num =
                (static_cast<int64_t>(y) - static_cast<int64_t>(y0)) *
                (static_cast<int64_t>(x1) - static_cast<int64_t>(x0));
            const int64_t den =
                static_cast<int64_t>(y1) - static_cast<int64_t>(y0);
            const int64_t x = static_cast<int64_t>(x0) + num / den;
            if (x > static_cast<int64_t>(std::numeric_limits<int>::max())) {
              crossings_.push_back(std::numeric_limits<int>::max());
            } else if (x < static_cast<int64_t>(std::numeric_limits<int>::min())) {
              crossings_.push_back(std::numeric_limits<int>::min());
            } else {
              crossings_.push_back(static_cast<int>(x));
            }
          }
        }
        base += ring_n;
      }
      std::sort(crossings_.begin(), crossings_.end());
      for (size_t k = 0; k + 1 < crossings_.size(); k += 2) {
        int xa = crossings_[k];
        int xb = crossings_[k + 1];
        if (xb < xa) {
          const int t = xa;
          xa = xb;
          xb = t;
        }
        if (xa < 0) xa = 0;
        if (xb >= width) xb = width - 1;
        if (xb < xa) continue;
        display_.drawFastHLine(xa, y, xb - xa + 1, rgb);
      }
    }
    display_.endWrite();
  }

 private:
  lgfx::v1::LovyanGFX& display_;
  std::vector<int> crossings_;
};

}  // namespace m5gfx_adapter
}  // namespace orcmap

#pragma once

#include <cmath>
#include <cstddef>

#include "orcmap/clip.hpp"

namespace orcmap {

// Shared map-stroke contract for every RenderTarget.
//
// RasterStrokeWidthPx:
//   width_px <= 0 or non-finite -> 0 (no draw)
//   0 < width_px <= 1           -> 1 (current one-pixel line)
//   width_px > 1                -> nearest integer, halves round away
//                                  from zero via floor(w+0.5), minimum 2
//
// RasterizeCenteredStroke paints an axis-aligned square brush of that
// integer width, centered on each Bresenham centerline pixel
// (top-left bias for even widths: FillRect(x - w/2, y - w/2, w, w)).
// No round caps, no miter/bevel joins, no dashes.
//
// Thick-line clipping expands the Liang-Barsky rectangle by w/2 so a
// centerline just outside the framebuffer can still stamp visible pixels.

inline constexpr int kMaxRasterStrokePx = 64;

inline int RasterStrokeWidthPx(float width_px) {
  if (!std::isfinite(width_px) || width_px <= 0.0f) return 0;
  if (width_px <= 1.0f) return 1;
  // Single precision on purpose: this runs per drawn segment, and no ESP32
  // FPU implements double. For the positive values that reach here,
  // truncation is floor(width_px + 0.5).
  int w = static_cast<int>(width_px + 0.5f);
  if (w < 2) w = 2;
  if (w > kMaxRasterStrokePx) w = kMaxRasterStrokePx;
  return w;
}

template <typename FillRect>
void RasterizeCenteredStroke(int x0, int y0, int x1, int y1, float width_px,
                             int fb_w, int fb_h, FillRect&& fill_rect) {
  const int w = RasterStrokeWidthPx(width_px);
  if (w <= 0 || fb_w <= 0 || fb_h <= 0) return;
  const int r = w / 2;
  const int xmin = -r;
  const int ymin = -r;
  const int xmax = fb_w - 1 + r;
  const int ymax = fb_h - 1 + r;
  if (!ClipLineToPixelRect(&x0, &y0, &x1, &y1, xmin, ymin, xmax, ymax)) {
    return;
  }

  auto iabs = [](int v) { return v < 0 ? -v : v; };
  int dx = iabs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -iabs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  // Run coalescing. The union of w x w brushes centred on consecutive
  // pixels of an AXIS-ALIGNED run is exactly one rectangle:
  //
  //   horizontal run x=a..b at constant y -> (a-r, y-r, (b-a+1)+w-1, w)
  //   vertical   run y=a..b at constant x -> (x-r, a-r, w, (b-a+1)+w-1)
  //
  // so emitting one rect per run paints precisely the same pixels as one
  // rect per pixel -- this is a pure speed change, not an approximation.
  // A DIAGONAL step breaks the identity (the union of two diagonally
  // adjacent squares is not a rectangle), so any step that moves in both
  // axes flushes the run first.
  //
  // This matters because roads are the widest, most numerous geometry:
  // orcsdr-dark draws motorways at 3 px and primaries at 2.5 px, so before
  // this every road pixel cost a separate FillRect call into the graphics
  // adapter.
  int run_x0 = x0;
  int run_y0 = y0;
  int run_x1 = x0;
  int run_y1 = y0;
  const auto flush = [&]() {
    const int left = (run_x0 < run_x1 ? run_x0 : run_x1) - r;
    const int top = (run_y0 < run_y1 ? run_y0 : run_y1) - r;
    const int span_x = iabs(run_x1 - run_x0) + 1;
    const int span_y = iabs(run_y1 - run_y0) + 1;
    fill_rect(left, top, span_x + w - 1, span_y + w - 1);
  };

  for (;;) {
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    const int prev_x = x0;
    const int prev_y = y0;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
    const bool moved_x = x0 != prev_x;
    const bool moved_y = y0 != prev_y;
    if (moved_x && !moved_y && run_y0 == run_y1) {
      run_x1 = x0;  // extend horizontal run
    } else if (moved_y && !moved_x && run_x0 == run_x1) {
      run_y1 = y0;  // extend vertical run
    } else {
      flush();
      run_x0 = run_x1 = x0;
      run_y0 = run_y1 = y0;
    }
  }
  flush();
}

}  // namespace orcmap

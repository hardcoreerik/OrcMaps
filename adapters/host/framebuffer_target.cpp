#include "framebuffer_target.hpp"

#include "orcmap/stroke.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace orcmap {
namespace host {

FramebufferTarget::FramebufferTarget(int width, int height, Color clear)
    : width_(width < 0 ? 0 : width),
      height_(height < 0 ? 0 : height),
      clip_{0, 0, width_, height_},
      pixels_(static_cast<size_t>(width_) * static_cast<size_t>(height_),
              clear) {}

bool FramebufferTarget::SetClipRect(PixelRect rect) {
  if (rect.width < 0 || rect.height < 0) return false;
  clip_ = IntersectPixelRects({0, 0, width_, height_}, rect);
  return true;
}

void FramebufferTarget::Put(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return;
  if (x < clip_.x || y < clip_.y || int64_t{x} >= int64_t{clip_.x} + clip_.width ||
      int64_t{y} >= int64_t{clip_.y} + clip_.height) return;
  pixels_[static_cast<size_t>(y) * static_cast<size_t>(width_) +
          static_cast<size_t>(x)] = color;
}

Color FramebufferTarget::At(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) return Color{};
  return pixels_[static_cast<size_t>(y) * static_cast<size_t>(width_) +
                 static_cast<size_t>(x)];
}

void FramebufferTarget::DrawPoint(int x, int y, Color color) { Put(x, y, color); }

void FramebufferTarget::FillRect(int x, int y, int w, int h, Color color) {
  if (w <= 0 || h <= 0) return;
  int64_t x0 = x < 0 ? 0 : static_cast<int64_t>(x);
  int64_t y0 = y < 0 ? 0 : static_cast<int64_t>(y);
  int64_t x1 = static_cast<int64_t>(x) + static_cast<int64_t>(w);
  int64_t y1 = static_cast<int64_t>(y) + static_cast<int64_t>(h);
  if (x1 > width_) x1 = width_;
  if (y1 > height_) y1 = height_;
  if (x0 >= x1 || y0 >= y1) return;
  for (int64_t py = y0; py < y1; ++py) {
    for (int64_t px = x0; px < x1; ++px) {
      Put(static_cast<int>(px), static_cast<int>(py), color);
    }
  }
}

void FramebufferTarget::DrawLine(int x0, int y0, int x1, int y1, Color color,
                                 float width_px) {
  RasterizeCenteredStroke(x0, y0, x1, y1, width_px, width_, height_,
                          [&](int x, int y, int w, int h) {
                            FillRect(x, y, w, h, color);
                          });
}

void FramebufferTarget::FillPolygon(const int* xy_pairs, size_t n_points,
                                    Color color) {
  FillPolygonRings(xy_pairs, &n_points, 1, color);
}

void FramebufferTarget::FillPolygonRings(const int* xy_pairs, const size_t* ring_point_counts,
                                         size_t ring_count, Color color) {
  if (xy_pairs == nullptr || ring_point_counts == nullptr || ring_count == 0) return;
  if (width_ <= 0 || height_ <= 0) return;
  size_t total_points = 0;
  for (size_t r = 0; r < ring_count; ++r) total_points += ring_point_counts[r];
  if (total_points < 3) return;
  int min_y = xy_pairs[1];
  int max_y = xy_pairs[1];
  int min_x = xy_pairs[0];
  int max_x = xy_pairs[0];
  for (size_t i = 0; i < total_points; ++i) {
    const int x = xy_pairs[i * 2];
    const int y = xy_pairs[i * 2 + 1];
    if (y < min_y) min_y = y;
    if (y > max_y) max_y = y;
    if (x < min_x) min_x = x;
    if (x > max_x) max_x = x;
  }
  if (max_y < 0 || min_y >= height_ || max_x < 0 || min_x >= width_) return;
  if (min_y < 0) min_y = 0;
  if (max_y >= height_) max_y = height_ - 1;
  std::vector<int> crossings;
  crossings.reserve(total_points);
  for (int y = min_y; y <= max_y; ++y) {
    crossings.clear();
    size_t base = 0;   // index of the first vertex of the current ring
    for (size_t r = 0; r < ring_count; ++r) {
      const size_t n_points = ring_point_counts[r];
      if (n_points >= 3) {
        for (size_t i = 0; i < n_points; ++i) {
          const size_t j = (i + 1) % n_points;
          const int y0 = xy_pairs[(base + i) * 2 + 1];
          const int y1 = xy_pairs[(base + j) * 2 + 1];
          const int x0 = xy_pairs[(base + i) * 2];
          const int x1 = xy_pairs[(base + j) * 2];
          if (y0 == y1) continue;
          const int lo = y0 < y1 ? y0 : y1;
          const int hi = y0 < y1 ? y1 : y0;
          if (y < lo || y >= hi) continue;
          const int64_t num = (static_cast<int64_t>(y) - static_cast<int64_t>(y0)) *
                              (static_cast<int64_t>(x1) - static_cast<int64_t>(x0));
          const int64_t den = static_cast<int64_t>(y1) - static_cast<int64_t>(y0);
          const int64_t x = static_cast<int64_t>(x0) + num / den;
          if (x > static_cast<int64_t>(std::numeric_limits<int>::max())) {
            crossings.push_back(std::numeric_limits<int>::max());
          } else if (x < static_cast<int64_t>(std::numeric_limits<int>::min())) {
            crossings.push_back(std::numeric_limits<int>::min());
          } else {
            crossings.push_back(static_cast<int>(x));
          }
        }
      }
      base += n_points;
    }
    std::sort(crossings.begin(), crossings.end());
    for (size_t k = 0; k + 1 < crossings.size(); k += 2) {
      int xa = crossings[k];
      int xb = crossings[k + 1];
      if (xb < xa) {
        const int t = xa;
        xa = xb;
        xb = t;
      }
      if (xa < 0) xa = 0;
      if (xb >= width_) xb = width_ - 1;
      if (xb < xa) continue;
      for (int x = xa; x <= xb; ++x) Put(x, y, color);
    }
  }
}

bool FramebufferTarget::WritePpm(const std::string& path) const {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  std::fprintf(f, "P6\n%d %d\n255\n", width_, height_);
  for (const Color& c : pixels_) {
    const unsigned char rgb[3] = {c.r, c.g, c.b};
    if (std::fwrite(rgb, 1, 3, f) != 3) {
      std::fclose(f);
      return false;
    }
  }
  std::fclose(f);
  return true;
}

}  // namespace host
}  // namespace orcmap

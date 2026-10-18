#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "orcmap/render_target.hpp"

namespace orcmap {
namespace host {

// Host-only RGBA8 framebuffer RenderTarget. No M5GFX/LVGL. Used by host
// tests and as the architecture proof that OrcMaps can produce pixels
// without a graphics framework. Not compiled into the ESP-IDF component.

class FramebufferTarget : public RenderTarget {
 public:
  FramebufferTarget(int width, int height, Color clear = Color::Rgb(0, 0, 0));

  int Width() const override { return width_; }
  int Height() const override { return height_; }
  PixelRect ClipRect() const override { return clip_; }
  bool SetClipRect(PixelRect rect) override;

  void FillRect(int x, int y, int w, int h, Color color) override;
  void DrawPoint(int x, int y, Color color) override;
  void DrawLine(int x0, int y0, int x1, int y1, Color color,
                float width_px = 1.0f) override;
  void FillPolygon(const int* xy_pairs, size_t n_points, Color color) override;
  void FillPolygonRings(const int* xy_pairs, const size_t* ring_point_counts, size_t ring_count,
                        Color color) override;

  Color At(int x, int y) const;
  const std::vector<Color>& Pixels() const { return pixels_; }

  // Optional P6 PPM dump for human inspection. Returns false on I/O error.
  bool WritePpm(const std::string& path) const;

 private:
  void Put(int x, int y, Color color);

  int width_ = 0;
  int height_ = 0;
  PixelRect clip_;
  std::vector<Color> pixels_;
};

}  // namespace host
}  // namespace orcmap

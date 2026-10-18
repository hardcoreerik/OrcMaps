#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include "framebuffer_target.hpp"
#include "orcmap/labels.hpp"

namespace orcmap::host {
// Optional host preview adapter. FreeType is never linked into the firmware.
class FreeTypeLabelTarget final : public LabelTextTarget {
 public:
  FreeTypeLabelTarget(FramebufferTarget& target, const char* font_path, unsigned pixels = 24);
  ~FreeTypeLabelTarget() override;
  FreeTypeLabelTarget(const FreeTypeLabelTarget&) = delete;
  FreeTypeLabelTarget& operator=(const FreeTypeLabelTarget&) = delete;
  bool valid() const { return face_ != nullptr; }
  bool MeasureText(std::string_view text, TextExtent* extent) override;
  void DrawText(int x, int y, std::string_view text, Color fg, Color halo) override;
 private:
  bool Walk(std::string_view text, bool paint, int x, int y, Color color);
  FramebufferTarget& target_;
  FT_Library library_ = nullptr;
  FT_Face face_ = nullptr;
  int min_x_ = 0, min_y_ = 0, max_x_ = 0, max_y_ = 0;
};
}  // namespace orcmap::host

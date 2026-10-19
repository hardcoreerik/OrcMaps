#include "freetype_label_target.hpp"
#include "orcmap/simple_text.hpp"
#include <algorithm>
#include <cstdlib>

namespace orcmap::host {
FreeTypeLabelTarget::FreeTypeLabelTarget(FramebufferTarget& target, const char* path, unsigned pixels)
    : target_(target) {
  if (path == nullptr || pixels == 0 || pixels > 128 || FT_Init_FreeType(&library_)) return;
  if (FT_New_Face(library_, path, 0, &face_) || FT_Set_Pixel_Sizes(face_, 0, pixels)) {
    if (face_) FT_Done_Face(face_);
    face_ = nullptr;
  }
}
FreeTypeLabelTarget::~FreeTypeLabelTarget() {
  if (face_) FT_Done_Face(face_);
  if (library_) FT_Done_FreeType(library_);
}
bool FreeTypeLabelTarget::Walk(std::string_view text, bool paint, int x, int y, Color color) {
  if (!face_ || text.size() > LabelLayout::kNameBytes || !IsSimpleLabelText(text)) return false;
  int pen = 0;
  const int ascent = static_cast<int>(face_->size->metrics.ascender / 64);
  FT_UInt previous = 0;
  size_t cursor = 0;
  uint32_t cp;
  while (cursor < text.size()) {
    if (!NextLabelCodepoint(text, &cursor, &cp)) return false;
    const FT_UInt glyph = FT_Get_Char_Index(face_, cp);
    if (!glyph) return false;
    if (previous && FT_HAS_KERNING(face_)) {
      FT_Vector kern{};
      if (FT_Get_Kerning(face_, previous, glyph, FT_KERNING_DEFAULT, &kern)) return false;
      pen += static_cast<int>(kern.x / 64);
    }
    if (FT_Load_Glyph(face_, glyph, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) return false;
    const auto slot = face_->glyph;
    const int left = pen + slot->bitmap_left, top = ascent - slot->bitmap_top;
    if (!paint) {
      min_x_ = std::min(min_x_, left); min_y_ = std::min(min_y_, top);
      max_x_ = std::max(max_x_, left + static_cast<int>(slot->bitmap.width));
      max_y_ = std::max(max_y_, top + static_cast<int>(slot->bitmap.rows));
    } else {
      if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_GRAY && slot->bitmap.rows) return false;
      for (unsigned row = 0; row < slot->bitmap.rows; ++row) {
        const int source_row = slot->bitmap.pitch >= 0 ? static_cast<int>(row)
                            : static_cast<int>(slot->bitmap.rows - row - 1);
        const auto* bytes = slot->bitmap.buffer + source_row * std::abs(slot->bitmap.pitch);
        for (unsigned col = 0; col < slot->bitmap.width; ++col) {
          const unsigned alpha = bytes[col];
          if (!alpha) continue;
          const int px = x + left - min_x_ + static_cast<int>(col);
          const int py = y + top - min_y_ + static_cast<int>(row);
          const auto bg = target_.At(px, py);
          auto mix = [alpha](uint8_t a, uint8_t b) {
            return static_cast<uint8_t>((a * alpha + b * (255 - alpha) + 127) / 255);
          };
          target_.DrawPoint(px, py, Color::Rgb(mix(color.r, bg.r), mix(color.g, bg.g), mix(color.b, bg.b)));
        }
      }
    }
    pen += static_cast<int>(slot->advance.x / 64);
    if (!paint) max_x_ = std::max(max_x_, pen);
    previous = glyph;
  }
  return true;
}
bool FreeTypeLabelTarget::MeasureText(std::string_view text, TextExtent* extent) {
  if (!extent || !face_) return false;
  min_x_ = min_y_ = max_x_ = 0;
  max_y_ = static_cast<int>(face_->size->metrics.height / 64);
  if (!Walk(text, false, 0, 0, {})) return false;
  *extent = {max_x_ - min_x_, max_y_ - min_y_};
  return extent->width > 0 && extent->height > 0;
}
void FreeTypeLabelTarget::DrawText(int x, int y, std::string_view text, Color fg, Color halo) {
  TextExtent extent;
  if (!MeasureText(text, &extent)) return;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx)
      if (dx || dy) Walk(text, true, x + dx, y + dy, halo);
  Walk(text, true, x, y, fg);
}
}  // namespace orcmap::host

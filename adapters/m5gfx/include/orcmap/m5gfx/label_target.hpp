#pragma once

#include <cstring>
#include "orcmap/m5gfx/display_target.hpp"
#include "orcmap/simple_text.hpp"

namespace orcmap::m5gfx_adapter {

// Use on the private map canvas, after basemap rendering has restored its
// tile scissor. Fixed-size font, transparent halo, no change to global UI state.
class LabelTarget final : public LabelTextTarget {
 public:
  LabelTarget(lgfx::v1::LovyanGFX& display, const lgfx::v1::IFont& font)
      : display_(display), font_(font), saved_font_(display.getFont()),
        saved_style_(display.getTextStyle()) {
    display_.setFont(&font_);
    display_.setTextSize(1);
    display_.setTextDatum(lgfx::v1::textdatum_t::top_left);
  }
  LabelTarget(const LabelTarget&) = delete;
  LabelTarget& operator=(const LabelTarget&) = delete;
  ~LabelTarget() override {
    display_.setFont(saved_font_);
    display_.setTextStyle(saved_style_);
  }
  bool MeasureText(std::string_view text, TextExtent* extent) override {
    if (extent == nullptr || text.size() > LabelLayout::kNameBytes ||
        !IsSimpleLabelText(text)) return false;
    lgfx::v1::FontMetrics metrics;
    font_.getDefaultMetric(&metrics);
    size_t cursor = 0;
    uint32_t cp;
    while (cursor < text.size()) {
      if (!NextLabelCodepoint(text, &cursor, &cp) || cp > 0xffff ||
          !font_.updateFontMetric(&metrics, static_cast<uint16_t>(cp))) return false;
    }
    Copy(text);
    *extent = {display_.textWidth(buffer_, &font_), display_.fontHeight(&font_)};
    return extent->width > 0 && extent->height > 0;
  }
  void DrawText(int x, int y, std::string_view text, Color fg, Color halo) override {
    TextExtent extent;
    if (!MeasureText(text, &extent)) return;
    display_.setTextColor(ToRgb565(halo));
    for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx)
        if (dx || dy) display_.drawString(buffer_, x + dx, y + dy);
    display_.setTextColor(ToRgb565(fg));
    display_.drawString(buffer_, x, y);
  }
 private:
  void Copy(std::string_view text) {
    std::memcpy(buffer_, text.data(), text.size()); buffer_[text.size()] = 0;
  }
  lgfx::v1::LovyanGFX& display_;
  const lgfx::v1::IFont& font_;
  const lgfx::v1::IFont* saved_font_;
  lgfx::v1::TextStyle saved_style_;
  char buffer_[LabelLayout::kNameBytes + 1]{};
};
}  // namespace orcmap::m5gfx_adapter

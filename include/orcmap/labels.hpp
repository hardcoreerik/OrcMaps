#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "orcmap/color.hpp"
#include "orcmap/feature.hpp"

namespace orcmap {
struct TileScreenMap;
struct MapStyle;

std::string_view PreferredPlaceName(const Feature& feature);
// Advances only on a valid Unicode scalar suitable for a single-line label.
bool NextLabelCodepoint(std::string_view text, size_t* cursor, uint32_t* codepoint);

struct TextExtent { int width = 0, height = 0; };
class LabelTextTarget {
 public:
  virtual ~LabelTextTarget() = default;
  // False means some glyph cannot be rendered; never measure replacement boxes.
  virtual bool MeasureText(std::string_view text, TextExtent* extent) = 0;
  virtual void DrawText(int x, int y, std::string_view text,
                        Color foreground, Color halo) = 0;
};

struct LabelStats {
  uint32_t offered = 0, invalid_names = 0, oversized_names = 0;
  uint32_t duplicates = 0, pool_overflow = 0, offscreen = 0;
  uint32_t overlap = 0, unsupported_glyphs = 0, draw_limit = 0, drawn = 0;
};

// Fixed storage; P4 callers must place this object in PSRAM, not a task stack
// or internal BSS. The text backend owns its font and any backend allocations.
class LabelLayout {
 public:
  static constexpr size_t kCandidates = 128, kDrawLimit = 64, kNameBytes = 95;
  void BeginFrame(int width, int height);
  bool Offer(std::string_view text, int anchor_x, int anchor_y,
              int32_t priority, uint64_t stable_id);
  // Shared place extraction/projection for streaming and host consumers.
  void OfferPlace(const Feature& feature, const TileScreenMap& map,
                   uint8_t zoom, const MapStyle& style);
  void Draw(LabelTextTarget& target, Color foreground, Color halo);
  const LabelStats& stats() const { return stats_; }
 private:
  struct Candidate {
    std::array<char, kNameBytes + 1> text{};
    uint64_t id = 0;
    int32_t priority = 0;
    int x = 0, y = 0;
    uint8_t length = 0;
  };
  static bool Better(const Candidate& a, const Candidate& b);
  std::array<Candidate, kCandidates> candidates_{};
  size_t count_ = 0;
  int width_ = 0, height_ = 0;
  uint32_t offered_offscreen_ = 0;
  LabelStats stats_;
};

}  // namespace orcmap

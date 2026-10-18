#include "orcmap/labels.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>
#include "orcmap/style.hpp"
#include "orcmap/viewport.hpp"

namespace orcmap {

std::string_view PreferredPlaceName(const Feature& feature) {
  for (const char* key : {"name:en", "name_en", "name"}) {
    const auto* value = FindProperty(feature, key);
    if (value == nullptr) continue;
    const auto* text = std::get_if<std::string>(value);
    if (text != nullptr && !text->empty()) return *text;
  }
  return {};
}

bool NextLabelCodepoint(std::string_view text, size_t* cursor, uint32_t* codepoint) {
  if (cursor == nullptr || codepoint == nullptr || *cursor >= text.size()) return false;
  size_t index = *cursor;
  const uint8_t first = static_cast<uint8_t>(text[index++]);
  uint32_t cp;
  int continuation;
  uint32_t minimum;
  if (first < 0x80) { cp = first; continuation = 0; minimum = 0; }
  else if (first >= 0xc2 && first <= 0xdf) { cp = first & 31; continuation = 1; minimum = 0x80; }
  else if (first >= 0xe0 && first <= 0xef) { cp = first & 15; continuation = 2; minimum = 0x800; }
  else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; continuation = 3; minimum = 0x10000; }
  else return false;
  if (text.size() - index < static_cast<size_t>(continuation)) return false;
  for (int i = 0; i < continuation; ++i) {
    const uint8_t next = static_cast<uint8_t>(text[index++]);
    if ((next & 0xc0) != 0x80) return false;
    cp = (cp << 6) | (next & 63);
  }
  if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) ||
      cp < 0x20 || (cp >= 0x7f && cp <= 0x9f) || cp == 0x2028 || cp == 0x2029) return false;
  *cursor = index;
  *codepoint = cp;
  return true;
}

void LabelLayout::BeginFrame(int width, int height) {
  width_ = std::max(0, width);
  height_ = std::max(0, height);
  count_ = 0;
  offered_offscreen_ = 0;
  stats_ = {};
}

bool LabelLayout::Better(const Candidate& a, const Candidate& b) {
  if (a.priority != b.priority) return a.priority > b.priority;
  if (a.id != b.id) return a.id < b.id;
  const std::string_view at(a.text.data(), a.length), bt(b.text.data(), b.length);
  if (at != bt) return at < bt;
  if (a.x != b.x) return a.x < b.x;
  return a.y < b.y;
}

bool LabelLayout::Offer(std::string_view text, int x, int y,
                         int32_t priority, uint64_t stable_id) {
  ++stats_.offered;
  if (text.size() > kNameBytes) { ++stats_.oversized_names; return false; }
  if (text.empty()) { ++stats_.invalid_names; return false; }
  size_t cursor = 0;
  uint32_t cp;
  while (cursor < text.size()) {
    if (!NextLabelCodepoint(text, &cursor, &cp)) { ++stats_.invalid_names; return false; }
  }
  if (x < 0 || y < 0 || x >= width_ || y >= height_) {
    ++offered_offscreen_;
    ++stats_.offscreen;
    return false;
  }
  Candidate candidate;
  std::memcpy(candidate.text.data(), text.data(), text.size());
  candidate.length = static_cast<uint8_t>(text.size());
  candidate.x = x; candidate.y = y;
  candidate.priority = priority; candidate.id = stable_id;
  for (size_t i = 0; i < count_; ++i) {
    if (candidates_[i].x == x && candidates_[i].y == y &&
        std::string_view(candidates_[i].text.data(), candidates_[i].length) == text) {
      ++stats_.duplicates;
      if (Better(candidate, candidates_[i])) candidates_[i] = candidate;
      return true;
    }
  }
  if (count_ < kCandidates) { candidates_[count_++] = candidate; return true; }
  ++stats_.pool_overflow;
  size_t worst = 0;
  for (size_t i = 1; i < count_; ++i)
    if (Better(candidates_[worst], candidates_[i])) worst = i;
  if (!Better(candidate, candidates_[worst])) return false;
  candidates_[worst] = candidate;
  return true;
}

void LabelLayout::OfferPlace(const Feature& feature, const TileScreenMap& map,
                              uint8_t zoom, const MapStyle& style) {
  if (zoom > 31 || !feature.kind_assigned || feature.geometry.type != GeomType::kPoint ||
      (feature.layer != "place" && feature.layer != "places" && feature.layer != "place_label") ||
      (feature.kind != FeatureKind::kLabelPrimary && feature.kind != FeatureKind::kLabelSecondary &&
       feature.kind != FeatureKind::kLabelMuted) ||
      !ResolveFeatureStyle(feature.kind, zoom, style).visible) return;
  auto number = [&feature](const char* key, double fallback) {
    const auto* value = FindProperty(feature, key);
    if (value) {
      if (auto* n = std::get_if<double>(value)) return std::isfinite(*n) ? *n : fallback;
      if (auto* n = std::get_if<int64_t>(value)) return static_cast<double>(*n);
      if (auto* n = std::get_if<uint64_t>(value)) return static_cast<double>(*n);
    }
    return fallback;
  };
  if (number("min_zoom", 0) > zoom) return;
  const double population_rank = number("population_rank", -1);
  const double rank = population_rank >= 0 ? population_rank
      : 100 - number("rank", number("scalerank", 100));
  const int priority = (feature.kind == FeatureKind::kLabelPrimary ? 20000 : 10000) +
                        static_cast<int>(std::clamp(rank, 0.0, 100.0) * 100);
  const auto name = PreferredPlaceName(feature);
  const uint32_t extent = feature.extent ? feature.extent : 4096;
  for (const auto& path : feature.geometry.paths) {
    for (const auto& point : path) {
      if (point.x < 0 || point.y < 0 || static_cast<uint32_t>(point.x) >= extent ||
          static_cast<uint32_t>(point.y) >= extent) continue;
      int x, y;
      ProjectLocal(map, point.x, point.y, &x, &y);
      Offer(name, x, y, priority, feature.id);
    }
  }
}

void LabelLayout::Draw(LabelTextTarget& target, Color foreground, Color halo) {
  stats_.offscreen = offered_offscreen_;
  stats_.drawn = stats_.overlap = stats_.unsupported_glyphs = stats_.draw_limit = 0;
  std::sort(candidates_.begin(), candidates_.begin() + count_, Better);
  struct Rect { int64_t left, top, right, bottom; };
  std::array<Rect, kDrawLimit> placed{};
  constexpr int padding = 4;
  for (size_t i = 0; i < count_; ++i) {
    if (stats_.drawn == kDrawLimit) {
      stats_.draw_limit = static_cast<uint32_t>(count_ - i);
      break;
    }
    const auto& candidate = candidates_[i];
    const std::string_view text(candidate.text.data(), candidate.length);
    TextExtent extent;
    if (!target.MeasureText(text, &extent) || extent.width <= 0 || extent.height <= 0) {
      ++stats_.unsupported_glyphs;
      continue;
    }
    const int64_t x = int64_t{candidate.x} - extent.width / 2;
    const int64_t y = int64_t{candidate.y} + padding;
    const Rect box{x - padding, y - padding, x + extent.width + padding,
                    y + extent.height + padding};
    if (box.left < 0 || box.top < 0 || box.right > width_ || box.bottom > height_) {
      ++stats_.offscreen;
      continue;
    }
    bool overlaps = false;
    for (size_t j = 0; j < stats_.drawn; ++j)
      overlaps |= box.left < placed[j].right && box.right > placed[j].left &&
                  box.top < placed[j].bottom && box.bottom > placed[j].top;
    if (overlaps) { ++stats_.overlap; continue; }
    target.DrawText(static_cast<int>(x), static_cast<int>(y), text, foreground, halo);
    placed[stats_.drawn++] = box;
  }
}

}  // namespace orcmap

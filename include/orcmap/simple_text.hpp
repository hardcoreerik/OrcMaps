#pragma once
#include "orcmap/labels.hpp"

namespace orcmap {
// Capability of the initial non-shaping adapters, not a restriction on the
// layout API. Other text targets may support additional scripts/shaping.
inline bool IsSimpleLabelText(std::string_view text) {
  size_t cursor = 0;
  uint32_t cp;
  if (text.empty()) return false;
  while (cursor < text.size()) {
    if (!NextLabelCodepoint(text, &cursor, &cp)) return false;
    const bool simple = cp <= 0x02af || (cp >= 0x0370 && cp <= 0x058f) ||
        (cp >= 0x10a0 && cp <= 0x10ff) || (cp >= 0x1e00 && cp <= 0x1eff) ||
        (cp >= 0x2000 && cp <= 0x2027 && !(cp >= 0x200b && cp <= 0x200f)) ||
        (cp >= 0x2030 && cp <= 0x205e) || (cp >= 0x2e80 && cp <= 0x3098) ||
        (cp >= 0x309b && cp <= 0x30ff) || (cp >= 0x31f0 && cp <= 0x31ff) ||
        (cp >= 0x3400 && cp <= 0x9fff) || (cp >= 0xac00 && cp <= 0xd7af) ||
        (cp >= 0xf900 && cp <= 0xfaff) || (cp >= 0xff00 && cp <= 0xffef);
    if (!simple) return false;
  }
  return true;
}
}  // namespace orcmap

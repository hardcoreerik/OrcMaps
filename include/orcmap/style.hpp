#pragma once

#include <cstddef>
#include <cstdint>

#include "orcmap/color.hpp"
#include "orcmap/feature_kind.hpp"

namespace orcmap {

// Base-map style system. See docs/STYLING.md for the full design rationale
// -- this header is the API surface it describes.
//
// Hard separation this file enforces: a MapStyle knows how to paint
// *categories* of base-map geography (FeatureKind in feature_kind.hpp).
// FeatureKind is not defined here -- features own semantic identity,
// styles own appearance. This file knows nothing about ADS-B aircraft,
// LoRa nodes, or any other application overlay -- those are drawn through
// the implemented, independent orcmap/overlay.hpp API using their
// own colors. See "Overlays are NOT base-map styles" in docs/STYLING.md.

// One feature category's visual rule. Zoom-gating lives here (min_zoom/
// max_zoom), not as a separate parallel table, so a style file (built-in
// or, later, external -- see docs/STYLING.md "External style files") is
// the single source of truth for both appearance and visibility of a
// category.
struct FeatureRule {
  Color color{};
  float width_px = 1.0f;  // Line width; unused for label/fill categories.
  uint8_t min_zoom = 0;
  uint8_t max_zoom = 24;
  bool visible = true;
};

// A complete, named, versioned base-map style. `version` is part of the
// rendered-tile cache key (orcmap::RenderedTileCacheKey, cache_key.hpp) --
// bump it whenever a built-in style's rules change, so previously cached
// renders under the old visuals are never silently reused. `id` is a
// short, stable, filesystem/JSON-safe identifier (e.g. "orcsdr-dark");
// external style files will use the same id namespace later.
struct MapStyle {
  const char* id = "";
  const char* display_name = "";
  uint32_t version = 1;

  FeatureRule rules[static_cast<size_t>(FeatureKind::kCount)]{};

  float label_scale = 1.0f;
  int label_priority_threshold = 0;

  const FeatureRule& Rule(FeatureKind kind) const {
    return rules[static_cast<size_t>(kind)];
  }
};

// A style rule resolved for one draw call -- what the renderer actually
// consumes. Splitting this from FeatureRule keeps the zoom/visibility
// *policy* (FeatureRule, part of MapStyle) separate from the *resolved
// answer* for a specific (feature, zoom) pair, which is where a future
// per-feature-attribute resolver (e.g. road name affecting label
// priority) would hook in without changing MapStyle's shape.
struct MapPaint {
  bool visible = false;
  Color color{};
  float width_px = 1.0f;
};

// Resolves how one feature category should be painted at a given zoom.
// This is the one function renderer code should call instead of reading
// style.rules[...] directly (see docs/STYLING.md "Renderer requirements")
// -- keeping a single resolution point is what lets a later per-feature
// resolver (e.g. varying color by a road's `class` attribute, not just its
// FeatureKind) extend this without every call site changing.
MapPaint ResolveFeatureStyle(FeatureKind kind, uint8_t zoom,
                              const MapStyle& style);

// The four initial built-in styles (docs/STYLING.md). Returned by const
// reference to a static instance -- no per-call allocation, safe to call
// from a render loop.
enum class BuiltinStyle : uint8_t {
  kOrcSdrDark = 0,
  kStandardLight,
  kHighContrastField,
  kNightRedSafe,
  kCount,
};

namespace styles {
const MapStyle& OrcSdrDark();
const MapStyle& StandardLight();
const MapStyle& HighContrastField();
const MapStyle& NightRedSafe();
}  // namespace styles

// Looks up a built-in style by enum. Never fails (BuiltinStyle is a closed
// enum of known-good styles); out-of-range input is impossible without
// deliberately misusing the enum, but if it happens this falls back to
// StandardLight -- the neutral, non-application-specific default, per
// docs/STYLING.md.
const MapStyle& GetBuiltinStyle(BuiltinStyle which);

// Looks up a built-in style by its `id` string (e.g. for a future
// Settings UI or an external-style-file `id` cross-reference). Returns
// nullptr if no built-in style has that id -- callers must handle this by
// keeping the previous style active, not by crashing or silently
// defaulting, per docs/STYLING.md "invalid style gracefully falls back".
const MapStyle* FindBuiltinStyleById(const char* id);

// OrcMaps' own generic, application-neutral default -- NOT the OrcSDR
// default. OrcSDR explicitly selects BuiltinStyle::kOrcSdrDark at startup
// (docs/STYLING.md "OrcSDR-specific styling guidance"); an unrelated
// OrcMaps consumer that never calls SetStyle() gets this instead.
inline const MapStyle& DefaultStyle() { return styles::StandardLight(); }

// Tracks the currently active style for one map view and hands out the
// version/id pair the cache layer needs. Deliberately tiny: switching
// styles never touches map-pack data (docs/STYLING.md "runtime style
// switching must not reload geographic data"), it only changes which
// MapStyle ResolveFeatureStyle() and the cache-key builder use.
class StyleManager {
 public:
  StyleManager() : active_(&styles::StandardLight()) {}

  void SetStyle(BuiltinStyle which) { active_ = &GetBuiltinStyle(which); }

  // Returns false (and leaves the active style unchanged) if `id` doesn't
  // match any built-in style -- see FindBuiltinStyleById().
  bool SetStyleById(const char* id) {
    const MapStyle* found = FindBuiltinStyleById(id);
    if (found == nullptr) return false;
    active_ = found;
    return true;
  }

  const MapStyle& Active() const { return *active_; }

 private:
  const MapStyle* active_;
};

}  // namespace orcmap

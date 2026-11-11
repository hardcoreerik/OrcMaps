#pragma once

#include <cstdint>

namespace orcmap {

// Semantic category of a base-map feature. This is identity, not appearance:
// Feature carries a FeatureKind, and MapStyle decides how that kind looks.
// Geometry type (point/line/polygon) is a separate fact -- a polygon is not
// automatically water, a building, or a park.
//
// The list is driven by what ORCMAP1 already drew (road/water/airport/label)
// plus categories a standard vector schema (OpenMapTiles/Shortbread) commonly
// exposes -- see docs/FORMAT_DECISION.md "Deferred: tile content schema".
// Adding a category here is a deliberate, reviewed change (every style must
// define a rule for it), not a place to casually bolt on one-off flags.

enum class FeatureKind : uint8_t {
  kBackground = 0,
  kLand,
  kWater,
  kMotorway,
  kPrimaryRoad,
  kSecondaryRoad,
  kMinorRoad,
  kRail,
  kBoundary,
  kBuilding,
  kPark,
  kAirport,
  kLabelPrimary,    // e.g. city/place names
  kLabelSecondary,  // e.g. smaller settlements
  kLabelMuted,      // e.g. attribution, minor annotations
  kBoundaryCountry,
  kBoundaryRegion,
  kBoundaryLocal,   // Local administrative units; not universally counties.
  kCount,
};

}  // namespace orcmap

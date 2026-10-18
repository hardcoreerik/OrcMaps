#pragma once

#include <string_view>

#include "orcmap/feature.hpp"

namespace orcmap {
namespace experimental {

// EXPERIMENTAL / NOT STABLE PUBLIC API.
//
// Temporary layer-name + property heuristic so tests and the Springfield
// host preview can attach FeatureKind. Mappings below were extended from
// measured OpenMapTiles 3.16 tiles (Planetiler) over Springfield / 97477.
// This is still not the OrcMaps tile-content schema -- see
// docs/FORMAT_DECISION.md "Deferred: tile content schema".
//
// Replace this file without changing Feature, Geometry, FeatureTile, the
// style system, Viewport, or graphics integrations.
//
// Unrecognized features are left with kind_assigned == false. This is not
// a production classifier and must not be treated as the OrcMaps schema.

// Returns true and writes `kind` if the heuristic recognizes `feature`.
bool TryClassifyFeature(const Feature& feature, FeatureKind* kind);

// Streaming equivalent of AssignFeatureKindsForProfile. Unsupported profiles,
// unrecognized features and null outputs fail without changing the output.
bool TryClassifyFeatureForProfile(std::string_view schema_version,
                                  const Feature& feature, FeatureKind* kind);

// Best-effort: sets kind/kind_assigned only on features this heuristic
// recognizes. Returns false if `tile` is null; empty tiles succeed.
bool AssignFeatureKinds(FeatureTile* tile);

// Supported input profiles: openmaptiles-3.16, orcmaps-overview-1, protomaps-v4.
// Protomaps uses kind/kind_detail; original properties/names remain untouched.
// Applies the classifier for one supported MVT input profile. Unknown profiles
// and null tiles fail without modifying the tile.
bool AssignFeatureKindsForProfile(std::string_view schema_version,
                                  FeatureTile* tile);

// MVT layer include callback for the current no-text basemap. Skips
// layers that only feed kLabel* (not drawn) or unused name plates.
// Not a schema freeze. Safe as DecodeMvtTile's include_layer.
bool IncludeNoTextBasemapLayer(const char* name, size_t name_len, void* ctx);

}  // namespace experimental
}  // namespace orcmap

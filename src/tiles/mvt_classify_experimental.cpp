#include "orcmap/experimental/mvt_classify.hpp"

#include <cstring>
#include <cmath>

namespace orcmap {
namespace experimental {

namespace {

bool LayerIs(const Feature& feature, const char* name) {
  return feature.layer == name;
}

bool PropIs(const Feature& feature, const char* key, const char* expected) {
  const PropertyValue* value = FindProperty(feature, key);
  if (value == nullptr || !std::holds_alternative<std::string>(*value)) {
    return false;
  }
  return std::get<std::string>(*value) == expected;
}

const char* StringProp(const Feature& feature, const char* key) {
  const PropertyValue* value = FindProperty(feature, key);
  if (value == nullptr || !std::holds_alternative<std::string>(*value)) {
    return nullptr;
  }
  return std::get<std::string>(*value).c_str();
}

// Explicit source categories, not substring guesses ("county region" occurs
// in Protomaps). A combined boundary takes its highest administrative level.
bool HasKindToken(std::string_view kinds, std::string_view expected) {
  while (!kinds.empty()) {
    const size_t end = kinds.find(' ');
    if (kinds.substr(0, end) == expected) return true;
    if (end == std::string_view::npos) break;
    kinds.remove_prefix(end + 1);
  }
  return false;
}

FeatureKind ProtomapsBoundaryKind(std::string_view kinds) {
  if (HasKindToken(kinds, "country")) return FeatureKind::kBoundaryCountry;
  if (HasKindToken(kinds, "region")) return FeatureKind::kBoundaryRegion;
  if (HasKindToken(kinds, "county") || HasKindToken(kinds, "locality")) {
    return FeatureKind::kBoundaryLocal;
  }
  return FeatureKind::kBoundary;
}

FeatureKind OpenMapTilesBoundaryKind(const Feature& feature) {
  const PropertyValue* value = FindProperty(feature, "admin_level");
  if (value == nullptr) return FeatureKind::kBoundary;
  int level = 0;
  if (const auto* number = std::get_if<int64_t>(value)) {
    if (*number >= 2 && *number <= 10) level = static_cast<int>(*number);
  } else if (const auto* number = std::get_if<uint64_t>(value)) {
    if (*number >= 2 && *number <= 10) level = static_cast<int>(*number);
  } else if (const auto* number = std::get_if<double>(value)) {
    if (std::isfinite(*number) && *number >= 2 && *number <= 10 &&
        std::floor(*number) == *number) level = static_cast<int>(*number);
  } else if (const auto* text = std::get_if<std::string>(value)) {
    if (*text == "10") level = 10;
    else if (text->size() == 1 && (*text)[0] >= '2' && (*text)[0] <= '9') {
      level = (*text)[0] - '0';
    }
  }
  if (level == 2) return FeatureKind::kBoundaryCountry;
  if (level == 3 || level == 4) return FeatureKind::kBoundaryRegion;
  if (level >= 5) return FeatureKind::kBoundaryLocal;
  return FeatureKind::kBoundary;
}

// OpenMapTiles `transportation` (measured Springfield 97477 tiles):
// class=motorway|primary|secondary|tertiary|minor|service|path|track|rail|
// busway|bridge. Rail is a class on this layer, not a separate layer.
FeatureKind ClassifyTransportation(const Feature& feature) {
  const char* cls = StringProp(feature, "class");
  const char* sub = StringProp(feature, "subclass");
  if ((cls != nullptr && std::strcmp(cls, "rail") == 0) ||
      (sub != nullptr && std::strcmp(sub, "rail") == 0)) {
    return FeatureKind::kRail;
  }
  if (cls == nullptr) return FeatureKind::kMinorRoad;
  if (std::strcmp(cls, "motorway") == 0 || std::strcmp(cls, "trunk") == 0) {
    return FeatureKind::kMotorway;
  }
  if (std::strcmp(cls, "primary") == 0) return FeatureKind::kPrimaryRoad;
  if (std::strcmp(cls, "secondary") == 0 || std::strcmp(cls, "tertiary") == 0) {
    return FeatureKind::kSecondaryRoad;
  }
  return FeatureKind::kMinorRoad;
}

bool ClassifyLandcover(const Feature& feature, FeatureKind* kind) {
  // subclass=park|recreation_ground|garden observed on landcover polygons.
  if (PropIs(feature, "subclass", "park") ||
      PropIs(feature, "subclass", "recreation_ground") ||
      PropIs(feature, "subclass", "garden")) {
    *kind = FeatureKind::kPark;
    return true;
  }
  *kind = FeatureKind::kLand;
  return true;
}

bool ClassifyLanduse(const Feature& feature, FeatureKind* kind) {
  const char* cls = StringProp(feature, "class");
  if (cls == nullptr) return false;
  if (std::strcmp(cls, "pitch") == 0 || std::strcmp(cls, "playground") == 0 ||
      std::strcmp(cls, "cemetery") == 0 || std::strcmp(cls, "forest") == 0 ||
      std::strcmp(cls, "grass") == 0 || std::strcmp(cls, "park") == 0) {
    *kind = FeatureKind::kPark;
    return true;
  }
  if (std::strcmp(cls, "residential") == 0 ||
      std::strcmp(cls, "commercial") == 0 ||
      std::strcmp(cls, "industrial") == 0 || std::strcmp(cls, "retail") == 0 ||
      std::strcmp(cls, "school") == 0 || std::strcmp(cls, "kindergarten") == 0 ||
      std::strcmp(cls, "military") == 0 || std::strcmp(cls, "railway") == 0 ||
      std::strcmp(cls, "stadium") == 0 || std::strcmp(cls, "track") == 0 ||
      std::strcmp(cls, "bus_station") == 0) {
    *kind = FeatureKind::kLand;
    return true;
  }
  return false;
}

// Protomaps v4 is Tilezen-derived; its kind/kind_detail fields are not
// OpenMapTiles class/subclass. Keep this mapping explicitly profile-scoped.
bool ClassifyProtomapsV4(const Feature& feature, FeatureKind* out) {
  const char* raw = StringProp(feature, "kind");
  if (raw == nullptr || *raw == '\0') return false;
  const std::string_view kind(raw);
  const char* raw_detail = StringProp(feature, "kind_detail");
  const std::string_view detail(raw_detail == nullptr ? "" : raw_detail);
  const auto geometry = feature.geometry.type;
  if (feature.layer == "roads" && geometry == GeomType::kLineString) {
    if (kind == "rail") *out = FeatureKind::kRail;
    else if (kind == "aeroway") *out = FeatureKind::kAirport;
    else if (kind == "highway") *out = FeatureKind::kMotorway;
    else if (kind == "major_road") {
      *out = detail == "secondary" || detail == "secondary_link" ||
                     detail == "tertiary" || detail == "tertiary_link"
                 ? FeatureKind::kSecondaryRoad : FeatureKind::kPrimaryRoad;
    } else if (kind == "minor_road" || kind == "path") {
      *out = FeatureKind::kMinorRoad;
    } else return false;
    return true;
  }
  if (feature.layer == "boundaries" && geometry == GeomType::kLineString) {
    // kind_detail remains intact; hierarchy comes from documented kind tokens.
    *out = ProtomapsBoundaryKind(kind);
    return true;
  }
  if (feature.layer == "water") {
    if (geometry == GeomType::kPolygon || geometry == GeomType::kLineString) {
      *out = FeatureKind::kWater;
      return true;
    }
    if (geometry == GeomType::kPoint) {
      *out = FeatureKind::kLabelMuted;
      return true;
    }
    return false;
  }
  if (feature.layer == "places" && geometry == GeomType::kPoint) {
    if (kind == "country" || kind == "region" || kind == "locality") {
      *out = detail == "village" || detail == "hamlet" ||
                     detail == "isolated_dwelling"
                 ? FeatureKind::kLabelSecondary : FeatureKind::kLabelPrimary;
      return true;
    }
    return false;
  }
  if (geometry != GeomType::kPolygon) return false;
  if (feature.layer == "earth" && kind == "earth") {
    *out = FeatureKind::kLand;
    return true;
  }
  if (feature.layer == "landcover" || feature.layer == "landuse") {
    const bool green = kind == "forest" || kind == "wood" || kind == "park" ||
                       kind == "national_park" || kind == "nature_reserve" ||
                       kind == "protected_area" || kind == "recreation_ground" ||
                       kind == "garden" || kind == "golf_course";
    *out = green ? FeatureKind::kPark : FeatureKind::kLand;
    return true;
  }
  if (feature.layer == "buildings" && kind == "building") {
    *out = FeatureKind::kBuilding;
    return true;
  }
  return false;
}

}  // namespace

bool TryClassifyFeature(const Feature& feature, FeatureKind* kind) {
  if (kind == nullptr) return false;
  if (LayerIs(feature, "water") || LayerIs(feature, "waterway") ||
      LayerIs(feature, "ocean")) {
    *kind = FeatureKind::kWater;
    return true;
  }
  if (LayerIs(feature, "road") || LayerIs(feature, "roads") ||
      LayerIs(feature, "transportation") || LayerIs(feature, "transport")) {
    *kind = ClassifyTransportation(feature);
    return true;
  }
  if (LayerIs(feature, "rail") || LayerIs(feature, "railway")) {
    *kind = FeatureKind::kRail;
    return true;
  }
  if (LayerIs(feature, "building") || LayerIs(feature, "buildings")) {
    *kind = FeatureKind::kBuilding;
    return true;
  }
  if (LayerIs(feature, "park")) {
    // OpenMapTiles park is often a point (label) at z14; polygons appear
    // at higher zoom. Points stay labels; polygons fill as park.
    if (feature.geometry.type == GeomType::kPolygon) {
      *kind = FeatureKind::kPark;
    } else {
      *kind = FeatureKind::kLabelPrimary;
    }
    return true;
  }
  if (LayerIs(feature, "landcover")) {
    return ClassifyLandcover(feature, kind);
  }
  if (LayerIs(feature, "land") || LayerIs(feature, "earth")) {
    *kind = FeatureKind::kLand;
    return true;
  }
  if (LayerIs(feature, "landuse")) {
    return ClassifyLanduse(feature, kind);
  }
  if (LayerIs(feature, "boundary") || LayerIs(feature, "boundaries")) {
    *kind = FeatureKind::kBoundary;
    return true;
  }
  if (LayerIs(feature, "airport") || LayerIs(feature, "aerodrome") ||
      LayerIs(feature, "aerodrome_label") || LayerIs(feature, "aeroway")) {
    *kind = FeatureKind::kAirport;
    return true;
  }
  if (LayerIs(feature, "place") || LayerIs(feature, "place_label") ||
      LayerIs(feature, "poi")) {
    *kind = FeatureKind::kLabelPrimary;
    return true;
  }
  return false;
}

bool AssignFeatureKinds(FeatureTile* tile) {
  if (tile == nullptr) return false;
  for (Feature& feature : tile->features) {
    FeatureKind kind = FeatureKind::kBackground;
    if (TryClassifyFeature(feature, &kind)) {
      feature.kind = kind;
      feature.kind_assigned = true;
    }
  }
  return true;
}

bool TryClassifyFeatureForProfile(std::string_view schema_version,
                                  const Feature& feature, FeatureKind* kind) {
  if (kind == nullptr) return false;
  if (schema_version == "openmaptiles-3.16") {
    if (!TryClassifyFeature(feature, kind)) return false;
    if (feature.layer == "boundary" || feature.layer == "boundaries") {
      *kind = OpenMapTilesBoundaryKind(feature);
    }
    return true;
  }
  if (schema_version == "protomaps-v4") return ClassifyProtomapsV4(feature, kind);
  if (schema_version != "orcmaps-overview-1") return false;
  if (feature.layer == "land") *kind = FeatureKind::kLand;
  else if (feature.layer == "water" || feature.layer == "waterway") *kind = FeatureKind::kWater;
  else if (feature.layer == "boundary") *kind = FeatureKind::kBoundary;
  else if (feature.layer == "place") *kind = FeatureKind::kLabelPrimary;
  else return false;
  return true;
}

bool AssignFeatureKindsForProfile(std::string_view schema_version,
                                  FeatureTile* tile) {
  if (tile == nullptr || (schema_version != "openmaptiles-3.16" &&
      schema_version != "protomaps-v4" && schema_version != "orcmaps-overview-1")) return false;
  for (Feature& feature : tile->features) {
    FeatureKind kind;
    if (TryClassifyFeatureForProfile(schema_version, feature, &kind)) {
      feature.kind = kind;
      feature.kind_assigned = true;
    }
  }
  return true;
}

bool IncludeNoTextBasemapLayer(const char* name, size_t name_len, void*) {
  if (name == nullptr) return false;
  auto is = [name, name_len](const char* lit) {
    const size_t n = std::strlen(lit);
    return name_len == n && std::memcmp(name, lit, n) == 0;
  };
  // Measured Springfield OpenMapTiles 3.16: these layers only produce
  // skipped label kinds or unused name plates under the current renderer.
  if (is("housenumber") || is("transportation_name") || is("water_name") ||
      is("mountain_peak") || is("poi") || is("place")) {
    return false;
  }
  return true;
}

}  // namespace experimental
}  // namespace orcmap

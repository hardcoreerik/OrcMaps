#include <cstdio>
#include <cmath>
#include <limits>
#include <string>

#include "orcmap/experimental/mvt_classify.hpp"

namespace {
using K = orcmap::FeatureKind;
using G = orcmap::GeomType;
int failures = 0;
void Expect(bool value, const char* message) {
  if (!value) { std::printf("FAIL %s\n", message); ++failures; }
}
void Check(const char* layer, const char* kind, const char* detail, G geometry,
           K expected, bool assigned = true) {
  orcmap::Feature f;
  f.layer = layer;
  f.geometry.type = geometry;
  f.property_keys = {"kind", "kind_detail", "name", "name:en"};
  f.property_values = {std::string(kind), std::string(detail),
                       std::string("original"), std::string("English")};
  orcmap::FeatureTile tile;
  tile.features.push_back(f);
  Expect(orcmap::experimental::AssignFeatureKindsForProfile("protomaps-v4", &tile),
         "explicit profile accepted");
  Expect(tile.features[0].kind_assigned == assigned, layer);
  K streamed = K::kBackground;
  Expect(orcmap::experimental::TryClassifyFeatureForProfile("protomaps-v4", f, &streamed)
             == assigned, "streaming and batch recognition agree");
  if (assigned) Expect(streamed == expected, "streaming and batch kinds agree");
  if (assigned) Expect(tile.features[0].kind == expected, detail);
  Expect(std::get<std::string>(tile.features[0].property_values[2]) == "original",
         "original name preserved");
  Expect(std::get<std::string>(tile.features[0].property_values[3]) == "English",
         "English name preserved");
}

void CheckAdminLevel(orcmap::PropertyValue value, K expected) {
  orcmap::Feature f;
  f.layer = "boundary";
  f.geometry.type = G::kLineString;
  f.property_keys = {"admin_level"};
  f.property_values.push_back(value);
  K result = K::kBackground;
  Expect(orcmap::experimental::TryClassifyFeatureForProfile(
             "openmaptiles-3.16", f, &result) && result == expected,
         "OpenMapTiles administrative hierarchy");
  orcmap::FeatureTile tile;
  tile.features.push_back(f);
  Expect(orcmap::experimental::AssignFeatureKindsForProfile("openmaptiles-3.16", &tile)
             && tile.features[0].kind == expected,
         "OpenMapTiles batch and stream hierarchy agree");
  const bool source_is_nan = std::holds_alternative<double>(value) &&
                            std::isnan(std::get<double>(value));
  Expect(source_is_nan
             ? std::holds_alternative<double>(tile.features[0].property_values[0]) &&
                   std::isnan(std::get<double>(tile.features[0].property_values[0]))
             : tile.features[0].property_values == f.property_values,
         "admin_level source property preserved");
  Expect(orcmap::experimental::TryClassifyFeature(f, &result) && result == K::kBoundary,
         "unprofiled legacy boundaries unchanged");
  Expect(orcmap::experimental::TryClassifyFeatureForProfile(
             "orcmaps-overview-1", f, &result) && result == K::kBoundary,
         "embedded overview boundaries unchanged");
}
}

int main() {
  Check("earth", "earth", "", G::kPolygon, K::kLand);
  Check("water", "water", "lake", G::kPolygon, K::kWater);
  Check("water", "river", "river", G::kLineString, K::kWater);
  Check("water", "ocean", "", G::kPoint, K::kLabelMuted);
  Check("landcover", "forest", "", G::kPolygon, K::kPark);
  Check("landcover", "farmland", "", G::kPolygon, K::kLand);
  Check("landuse", "nature_reserve", "", G::kPolygon, K::kPark);
  Check("landuse", "national_park", "", G::kPolygon, K::kPark);
  Check("landuse", "wood", "", G::kPolygon, K::kPark);
  Check("landuse", "residential", "", G::kPolygon, K::kLand);
  Check("roads", "highway", "motorway", G::kLineString, K::kMotorway);
  Check("roads", "highway", "trunk_link", G::kLineString, K::kMotorway);
  Check("roads", "major_road", "primary_link", G::kLineString, K::kPrimaryRoad);
  Check("roads", "major_road", "secondary", G::kLineString, K::kSecondaryRoad);
  Check("roads", "major_road", "tertiary_link", G::kLineString, K::kSecondaryRoad);
  Check("roads", "minor_road", "residential", G::kLineString, K::kMinorRoad);
  Check("roads", "path", "footway", G::kLineString, K::kMinorRoad);
  Check("roads", "rail", "tram", G::kLineString, K::kRail);
  Check("roads", "aeroway", "runway", G::kLineString, K::kAirport);
  Check("boundaries", "country", "2", G::kLineString, K::kBoundaryCountry);
  Check("boundaries", "county", "6", G::kLineString, K::kBoundaryLocal);
  Check("boundaries", "locality", "8", G::kLineString, K::kBoundaryLocal);
  Check("boundaries", "region", "4", G::kLineString, K::kBoundaryRegion);
  Check("boundaries", "county region", "6", G::kLineString, K::kBoundaryRegion);
  Check("boundaries", "region county", "6", G::kLineString, K::kBoundaryRegion);
  Check("boundaries", "county country region", "6", G::kLineString, K::kBoundaryCountry);
  Check("boundaries", "  county  region ", "6", G::kLineString, K::kBoundaryRegion);
  Check("boundaries", "countryish", "2", G::kLineString, K::kBoundary);
  Check("boundaries", "unknown", "2", G::kLineString, K::kBoundary);
  Check("boundaries", "county-region", "4", G::kLineString, K::kBoundary);
  Check("boundaries", "country", "2", G::kPolygon, K::kBackground, false);

  for (int level = 2; level <= 10; ++level) {
    const K expected = level == 2 ? K::kBoundaryCountry
                       : level <= 4 ? K::kBoundaryRegion : K::kBoundaryLocal;
    CheckAdminLevel(int64_t{level}, expected);
    CheckAdminLevel(uint64_t(level), expected);
    CheckAdminLevel(double(level), expected);
    CheckAdminLevel(std::to_string(level), expected);
  }
  for (const orcmap::PropertyValue& bad : {
           orcmap::PropertyValue{std::monostate{}}, orcmap::PropertyValue{true},
           orcmap::PropertyValue{false}, orcmap::PropertyValue{int64_t{-1}},
           orcmap::PropertyValue{int64_t{1}}, orcmap::PropertyValue{int64_t{11}},
           orcmap::PropertyValue{std::numeric_limits<uint64_t>::max()},
           orcmap::PropertyValue{4.5},
           orcmap::PropertyValue{std::numeric_limits<double>::infinity()},
           orcmap::PropertyValue{std::numeric_limits<double>::quiet_NaN()},
           orcmap::PropertyValue{std::string("4x")}, orcmap::PropertyValue{std::string(" 4")},
           orcmap::PropertyValue{std::string("4 ")}, orcmap::PropertyValue{std::string("4.0")},
           orcmap::PropertyValue{std::string("04")}, orcmap::PropertyValue{std::string("+4")},
           orcmap::PropertyValue{std::string("")}}) {
    CheckAdminLevel(bad, K::kBoundary);
  }
  Check("places", "locality", "city", G::kPoint, K::kLabelPrimary);
  Check("places", "locality", "village", G::kPoint, K::kLabelSecondary);
  Check("places", "region", "state", G::kPoint, K::kLabelPrimary);
  Check("buildings", "building", "", G::kPolygon, K::kBuilding);
  Check("roads", "ferry", "", G::kLineString, K::kBackground, false);
  Check("roads", "highway", "motorway", G::kPolygon, K::kBackground, false);
  Check("places", "locality", "city", G::kPolygon, K::kBackground, false);
  Check("pois", "shop", "", G::kPoint, K::kBackground, false);
  Check("mystery", "earth", "", G::kPolygon, K::kBackground, false);

  orcmap::FeatureTile numeric;
  orcmap::Feature border;
  border.layer = "boundaries";
  border.geometry.type = G::kLineString;
  border.property_keys = {"kind", "kind_detail"};
  border.property_values = {std::string("county"), int64_t{6}};
  numeric.features.push_back(border);
  Expect(orcmap::experimental::AssignFeatureKindsForProfile("protomaps-v4", &numeric)
         && numeric.features[0].kind == K::kBoundaryLocal
         && std::get<int64_t>(numeric.features[0].property_values[1]) == 6,
         "numeric boundary hierarchy preserved");
  numeric.features[0].property_values[0] = int64_t{123};
  numeric.features[0].kind_assigned = false;
  Expect(orcmap::experimental::AssignFeatureKindsForProfile("protomaps-v4", &numeric)
         && !numeric.features[0].kind_assigned, "wrong property type skipped safely");

  orcmap::FeatureTile tile;
  orcmap::Feature old;
  old.layer = "transportation";
  old.property_keys = {"class"};
  old.property_values = {std::string("motorway")};
  tile.features.push_back(old);
  Expect(orcmap::experimental::AssignFeatureKindsForProfile("openmaptiles-3.16", &tile)
         && tile.features[0].kind == K::kMotorway, "existing OpenMapTiles mapping");
  const auto saved = tile.features[0].kind;
  K streamed = K::kWater;
  Expect(!orcmap::experimental::TryClassifyFeatureForProfile("unknown", old, &streamed)
         && streamed == K::kWater, "unknown streaming profile preserves output");
  Expect(!orcmap::experimental::TryClassifyFeatureForProfile("protomaps-v4", old, nullptr),
         "null streaming output rejected");
  Expect(orcmap::experimental::TryClassifyFeatureForProfile("openmaptiles-3.16", old, &streamed)
         && streamed == saved, "streaming preserves OpenMapTiles mapping");
  Expect(!orcmap::experimental::AssignFeatureKindsForProfile("unknown", &tile)
         && tile.features[0].kind == saved, "unknown profile leaves tile unchanged");
  Expect(!orcmap::experimental::AssignFeatureKindsForProfile("protomaps-v4", nullptr),
         "null tile rejected");
  tile.features.clear();
  old.layer = "water";
  Expect(orcmap::experimental::TryClassifyFeatureForProfile("orcmaps-overview-1", old, &streamed)
         && streamed == K::kWater, "streaming preserves overview mapping");
  old.kind_assigned = false;
  tile.features.push_back(old);
  Expect(orcmap::experimental::AssignFeatureKindsForProfile("orcmaps-overview-1", &tile)
         && tile.features[0].kind == K::kWater, "existing overview mapping");
  tile.features.clear();
  Expect(orcmap::experimental::AssignFeatureKindsForProfile("protomaps-v4", &tile),
         "empty supported tile accepted");
  std::printf("profile tests: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}

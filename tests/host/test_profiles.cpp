#include <cstdio>
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
  if (assigned) Expect(tile.features[0].kind == expected, detail);
  Expect(std::get<std::string>(tile.features[0].property_values[2]) == "original",
         "original name preserved");
  Expect(std::get<std::string>(tile.features[0].property_values[3]) == "English",
         "English name preserved");
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
  Check("boundaries", "county", "6", G::kLineString, K::kBoundary);
  Check("boundaries", "region", "4", G::kLineString, K::kBoundary);
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
         && numeric.features[0].kind == K::kBoundary
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
  Expect(!orcmap::experimental::AssignFeatureKindsForProfile("unknown", &tile)
         && tile.features[0].kind == saved, "unknown profile leaves tile unchanged");
  Expect(!orcmap::experimental::AssignFeatureKindsForProfile("protomaps-v4", nullptr),
         "null tile rejected");
  tile.features.clear();
  old.layer = "water";
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

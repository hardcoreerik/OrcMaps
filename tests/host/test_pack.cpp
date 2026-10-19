#include "orcmap/pack.hpp"

#include <string>

#include "test_util.hpp"

namespace {

orcmap::PackManifest MakePack(std::string region_id, double min_lon,
                              double min_lat, double max_lon, double max_lat,
                              uint8_t min_zoom, uint8_t max_zoom,
                              int priority) {
  orcmap::PackManifest pack;
  pack.pack_version = "2026.09";
  pack.display_name = region_id;
  pack.region_id = std::move(region_id);
  pack.region_name = pack.display_name;
  pack.bounds = {min_lon, min_lat, max_lon, max_lat};
  pack.min_zoom = min_zoom;
  pack.max_zoom = max_zoom;
  pack.content_profile = "standard";
  pack.pmtiles_version = 3;
  pack.schema_version = "openmaptiles-3.16";
  pack.source_snapshot = "2026-09";
  pack.builder = "orcmaps-test";
  pack.builder_version = "1";
  pack.builder_commit = std::string(40, 'a');
  pack.build_date = "2026-09-14";
  pack.provenance_ids = {"test-source"};
  pack.pack_class = "open";
  pack.size_bytes = 1234;
  pack.output_sha256 = std::string(64, 'b');
  pack.priority = priority;
  pack.archive_path = "/orcmaps/packs/" + pack.region_id + "/map.pmtiles";
  pack.pack_id = orcmap::MakePackId(pack);
  return pack;
}

void TestPackIdentityUsesImmutableInputs() {
  auto pack = MakePack("US-OR", -124.7, 41.9, -116.4, 46.3, 0, 14, 10);
  const std::string id = orcmap::MakePackId(pack);
  pack.display_name = "Renamed for UI";
  pack.archive_path = "/different/device/path.pmtiles";
  ORCMAP_EXPECT_TRUE(orcmap::MakePackId(pack) == id);
  pack.source_snapshot = "2026-10";
  ORCMAP_EXPECT_TRUE(orcmap::MakePackId(pack) != id);
  pack.source_snapshot = "2026-09";
  pack.bounds.max_lon_deg -= 0.1;
  ORCMAP_EXPECT_TRUE(orcmap::MakePackId(pack) != id);
}

void TestMissingPackClassIsRejected() {
  auto pack = MakePack("bad", -10.0, -10.0, 10.0, 10.0, 0, 7, 0);
  pack.pack_class.clear();
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(pack) ==
                     orcmap::PackValidationError::kRequiredField);
  pack.pack_class = "secret-fourth-class";
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(pack) ==
                     orcmap::PackValidationError::kCompatibility);
}

void TestValidManifestPassesValidation() {
  const auto pack = MakePack("world", -180.0, -85.0, 180.0, 85.0, 0, 7, 0);
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(pack) ==
                     orcmap::PackValidationError::kNone);
}

void TestSupportedInputProfilesAreCentralized() {
  auto pack = MakePack("world", -180.0, -85.0, 180.0, 85.0, 0, 8, 0);
  pack.schema_version = "orcmaps-overview-1";
  pack.pack_id = orcmap::MakePackId(pack);
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(pack) ==
                     orcmap::PackValidationError::kNone);
  pack.schema_version = "unknown-profile";
  pack.pack_id = orcmap::MakePackId(pack);
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(pack) ==
                     orcmap::PackValidationError::kCompatibility);
}

void TestProtomapsProfileRetainsManifestGuards() {
  auto pack = MakePack("world", -180.0, -85.0511287, 180.0, 85.0511287, 0, 8, 0);
  pack.schema_version = "protomaps-v4";
  pack.pack_id = orcmap::MakePackId(pack);
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(pack) == orcmap::PackValidationError::kNone);
  auto bad = pack;
  bad.pack_id = "different-identity";
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(bad) == orcmap::PackValidationError::kIdentity);
  bad = pack; bad.output_sha256 = "invalid";
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(bad) == orcmap::PackValidationError::kArchive);
  bad = pack; bad.size_bytes = 0;
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(bad) == orcmap::PackValidationError::kArchive);
  bad = pack; bad.bounds.max_lat_deg = 90;
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(bad) == orcmap::PackValidationError::kBounds);
  bad = pack; bad.schema_version = "protomaps-v999"; bad.pack_id = orcmap::MakePackId(bad);
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(bad) == orcmap::PackValidationError::kCompatibility);
}

void TestInvalidBoundsAreRejected() {
  auto pack = MakePack("bad", -10.0, -90.0, 10.0, 30.0, 0, 7, 0);
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(pack) ==
                     orcmap::PackValidationError::kBounds);
}

void TestInvalidZoomRangeIsRejected() {
  auto pack = MakePack("bad", -10.0, -10.0, 10.0, 10.0, 15, 14, 0);
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(pack) ==
                     orcmap::PackValidationError::kZoomRange);
}

void TestMissingOfflineAttributionTextIsRejected() {
  auto pack = MakePack("bad", -10.0, -10.0, 10.0, 10.0, 0, 14, 0);
  pack.attribution.push_back({true, "", "https://example.invalid"});
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(pack) ==
                     orcmap::PackValidationError::kAttribution);
}

void TestCatalogRejectsInvalidAndDuplicatePacks() {
  orcmap::PackCatalog catalog;
  auto pack = MakePack("US-OR", -124.7, 41.9, -116.4, 46.3, 0, 14, 10);
  const std::string archive_path = pack.archive_path;
  pack.archive_path.clear();
  ORCMAP_EXPECT_TRUE(!catalog.Add(pack));
  pack.archive_path = archive_path;
  ORCMAP_EXPECT_TRUE(catalog.Add(pack));
  ORCMAP_EXPECT_TRUE(!catalog.Add(pack));
  pack.pack_id.clear();
  ORCMAP_EXPECT_TRUE(!catalog.Add(pack));
  ORCMAP_EXPECT_EQ(catalog.Packs().size(), static_cast<size_t>(1));
}

void TestResolverSelectsOneBestLocalPack() {
  orcmap::PackCatalog catalog;
  auto world = MakePack("world", -180.0, -85.0, 180.0, 85.0, 0, 7, 0);
  world.content_profile = "overview";
  world.pack_id = orcmap::MakePackId(world);
  auto oregon = MakePack("US-OR", -124.7, 41.9, -116.4, 46.3, 6, 14, 10);
  ORCMAP_EXPECT_TRUE(catalog.Add(world));
  ORCMAP_EXPECT_TRUE(catalog.Add(oregon));

  const orcmap::GeoBounds springfield = {-123.1, 44.0, -122.9, 44.1};
  const auto* low = orcmap::ResolvePack(catalog, springfield, 4);
  const auto* detail = orcmap::ResolvePack(catalog, springfield, 12);
  ORCMAP_EXPECT_TRUE(low != nullptr && low->region_id == "world");
  ORCMAP_EXPECT_TRUE(detail != nullptr && detail->region_id == "US-OR");
}

void TestResolverReturnsMissingWithoutNetworkFallback() {
  orcmap::PackCatalog catalog;
  ORCMAP_EXPECT_TRUE(catalog.Add(
      MakePack("world", -180.0, -85.0, 180.0, 85.0, 0, 7, 0)));
  const orcmap::GeoBounds springfield = {-123.1, 44.0, -122.9, 44.1};
  ORCMAP_EXPECT_TRUE(orcmap::ResolvePack(catalog, springfield, 12) == nullptr);
}

void TestResolverTieBreakPrefersNewestIdentity() {
  orcmap::PackCatalog catalog;
  auto older = MakePack("world", -180.0, -85.0, 180.0, 85.0, 0, 7, 0);
  auto newer = older;
  newer.source_snapshot = "2026-10";
  newer.pack_id = orcmap::MakePackId(newer);
  ORCMAP_EXPECT_TRUE(catalog.Add(older));
  ORCMAP_EXPECT_TRUE(catalog.Add(newer));
  const orcmap::GeoBounds request = {-10.0, -10.0, 10.0, 10.0};
  const auto* selected = orcmap::ResolvePack(catalog, request, 4);
  ORCMAP_EXPECT_TRUE(selected != nullptr &&
                     selected->source_snapshot == "2026-10");
}

}  // namespace

void RunPackTests() {
  TestPackIdentityUsesImmutableInputs();
  TestValidManifestPassesValidation();
  TestSupportedInputProfilesAreCentralized();
  TestProtomapsProfileRetainsManifestGuards();
  TestInvalidBoundsAreRejected();
  TestInvalidZoomRangeIsRejected();
  TestMissingOfflineAttributionTextIsRejected();
  TestMissingPackClassIsRejected();
  TestCatalogRejectsInvalidAndDuplicatePacks();
  TestResolverSelectsOneBestLocalPack();
  TestResolverReturnsMissingWithoutNetworkFallback();
  TestResolverTieBreakPrefersNewestIdentity();
}

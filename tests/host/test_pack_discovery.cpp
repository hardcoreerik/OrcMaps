#include "orcmap/pack_discovery.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include "orcmap/pack_json.hpp"
#include "pack_filesystem.hpp"
#include "test_util.hpp"

namespace {

// A manifest document that mirrors the real on-disk shape, parameterised so
// each test can break exactly one thing.
struct ManifestSpec {
  std::string region_id = "springfield-97477";
  std::string schema_version = "openmaptiles-3.16";
  std::string pack_class = "open";
  std::string content_profile = "standard";
  std::string source_snapshot = "geofabrik-oregon-2026-09-14";
  double min_lon = -123.055;
  double min_lat = 44.030;
  double max_lon = -122.960;
  double max_lat = 44.090;
  int min_zoom = 0;
  int max_zoom = 15;
  uint64_t size_bytes = 3507636;
  int priority = 10;
  std::string extra_keys;   // injected verbatim before the closing brace
  std::string attribution =
      "\"required_attribution\": [\"(c) OpenMapTiles\", "
      "\"(c) OpenStreetMap contributors\"],\n"
      "\"attribution_links\": [\"https://openmaptiles.org/\", "
      "\"https://www.openstreetmap.org/copyright\"],\n";
};

// Identity is derived, so the document must carry the pack_id the engine
// would compute. Build the manifest, ask the engine for its id, then
// re-emit with that id -- the same two-step a real builder performs.
std::string BuildManifestJson(const ManifestSpec& spec,
                              const std::string& pack_id) {
  char buffer[2600];
  std::snprintf(
      buffer, sizeof(buffer),
      "{\n"
      "  \"manifest_version\": 1,\n"
      "  \"pack_id\": \"%s\",\n"
      "  \"pack_version\": \"1\",\n"
      "  \"display_name\": \"Test pack\",\n"
      "  \"region_id\": \"%s\",\n"
      "  \"region_name\": \"Test Region\",\n"
      // 8 decimals, not 6: %.6f rounds kMercatorMaxLatDeg UP to 85.051129,
      // which ValidBounds correctly refuses. This is the same rounding trap
      // that makes build_world_overview.py emit manifests the engine
      // rejects, reproduced here by accident and worth keeping in view.
      "  \"bounds\": {\"min_lon\": %.8f, \"min_lat\": %.8f, "
      "\"max_lon\": %.8f, \"max_lat\": %.8f},\n"
      "  \"min_zoom\": %d,\n"
      "  \"max_zoom\": %d,\n"
      "  \"content_profile\": \"%s\",\n"
      "  \"pmtiles_version\": 3,\n"
      "  \"schema_version\": \"%s\",\n"
      "  \"source_snapshot\": \"%s\",\n"
      "  \"builder\": \"orcmaps-test\",\n"
      "  \"builder_version\": \"1\",\n"
      "  \"builder_commit\": \"%s\",\n"
      "  \"build_date\": \"2026-09-14\",\n"
      "  \"sources\": [{\"provenance_id\": \"openstreetmap\"}, "
      "{\"provenance_id\": \"openmaptiles\"}],\n"
      "  \"pack_class\": \"%s\",\n"
      "  %s"
      "  \"priority\": %d,\n"
      "  \"size_bytes\": %llu,\n"
      "  \"output_sha256\": \"%s\"%s\n"
      "}\n",
      pack_id.c_str(), spec.region_id.c_str(), spec.min_lon, spec.min_lat,
      spec.max_lon, spec.max_lat, spec.min_zoom, spec.max_zoom,
      spec.content_profile.c_str(), spec.schema_version.c_str(),
      spec.source_snapshot.c_str(), std::string(40, 'a').c_str(),
      spec.pack_class.c_str(), spec.attribution.c_str(), spec.priority,
      static_cast<unsigned long long>(spec.size_bytes),
      std::string(64, 'b').c_str(),
      spec.extra_keys.empty() ? "" : spec.extra_keys.c_str());
  return std::string(buffer);
}

std::string ManifestJson(const ManifestSpec& spec = ManifestSpec()) {
  orcmap::PackManifest probe;
  const std::string draft = BuildManifestJson(spec, "placeholder");
  if (orcmap::ParsePackManifestJson(draft, &probe) !=
      orcmap::PackJsonError::kNone) {
    return draft;  // malformed on purpose; identity is irrelevant
  }
  return BuildManifestJson(spec, orcmap::MakePackId(probe));
}

// In-memory PackFileSystem: lets a test present an unreadable file, a
// missing archive, or a wrong-sized archive without touching real storage.
class FakeFileSystem final : public orcmap::PackFileSystem {
 public:
  bool list_fails = false;
  std::vector<std::string> names;
  std::map<std::string, std::string> files;      // path -> text
  std::map<std::string, uint64_t> archives;     // path -> size
  std::vector<std::string> unreadable;          // paths that fail to read

  bool ListManifestNames(const std::string&,
                         std::vector<std::string>* out) override {
    if (list_fails) return false;
    *out = names;
    return true;
  }
  bool ReadTextFile(const std::string& path, size_t max_bytes,
                    std::string* out) override {
    for (const std::string& bad : unreadable) {
      if (bad == path) return false;
    }
    const auto it = files.find(path);
    if (it == files.end()) return false;
    if (it->second.size() > max_bytes) return false;
    *out = it->second;
    return true;
  }
  bool FileSize(const std::string& path, uint64_t* size) override {
    const auto it = archives.find(path);
    if (it == archives.end()) return false;
    if (size != nullptr) *size = it->second;
    return true;
  }
};

// One valid pack, ready to install into the fake filesystem.
void Install(FakeFileSystem* fs, const std::string& stem,
             const ManifestSpec& spec = ManifestSpec()) {
  const std::string name = stem + ".manifest.json";
  fs->names.push_back(name);
  fs->files["/orcmaps/" + name] = ManifestJson(spec);
  fs->archives["/orcmaps/" + stem + ".pmtiles"] = spec.size_bytes;
}

void TestJsonRoundTripsEveryManifestField() {
  orcmap::PackManifest manifest;
  ORCMAP_EXPECT_TRUE(orcmap::ParsePackManifestJson(ManifestJson(),
                                                   &manifest) ==
                     orcmap::PackJsonError::kNone);
  ORCMAP_EXPECT_EQ(manifest.region_id, std::string("springfield-97477"));
  ORCMAP_EXPECT_EQ(manifest.schema_version, std::string("openmaptiles-3.16"));
  ORCMAP_EXPECT_EQ(static_cast<int>(manifest.min_zoom), 0);
  ORCMAP_EXPECT_EQ(static_cast<int>(manifest.max_zoom), 15);
  ORCMAP_EXPECT_EQ(static_cast<int>(manifest.pmtiles_version), 3);
  ORCMAP_EXPECT_EQ(manifest.priority, 10);
  ORCMAP_EXPECT_NEAR(manifest.bounds.min_lon_deg, -123.055, 1e-9);
  ORCMAP_EXPECT_NEAR(manifest.bounds.max_lat_deg, 44.090, 1e-9);
  // provenance_ids come from sources[].provenance_id, in order.
  ORCMAP_EXPECT_EQ(static_cast<int>(manifest.provenance_ids.size()), 2);
  ORCMAP_EXPECT_EQ(manifest.provenance_ids[0], std::string("openstreetmap"));
  ORCMAP_EXPECT_EQ(manifest.provenance_ids[1], std::string("openmaptiles"));
  // Attribution stays exactly as the manifest declared it; the engine never
  // invents or reorders a credit.
  ORCMAP_EXPECT_EQ(static_cast<int>(manifest.attribution.size()), 2);
  ORCMAP_EXPECT_EQ(manifest.attribution[0].text, std::string("(c) OpenMapTiles"));
  ORCMAP_EXPECT_EQ(manifest.attribution[0].url,
                   std::string("https://openmaptiles.org/"));
  ORCMAP_EXPECT_EQ(manifest.attribution[1].text,
                   std::string("(c) OpenStreetMap contributors"));
  ORCMAP_EXPECT_TRUE(manifest.attribution[1].required);
  // The parser must not fabricate an archive path.
  ORCMAP_EXPECT_TRUE(manifest.archive_path.empty());
  ORCMAP_EXPECT_TRUE(orcmap::ValidatePackManifest(manifest) ==
                     orcmap::PackValidationError::kNone);
}

void TestJsonRejectsMalformedAndHostileInput() {
  orcmap::PackManifest manifest;
  const auto parse = [&manifest](const std::string& text) {
    return orcmap::ParsePackManifestJson(text, &manifest);
  };
  ORCMAP_EXPECT_TRUE(parse("{not json") == orcmap::PackJsonError::kMalformed);
  ORCMAP_EXPECT_TRUE(parse("[1,2,3]") == orcmap::PackJsonError::kNotAnObject);
  ORCMAP_EXPECT_TRUE(parse("") == orcmap::PackJsonError::kMalformed);
  // Trailing garbage after a complete document is not accepted.
  ORCMAP_EXPECT_TRUE(parse("{} trailing") == orcmap::PackJsonError::kMalformed);

  // Oversized input is refused before any parsing happens.
  const std::string huge(orcmap::kMaxPackManifestBytes + 1, ' ');
  ORCMAP_EXPECT_TRUE(parse(huge) == orcmap::PackJsonError::kTooLarge);

  // Deep nesting must hit the depth limit, not the stack.
  std::string deep;
  for (int i = 0; i < 400; ++i) deep += "[";
  ORCMAP_EXPECT_TRUE(parse(deep) == orcmap::PackJsonError::kTooDeep);

  // A wrong type is an error, never a silent default.
  ManifestSpec spec;
  std::string text = ManifestJson(spec);
  const size_t at = text.find("\"min_zoom\": 0");
  ORCMAP_EXPECT_TRUE(at != std::string::npos);
  text.replace(at, std::string("\"min_zoom\": 0").size(),
               "\"min_zoom\": \"zero\"");
  ORCMAP_EXPECT_TRUE(parse(text) == orcmap::PackJsonError::kWrongType);

  // Non-integral and out-of-range numbers are refused rather than truncated.
  text = ManifestJson(spec);
  text.replace(text.find("\"max_zoom\": 15"),
               std::string("\"max_zoom\": 15").size(), "\"max_zoom\": 15.5");
  ORCMAP_EXPECT_TRUE(parse(text) == orcmap::PackJsonError::kOutOfRange);
  text = ManifestJson(spec);
  text.replace(text.find("\"max_zoom\": 15"),
               std::string("\"max_zoom\": 15").size(), "\"max_zoom\": 99");
  ORCMAP_EXPECT_TRUE(parse(text) == orcmap::PackJsonError::kOutOfRange);

  // A missing required field is named as such.
  text = ManifestJson(spec);
  text.replace(text.find("\"region_id\""), std::string("\"region_id\"").size(),
               "\"region_id_typo\"");
  ORCMAP_EXPECT_TRUE(parse(text) == orcmap::PackJsonError::kMissingField);

  // Unknown keys are ignored, so a newer builder's extra metadata does not
  // brick an older device.
  spec.extra_keys = ",\n  \"future_field\": {\"nested\": [1, 2, 3]}";
  ORCMAP_EXPECT_TRUE(parse(ManifestJson(spec)) == orcmap::PackJsonError::kNone);
}

void TestDiscoveryAddsValidPacksAndNamesArchiveByPosition() {
  FakeFileSystem fs;
  ManifestSpec world;
  world.region_id = "world";
  world.content_profile = "overview";
  world.schema_version = "orcmaps-overview-1";
  world.pack_class = "clean";
  world.source_snapshot = "5.1.2";
  world.min_lon = -180.0;
  world.max_lon = 180.0;
  world.min_lat = -orcmap::kMercatorMaxLatDeg;
  world.max_lat = orcmap::kMercatorMaxLatDeg;
  world.max_zoom = 7;
  world.size_bytes = 9737500;
  world.priority = 0;
  world.attribution = "";  // Natural Earth: no required credit
  Install(&fs, "world-overview", world);
  Install(&fs, "springfield");

  orcmap::PackCatalog catalog;
  orcmap::DiscoveryReport report;
  ORCMAP_EXPECT_TRUE(orcmap::DiscoverPacks(fs, "/orcmaps", &catalog, &report));
  ORCMAP_EXPECT_TRUE(report.directory_listed);
  ORCMAP_EXPECT_EQ(static_cast<int>(report.manifests_seen), 2);
  ORCMAP_EXPECT_EQ(static_cast<int>(report.packs_added), 2);
  ORCMAP_EXPECT_EQ(static_cast<int>(report.rejected.size()), 0);
  ORCMAP_EXPECT_EQ(static_cast<int>(catalog.Packs().size()), 2);

  // The archive is derived from the manifest's filename, not from any path
  // inside the document -- a card cannot redirect the engine elsewhere.
  bool saw_springfield = false;
  for (const orcmap::PackManifest& pack : catalog.Packs()) {
    if (pack.region_id == "springfield-97477") {
      ORCMAP_EXPECT_EQ(pack.archive_path,
                       std::string("/orcmaps/springfield.pmtiles"));
      saw_springfield = true;
    }
  }
  ORCMAP_EXPECT_TRUE(saw_springfield);

  ORCMAP_EXPECT_EQ(
      orcmap::ArchivePathForManifest("/orcmaps", "a.manifest.json"),
      std::string("/orcmaps/a.pmtiles"));
  ORCMAP_EXPECT_TRUE(
      orcmap::ArchivePathForManifest("/orcmaps", "notes.txt").empty());
}

void TestDiscoveryResolvesWorldThenRegional() {
  // The product hierarchy: the overview serves low zooms, the regional pack
  // outranks it where both cover the view. Source selection is a resolver
  // policy over what discovery installed, not a property of file order.
  FakeFileSystem fs;
  ManifestSpec world;
  world.region_id = "world";
  world.content_profile = "overview";
  world.schema_version = "orcmaps-overview-1";
  world.pack_class = "clean";
  world.source_snapshot = "5.1.2";
  world.min_lon = -180.0;
  world.max_lon = 180.0;
  world.min_lat = -orcmap::kMercatorMaxLatDeg;
  world.max_lat = orcmap::kMercatorMaxLatDeg;
  world.max_zoom = 7;
  world.size_bytes = 9737500;
  world.priority = 0;
  world.attribution = "";
  Install(&fs, "world-overview", world);

  ManifestSpec oregon;
  oregon.region_id = "oregon";
  oregon.min_lon = -124.6;
  oregon.min_lat = 41.9;
  oregon.max_lon = -116.4;
  oregon.max_lat = 46.3;
  oregon.min_zoom = 1;
  oregon.max_zoom = 13;
  oregon.size_bytes = 84615534;
  oregon.priority = 10;
  Install(&fs, "oregon", oregon);

  orcmap::PackCatalog catalog;
  ORCMAP_EXPECT_TRUE(orcmap::DiscoverPacks(fs, "/orcmaps", &catalog, nullptr));

  // A whole-world view at a low zoom: only the overview can cover it.
  const orcmap::GeoBounds wide{-170.0, -60.0, 170.0, 60.0};
  const orcmap::PackManifest* low = orcmap::ResolvePack(catalog, wide, 3);
  ORCMAP_EXPECT_TRUE(low != nullptr);
  ORCMAP_EXPECT_EQ(low->region_id, std::string("world"));

  // Inside Oregon at a zoom both packs store: the regional pack wins on
  // priority, which is what "enter Oregon and detail takes over" means.
  const orcmap::GeoBounds inside{-123.1, 44.0, -123.0, 44.1};
  const orcmap::PackManifest* mid = orcmap::ResolvePack(catalog, inside, 7);
  ORCMAP_EXPECT_TRUE(mid != nullptr);
  ORCMAP_EXPECT_EQ(mid->region_id, std::string("oregon"));

  // Deeper than the overview reaches, still inside Oregon: regional only.
  const orcmap::PackManifest* deep = orcmap::ResolvePack(catalog, inside, 13);
  ORCMAP_EXPECT_TRUE(deep != nullptr);
  ORCMAP_EXPECT_EQ(deep->region_id, std::string("oregon"));

  // Deeper than ANY installed pack: nothing, and no network fallback.
  ORCMAP_EXPECT_TRUE(orcmap::ResolvePack(catalog, inside, 15) == nullptr);

  // Outside Oregon at a detail zoom the overview cannot reach: nothing.
  const orcmap::GeoBounds elsewhere{2.2, 48.8, 2.4, 48.9};
  ORCMAP_EXPECT_TRUE(orcmap::ResolvePack(catalog, elsewhere, 12) == nullptr);
}

void TestDiscoveryRejectsBadInputSafelyAndKeepsGoing() {
  FakeFileSystem fs;
  Install(&fs, "good");

  // Malformed JSON.
  fs.names.push_back("broken.manifest.json");
  fs.files["/orcmaps/broken.manifest.json"] = "{ nope";
  fs.archives["/orcmaps/broken.pmtiles"] = 1;

  // Unknown schema profile: refused, never guessed at.
  ManifestSpec alien;
  alien.region_id = "alien";
  alien.schema_version = "some-other-schema-9";
  Install(&fs, "alien", alien);

  // Valid manifest, absent archive.
  ManifestSpec orphan;
  orphan.region_id = "orphan";
  fs.names.push_back("orphan.manifest.json");
  fs.files["/orcmaps/orphan.manifest.json"] = ManifestJson(orphan);

  // Valid manifest, archive present but the wrong size.
  ManifestSpec wrong_size;
  wrong_size.region_id = "wrongsize";
  fs.names.push_back("wrongsize.manifest.json");
  fs.files["/orcmaps/wrongsize.manifest.json"] = ManifestJson(wrong_size);
  fs.archives["/orcmaps/wrongsize.pmtiles"] = wrong_size.size_bytes + 1;

  // A manifest that cannot be read at all.
  fs.names.push_back("locked.manifest.json");
  fs.files["/orcmaps/locked.manifest.json"] = ManifestJson(ManifestSpec());
  fs.unreadable.push_back("/orcmaps/locked.manifest.json");

  orcmap::PackCatalog catalog;
  orcmap::DiscoveryReport report;
  // Returns false because something was rejected...
  ORCMAP_EXPECT_TRUE(!orcmap::DiscoverPacks(fs, "/orcmaps", &catalog, &report));
  // ...but the good pack still got installed: one bad file must not hide
  // the packs beside it.
  ORCMAP_EXPECT_EQ(static_cast<int>(report.packs_added), 1);
  ORCMAP_EXPECT_EQ(static_cast<int>(catalog.Packs().size()), 1);
  ORCMAP_EXPECT_EQ(static_cast<int>(report.manifests_seen), 6);
  ORCMAP_EXPECT_EQ(static_cast<int>(report.rejected.size()), 5);

  std::map<std::string, orcmap::PackRejection> by_name;
  for (const orcmap::RejectedPack& item : report.rejected) {
    by_name[item.manifest_name] = item.rejection;
  }
  ORCMAP_EXPECT_TRUE(by_name["broken.manifest.json"] ==
                     orcmap::PackRejection::kBadJson);
  ORCMAP_EXPECT_TRUE(by_name["alien.manifest.json"] ==
                     orcmap::PackRejection::kInvalidManifest);
  ORCMAP_EXPECT_TRUE(by_name["orphan.manifest.json"] ==
                     orcmap::PackRejection::kArchiveMissing);
  ORCMAP_EXPECT_TRUE(by_name["wrongsize.manifest.json"] ==
                     orcmap::PackRejection::kArchiveSizeMismatch);
  ORCMAP_EXPECT_TRUE(by_name["locked.manifest.json"] ==
                     orcmap::PackRejection::kUnreadable);

  // The unknown profile must be reported as a compatibility refusal, which
  // is what distinguishes "we don't support this" from "this is corrupt".
  for (const orcmap::RejectedPack& item : report.rejected) {
    if (item.manifest_name == "alien.manifest.json") {
      ORCMAP_EXPECT_TRUE(item.validation_error ==
                         orcmap::PackValidationError::kCompatibility);
    }
    if (item.manifest_name == "orphan.manifest.json") {
      ORCMAP_EXPECT_EQ(item.detail, std::string("/orcmaps/orphan.pmtiles"));
    }
  }
}

void TestDiscoveryHandlesUnlistableAndEmptyDirectories() {
  FakeFileSystem fs;
  fs.list_fails = true;
  orcmap::PackCatalog catalog;
  orcmap::DiscoveryReport report;
  ORCMAP_EXPECT_TRUE(!orcmap::DiscoverPacks(fs, "/orcmaps", &catalog, &report));
  // An unlistable directory is distinct from an empty one, so a board can
  // tell "no card" from "card with no maps".
  ORCMAP_EXPECT_TRUE(!report.directory_listed);
  ORCMAP_EXPECT_EQ(static_cast<int>(report.manifests_seen), 0);

  FakeFileSystem empty;
  orcmap::PackCatalog empty_catalog;
  orcmap::DiscoveryReport empty_report;
  ORCMAP_EXPECT_TRUE(
      orcmap::DiscoverPacks(empty, "/orcmaps", &empty_catalog, &empty_report));
  ORCMAP_EXPECT_TRUE(empty_report.directory_listed);
  ORCMAP_EXPECT_EQ(static_cast<int>(empty_report.packs_added), 0);
  ORCMAP_EXPECT_TRUE(empty_report.rejected.empty());

  ORCMAP_EXPECT_TRUE(!orcmap::DiscoverPacks(empty, "/orcmaps", nullptr,
                                            &empty_report));
}

void TestDiscoveryIsDeterministicRegardlessOfListingOrder() {
  // Two cards with identical contents must yield identical catalogues even
  // if the filesystem enumerates them in different orders.
  FakeFileSystem forward;
  Install(&forward, "aaa");
  ManifestSpec other;
  other.region_id = "other";
  Install(&forward, "zzz", other);

  FakeFileSystem reversed = forward;
  std::reverse(reversed.names.begin(), reversed.names.end());

  orcmap::PackCatalog a;
  orcmap::PackCatalog b;
  ORCMAP_EXPECT_TRUE(orcmap::DiscoverPacks(forward, "/orcmaps", &a, nullptr));
  ORCMAP_EXPECT_TRUE(orcmap::DiscoverPacks(reversed, "/orcmaps", &b, nullptr));
  ORCMAP_EXPECT_EQ(static_cast<int>(a.Packs().size()),
                   static_cast<int>(b.Packs().size()));
  for (size_t i = 0; i < a.Packs().size(); ++i) {
    ORCMAP_EXPECT_EQ(a.Packs()[i].pack_id, b.Packs()[i].pack_id);
  }
}

void TestHostFileSystemAdapterListsOneDirectoryOnly() {
  // Exercises the real adapter, not the fake: a temp directory with a
  // manifest, a decoy in a subdirectory, and a non-manifest file.
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / "orcmaps-discovery-test";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root / "nested", ec);

  const auto write = [](const std::filesystem::path& path,
                        const std::string& text) {
    std::FILE* file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) return;
    std::fwrite(text.data(), 1, text.size(), file);
    std::fclose(file);
  };
  write(root / "springfield.manifest.json", ManifestJson());
  write(root / "springfield.pmtiles", std::string(3507636, '\0'));
  write(root / "README.md", "not a manifest");
  write(root / "nested" / "hidden.manifest.json", ManifestJson());

  orcmap::host::PackFileSystem fs;
  std::vector<std::string> names;
  ORCMAP_EXPECT_TRUE(fs.ListManifestNames(root.string(), &names));
  // Exactly one: the decoy in nested/ must NOT be found, and README.md is
  // not a manifest.
  ORCMAP_EXPECT_EQ(static_cast<int>(names.size()), 1);
  ORCMAP_EXPECT_EQ(names[0], std::string("springfield.manifest.json"));

  uint64_t size = 0;
  ORCMAP_EXPECT_TRUE(
      fs.FileSize((root / "springfield.pmtiles").string(), &size));
  ORCMAP_EXPECT_TRUE(size == 3507636u);
  ORCMAP_EXPECT_TRUE(!fs.FileSize((root / "absent.pmtiles").string(), &size));
  // A directory is not a regular file.
  ORCMAP_EXPECT_TRUE(!fs.FileSize((root / "nested").string(), &size));

  std::string text;
  ORCMAP_EXPECT_TRUE(fs.ReadTextFile(
      (root / "springfield.manifest.json").string(), 64 * 1024, &text));
  ORCMAP_EXPECT_TRUE(!text.empty());
  // A byte budget smaller than the file refuses rather than truncating.
  ORCMAP_EXPECT_TRUE(!fs.ReadTextFile(
      (root / "springfield.manifest.json").string(), 8, &text));

  // End to end through the real filesystem.
  orcmap::PackCatalog catalog;
  orcmap::DiscoveryReport report;
  ORCMAP_EXPECT_TRUE(
      orcmap::DiscoverPacks(fs, root.string(), &catalog, &report));
  ORCMAP_EXPECT_EQ(static_cast<int>(report.packs_added), 1);
  ORCMAP_EXPECT_EQ(static_cast<int>(catalog.Packs().size()), 1);

  std::filesystem::remove_all(root, ec);
}

void TestProtomapsDiscoveryUsesExistingIntegrityChecks() {
  FakeFileSystem fs;
  ManifestSpec spec;
  spec.schema_version = "protomaps-v4";
  // Match the assessed global archive's header limits and size.
  spec.region_id = "world";
  spec.min_lon = -180.0; spec.max_lon = 180.0;
  spec.min_lat = -85.0511287; spec.max_lat = 85.0511287;
  spec.min_zoom = 0; spec.max_zoom = 8; spec.size_bytes = 543380195;
  fs.names = {"global.manifest.json", "wrongsize.manifest.json"};
  fs.files["/orcmaps/global.manifest.json"] = ManifestJson(spec);
  fs.archives["/orcmaps/global.pmtiles"] = spec.size_bytes;
  spec.region_id = "wrongsize";
  fs.files["/orcmaps/wrongsize.manifest.json"] = ManifestJson(spec);
  fs.archives["/orcmaps/wrongsize.pmtiles"] = spec.size_bytes + 1;
  orcmap::PackCatalog catalog;
  orcmap::DiscoveryReport report;
  ORCMAP_EXPECT_TRUE(!orcmap::DiscoverPacks(fs, "/orcmaps", &catalog, &report));
  ORCMAP_EXPECT_EQ(static_cast<int>(catalog.Packs().size()), 1);
  if (!catalog.Packs().empty()) {
    ORCMAP_EXPECT_EQ(catalog.Packs()[0].schema_version, std::string("protomaps-v4"));
    ORCMAP_EXPECT_EQ(catalog.Packs()[0].archive_path, std::string("/orcmaps/global.pmtiles"));
    ORCMAP_EXPECT_TRUE(orcmap::ResolvePack(catalog, {150.0, -40.0, 155.0, -30.0}, 8) != nullptr);
    ORCMAP_EXPECT_TRUE(orcmap::ResolvePack(catalog, {150.0, -40.0, 155.0, -30.0}, 9) == nullptr);
  }
  ORCMAP_EXPECT_EQ(static_cast<int>(report.rejected.size()), 1);
  if (report.rejected.size() == 1)
    ORCMAP_EXPECT_TRUE(report.rejected[0].rejection == orcmap::PackRejection::kArchiveSizeMismatch);
}

}  // namespace

void RunPackDiscoveryTests() {
  TestProtomapsDiscoveryUsesExistingIntegrityChecks();
  TestJsonRoundTripsEveryManifestField();
  TestJsonRejectsMalformedAndHostileInput();
  TestDiscoveryAddsValidPacksAndNamesArchiveByPosition();
  TestDiscoveryResolvesWorldThenRegional();
  TestDiscoveryRejectsBadInputSafelyAndKeepsGoing();
  TestDiscoveryHandlesUnlistableAndEmptyDirectories();
  TestDiscoveryIsDeterministicRegardlessOfListingOrder();
  TestHostFileSystemAdapterListsOneDirectoryOnly();
}

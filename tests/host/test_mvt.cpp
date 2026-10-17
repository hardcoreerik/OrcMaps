#include "orcmap/experimental/mvt_classify.hpp"
#include "orcmap/mvt.hpp"
#include "orcmap/mvt_stream.hpp"
#include <new>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "test_util.hpp"

namespace {

std::vector<uint8_t> ReadFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
}

// Numeric attribute values may decode as int64_t, uint64_t, or double
// depending on which MVT wire representation the encoder chose for an
// equal mathematical value -- these tests care about the value, not which
// alternative holds it (string/bool ARE checked exactly, since MVT is
// unambiguous about those two).
double AsNumber(const orcmap::MvtValue& v, bool* ok) {
  *ok = true;
  if (std::holds_alternative<double>(v)) return std::get<double>(v);
  if (std::holds_alternative<int64_t>(v)) return static_cast<double>(std::get<int64_t>(v));
  if (std::holds_alternative<uint64_t>(v)) return static_cast<double>(std::get<uint64_t>(v));
  *ok = false;
  return 0.0;
}

const orcmap::MvtLayer* FindLayer(const orcmap::MvtTile& tile, const std::string& name) {
  for (const auto& layer : tile.layers) {
    if (layer.name == name) return &layer;
  }
  return nullptr;
}

const orcmap::MvtValue* FindAttribute(const orcmap::MvtFeature& feature,
                                       const std::string& key) {
  for (size_t i = 0; i < feature.attribute_keys.size(); ++i) {
    if (feature.attribute_keys[i] == key) return &feature.attribute_values[i];
  }
  return nullptr;
}

// STREAMING decode must be output-identical to materialising decode. This is
// the gate on the change that made a no-PSRAM board usable: if streaming
// ever emitted a different feature sequence, maps would silently differ by
// board.
struct StreamCapture {
  std::vector<std::string> layers;
  std::vector<orcmap::MvtFeature> features;
  std::vector<uint32_t> extents;
  // Proves the decoder really does reuse one Feature rather than holding all
  // of them: every callback must see the same object address.
  const void* first_address = nullptr;
  bool single_address = true;
};

bool CaptureFeature(const orcmap::MvtLayer& layer,
                    const orcmap::MvtFeature& feature, void* ctx) {
  StreamCapture& cap = *static_cast<StreamCapture*>(ctx);
  if (cap.first_address == nullptr) {
    cap.first_address = &feature;
  } else if (cap.first_address != &feature) {
    cap.single_address = false;
  }
  cap.layers.push_back(layer.name);
  cap.extents.push_back(layer.extent);
  cap.features.push_back(feature);  // deep copy, for comparison only
  return true;
}

void TestStreamingMatchesMaterialisedDecode(const std::string& fixture_path) {
  const std::vector<uint8_t> data = ReadFile(fixture_path);
  ORCMAP_EXPECT_TRUE(!data.empty());

  orcmap::MvtTile batch;
  ORCMAP_EXPECT_TRUE(orcmap::DecodeMvtTile(data.data(), data.size(), &batch));

  StreamCapture cap;
  orcmap::MvtDecodeOptions options;
  options.feature_sink = &CaptureFeature;
  options.feature_sink_ctx = &cap;
  orcmap::MvtTile streamed;
  ORCMAP_EXPECT_TRUE(orcmap::DecodeMvtTile(data.data(), data.size(), options,
                                           &streamed));

  // Streaming must not accumulate anything.
  ORCMAP_EXPECT_EQ(static_cast<int>(streamed.layers.size()), 0);
  ORCMAP_EXPECT_TRUE(cap.single_address);

  size_t expected = 0;
  for (const orcmap::MvtLayer& layer : batch.layers) {
    expected += layer.features.size();
  }
  ORCMAP_EXPECT_EQ(static_cast<int>(cap.features.size()),
                   static_cast<int>(expected));
  ORCMAP_EXPECT_TRUE(expected > 0);

  // Same features, same order, same layer context.
  size_t i = 0;
  for (const orcmap::MvtLayer& layer : batch.layers) {
    for (const orcmap::MvtFeature& want : layer.features) {
      const orcmap::MvtFeature& got = cap.features[i];
      ORCMAP_EXPECT_EQ(cap.layers[i], layer.name);
      ORCMAP_EXPECT_TRUE(cap.extents[i] == layer.extent);
      ORCMAP_EXPECT_TRUE(got.id == want.id);
      ORCMAP_EXPECT_TRUE(got.geom_type == want.geom_type);
      ORCMAP_EXPECT_EQ(static_cast<int>(got.geometry.size()),
                       static_cast<int>(want.geometry.size()));
      for (size_t r = 0; r < want.geometry.size(); ++r) {
        ORCMAP_EXPECT_EQ(static_cast<int>(got.geometry[r].size()),
                         static_cast<int>(want.geometry[r].size()));
        for (size_t k = 0; k < want.geometry[r].size(); ++k) {
          ORCMAP_EXPECT_TRUE(got.geometry[r][k].x == want.geometry[r][k].x);
          ORCMAP_EXPECT_TRUE(got.geometry[r][k].y == want.geometry[r][k].y);
        }
      }
      ORCMAP_EXPECT_TRUE(got.attribute_keys == want.attribute_keys);
      ORCMAP_EXPECT_EQ(static_cast<int>(got.attribute_values.size()),
                       static_cast<int>(want.attribute_values.size()));
      for (size_t v = 0; v < want.attribute_values.size(); ++v) {
        ORCMAP_EXPECT_TRUE(got.attribute_values[v] == want.attribute_values[v]);
      }
      ++i;
    }
  }
}

void TestStreamingHonoursLayerFilterAndAbort(const std::string& fixture_path) {
  const std::vector<uint8_t> data = ReadFile(fixture_path);

  // The layer filter still applies while streaming.
  StreamCapture all;
  orcmap::MvtDecodeOptions unfiltered;
  unfiltered.feature_sink = &CaptureFeature;
  unfiltered.feature_sink_ctx = &all;
  orcmap::MvtTile sink_only;
  ORCMAP_EXPECT_TRUE(orcmap::DecodeMvtTile(data.data(), data.size(),
                                           unfiltered, &sink_only));

  StreamCapture filtered;
  orcmap::MvtDecodeOptions with_filter;
  with_filter.feature_sink = &CaptureFeature;
  with_filter.feature_sink_ctx = &filtered;
  with_filter.include_layer = &orcmap::experimental::IncludeNoTextBasemapLayer;
  ORCMAP_EXPECT_TRUE(orcmap::DecodeMvtTile(data.data(), data.size(),
                                           with_filter, &sink_only));
  ORCMAP_EXPECT_TRUE(filtered.features.size() <= all.features.size());
  for (const std::string& name : filtered.layers) {
    ORCMAP_EXPECT_TRUE(
        orcmap::experimental::IncludeNoTextBasemapLayer(name.c_str(),
                                                        name.size(), nullptr));
  }

  // A sink that refuses aborts the decode rather than silently truncating.
  struct Refuse {
    static bool Sink(const orcmap::MvtLayer&, const orcmap::MvtFeature&,
                     void* ctx) {
      ++*static_cast<int*>(ctx);
      return false;
    }
  };
  int calls = 0;
  orcmap::MvtDecodeOptions refusing;
  refusing.feature_sink = &Refuse::Sink;
  refusing.feature_sink_ctx = &calls;
  ORCMAP_EXPECT_TRUE(!orcmap::DecodeMvtTile(data.data(), data.size(), refusing,
                                            &sink_only));
  ORCMAP_EXPECT_EQ(calls, 1);
}

void TestDecodeFixture(const std::string& fixture_path) {
  const std::vector<uint8_t> data = ReadFile(fixture_path);
  ORCMAP_EXPECT_TRUE(!data.empty());

  orcmap::MvtTile tile;
  const bool ok = orcmap::DecodeMvtTile(data.data(), data.size(), &tile);
  ORCMAP_EXPECT_TRUE(ok);
  ORCMAP_EXPECT_EQ(tile.layers.size(), static_cast<size_t>(3));

  // --- water: one polygon feature -----------------------------------
  const orcmap::MvtLayer* water = FindLayer(tile, "water");
  ORCMAP_EXPECT_TRUE(water != nullptr);
  if (water == nullptr) return;
  ORCMAP_EXPECT_EQ(water->extent, 4096u);
  ORCMAP_EXPECT_EQ(water->features.size(), static_cast<size_t>(1));
  const orcmap::MvtFeature& lake = water->features[0];
  ORCMAP_EXPECT_TRUE(lake.geom_type == orcmap::MvtGeomType::kPolygon);
  ORCMAP_EXPECT_EQ(lake.geometry.size(), static_cast<size_t>(1));
  ORCMAP_EXPECT_EQ(lake.geometry[0].size(), static_cast<size_t>(4));
  // Ring is a closed square (exact coordinates depend on the encoder's Y
  // flip, but must form a 4x4 square).
  int32_t min_x = lake.geometry[0][0].x, max_x = min_x;
  int32_t min_y = lake.geometry[0][0].y, max_y = min_y;
  for (const auto& p : lake.geometry[0]) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }
  ORCMAP_EXPECT_EQ(max_x - min_x, 4);
  ORCMAP_EXPECT_EQ(max_y - min_y, 4);

  const orcmap::MvtValue* kind = FindAttribute(lake, "kind");
  ORCMAP_EXPECT_TRUE(kind != nullptr && std::holds_alternative<std::string>(*kind));
  if (kind != nullptr && std::holds_alternative<std::string>(*kind)) {
    ORCMAP_EXPECT_TRUE(std::get<std::string>(*kind) == "lake");
  }
  const orcmap::MvtValue* area = FindAttribute(lake, "area");
  ORCMAP_EXPECT_TRUE(area != nullptr);
  if (area != nullptr) {
    bool numeric_ok = false;
    ORCMAP_EXPECT_NEAR(AsNumber(*area, &numeric_ok), 12.5, 1e-6);
    ORCMAP_EXPECT_TRUE(numeric_ok);
  }

  // --- road: one linestring feature -----------------------------------
  const orcmap::MvtLayer* road = FindLayer(tile, "road");
  ORCMAP_EXPECT_TRUE(road != nullptr);
  if (road == nullptr) return;
  ORCMAP_EXPECT_EQ(road->features.size(), static_cast<size_t>(1));
  const orcmap::MvtFeature& primary = road->features[0];
  ORCMAP_EXPECT_TRUE(primary.geom_type == orcmap::MvtGeomType::kLineString);
  ORCMAP_EXPECT_EQ(primary.geometry.size(), static_cast<size_t>(1));
  ORCMAP_EXPECT_EQ(primary.geometry[0].size(), static_cast<size_t>(3));

  const orcmap::MvtValue* oneway = FindAttribute(primary, "oneway");
  ORCMAP_EXPECT_TRUE(oneway != nullptr && std::holds_alternative<bool>(*oneway));
  if (oneway != nullptr && std::holds_alternative<bool>(*oneway)) {
    ORCMAP_EXPECT_TRUE(std::get<bool>(*oneway) == true);
  }
  const orcmap::MvtValue* lanes = FindAttribute(primary, "lanes");
  ORCMAP_EXPECT_TRUE(lanes != nullptr);
  if (lanes != nullptr) {
    bool numeric_ok = false;
    ORCMAP_EXPECT_NEAR(AsNumber(*lanes, &numeric_ok), 2.0, 1e-9);
    ORCMAP_EXPECT_TRUE(numeric_ok);
  }

  // --- place: one point feature -----------------------------------------
  const orcmap::MvtLayer* place = FindLayer(tile, "place");
  ORCMAP_EXPECT_TRUE(place != nullptr);
  if (place == nullptr) return;
  ORCMAP_EXPECT_EQ(place->features.size(), static_cast<size_t>(1));
  const orcmap::MvtFeature& town = place->features[0];
  ORCMAP_EXPECT_TRUE(town.geom_type == orcmap::MvtGeomType::kPoint);
  ORCMAP_EXPECT_EQ(town.geometry.size(), static_cast<size_t>(1));
  ORCMAP_EXPECT_EQ(town.geometry[0].size(), static_cast<size_t>(1));

  const orcmap::MvtValue* name = FindAttribute(town, "name");
  ORCMAP_EXPECT_TRUE(name != nullptr && std::holds_alternative<std::string>(*name));
  if (name != nullptr && std::holds_alternative<std::string>(*name)) {
    ORCMAP_EXPECT_TRUE(std::get<std::string>(*name) == "Testville");
  }
}

void TestEmptyBufferFails() {
  orcmap::MvtTile tile;
  const bool ok = orcmap::DecodeMvtTile(nullptr, 0, &tile);
  ORCMAP_EXPECT_TRUE(!ok);
}

void TestGarbageBufferFails() {
  const std::vector<uint8_t> garbage = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  orcmap::MvtTile tile;
  const bool ok = orcmap::DecodeMvtTile(garbage.data(), garbage.size(), &tile);
  ORCMAP_EXPECT_TRUE(!ok);
}

bool ExcludeWater(const char* name, size_t len, void*) {
  const char k[] = "water";
  return !(len == 5 && std::memcmp(name, k, 5) == 0);
}

void TestFilterNullMatchesLegacy(const std::string& fixture_path) {
  const std::vector<uint8_t> data = ReadFile(fixture_path);
  orcmap::MvtTile a;
  orcmap::MvtTile b;
  ORCMAP_EXPECT_TRUE(orcmap::DecodeMvtTile(data.data(), data.size(), &a));
  orcmap::MvtDecodeOptions opt;
  ORCMAP_EXPECT_TRUE(
      orcmap::DecodeMvtTile(data.data(), data.size(), opt, &b));
  ORCMAP_EXPECT_EQ(a.layers.size(), b.layers.size());
  ORCMAP_EXPECT_EQ(a.layers.size(), static_cast<size_t>(3));
}

void TestFilterExcludesRequestedLayer(const std::string& fixture_path) {
  const std::vector<uint8_t> data = ReadFile(fixture_path);
  orcmap::MvtDecodeOptions opt;
  opt.include_layer = &ExcludeWater;
  orcmap::MvtTile tile;
  ORCMAP_EXPECT_TRUE(
      orcmap::DecodeMvtTile(data.data(), data.size(), opt, &tile));
  ORCMAP_EXPECT_TRUE(FindLayer(tile, "water") == nullptr);
  ORCMAP_EXPECT_TRUE(FindLayer(tile, "road") != nullptr);
  ORCMAP_EXPECT_TRUE(FindLayer(tile, "place") != nullptr);
  for (const auto& layer : tile.layers) {
    ORCMAP_EXPECT_TRUE(layer.name != "water");
    ORCMAP_EXPECT_TRUE(!layer.features.empty());
  }
}

struct NameSink {
  std::vector<std::string> names;
};

bool RecordNamesKeepAll(const char* name, size_t len, void* ctx) {
  auto* sink = static_cast<NameSink*>(ctx);
  sink->names.emplace_back(name, len);
  return true;
}

void TestFilterCallbackReceivesNames(const std::string& fixture_path) {
  const std::vector<uint8_t> data = ReadFile(fixture_path);
  NameSink sink;
  orcmap::MvtDecodeOptions opt;
  opt.include_layer = &RecordNamesKeepAll;
  opt.include_layer_ctx = &sink;
  orcmap::MvtTile tile;
  ORCMAP_EXPECT_TRUE(
      orcmap::DecodeMvtTile(data.data(), data.size(), opt, &tile));
  ORCMAP_EXPECT_EQ(sink.names.size(), static_cast<size_t>(3));
  ORCMAP_EXPECT_TRUE(sink.names[0] == "water");
  ORCMAP_EXPECT_TRUE(sink.names[1] == "road");
  ORCMAP_EXPECT_TRUE(sink.names[2] == "place");
  ORCMAP_EXPECT_EQ(tile.layers.size(), static_cast<size_t>(3));
}

void TestBasemapFilterSkipsPlaceOnFixture(const std::string& fixture_path) {
  const std::vector<uint8_t> data = ReadFile(fixture_path);
  orcmap::MvtDecodeOptions opt;
  opt.include_layer = &orcmap::experimental::IncludeNoTextBasemapLayer;
  orcmap::MvtTile tile;
  ORCMAP_EXPECT_TRUE(
      orcmap::DecodeMvtTile(data.data(), data.size(), opt, &tile));
  ORCMAP_EXPECT_TRUE(FindLayer(tile, "place") == nullptr);
  ORCMAP_EXPECT_TRUE(FindLayer(tile, "water") != nullptr);
}

void TestStreamingCleanupReleasesOversizeOnFailure() {
  for(bool throws : {false,true}) {
    orcmap::MvtStreamScratch scratch;
    scratch.feature_bytes.resize(orcmap::kMaxRetainedFeatureBytes+1024);
    scratch.layers.resize(1);
    scratch.feature.geometry.paths.resize(1);
    scratch.feature.geometry.paths[0].reserve(orcmap::kRetainedGeometryPoints+1);
    orcmap::MvtStreamOptions options;
    options.feature_sink=[](const orcmap::Feature&,void*) {return true;};
    const auto reader=[](void* context,uint64_t,uint8_t*,size_t)->size_t {
      if(*static_cast<bool*>(context))throw std::bad_alloc();
      return 0;
    };
    bool caught=false;
    try {
      ORCMAP_EXPECT_TRUE(!orcmap::StreamMvtTile(orcmap::Compression::kGzip,reader,&throws,0,18,options,&scratch));
    } catch(const std::bad_alloc&) {caught=true;}
    ORCMAP_EXPECT_EQ(caught,throws);
    ORCMAP_EXPECT_EQ(scratch.feature_bytes.capacity(),static_cast<size_t>(0));
    ORCMAP_EXPECT_EQ(scratch.layers.capacity(),static_cast<size_t>(0));
    ORCMAP_EXPECT_EQ(scratch.feature.geometry.paths.capacity(),static_cast<size_t>(0));
  }
}

void TestTruncatedFixtureFails(const std::string& fixture_path) {
  std::vector<uint8_t> data = ReadFile(fixture_path);
  ORCMAP_EXPECT_TRUE(data.size() > 10);
  data.resize(data.size() / 2);  // Cut a valid tile in half.
  orcmap::MvtTile tile;
  const bool ok = orcmap::DecodeMvtTile(data.data(), data.size(), &tile);
  ORCMAP_EXPECT_TRUE(!ok);
}

}  // namespace

void RunMvtTests(const std::string& fixture_path) {
  TestDecodeFixture(fixture_path);
  TestStreamingMatchesMaterialisedDecode(fixture_path);
  TestStreamingHonoursLayerFilterAndAbort(fixture_path);
  TestEmptyBufferFails();
  TestGarbageBufferFails();
  TestTruncatedFixtureFails(fixture_path);
  TestStreamingCleanupReleasesOversizeOnFailure();
  TestFilterNullMatchesLegacy(fixture_path);
  TestFilterExcludesRequestedLayer(fixture_path);
  TestFilterCallbackReceivesNames(fixture_path);
  TestBasemapFilterSkipsPlaceOnFixture(fixture_path);
}

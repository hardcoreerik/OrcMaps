#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "orcmap/compression.hpp"
#include "orcmap/feature.hpp"
#include "orcmap/mvt.hpp"

namespace orcmap {

// END-TO-END STREAMING MVT PARSE: reads a compressed tile straight out of an
// archive and emits its features one at a time, without ever materialising
// the inflated tile.
//
// Why this exists, in measured numbers. On a CYD 3.5" (ESP32-3248S035, no
// PSRAM) the binding limit is the largest CONTIGUOUS free block, not total
// free memory: 190,264 bytes free but a 69,632-byte largest block, against a
// 111,366-byte inflated Oregon z7 tile. Buffering the tile therefore cannot
// work however it is obtained -- pre-reserving the buffer at startup only
// moved the problem and made it worse.
//
// This parser's footprint is a similar total but in SEPARATE, SMALL pieces:
//
//   inflate scratch (32 KiB window + ~8 KiB state + 2 KiB input)  ~43 KiB
//   all layers' key/value tables (worst measured: Oregon z7)       ~24 KiB
//   one feature's bytes (worst measured: Springfield z13)          ~36 KiB
//
// so no single allocation exceeds ~36 KiB and the whole thing fits in a
// fragmented heap that cannot supply one 111 KiB block.
//
// TWO passes, because MVT field order forbids one. Measured across the
// project's own packs, 167 of 173 layers emit features (field 2) BEFORE keys
// (3) and values (4) -- ascending protobuf field order, as Planetiler writes
// -- so a feature's attribute indices cannot be resolved when it is first
// seen. Pass 1 skips features and collects the tables; pass 2 decodes and
// emits features. The payload is inflated twice, which trades CPU for the
// contiguous memory the board does not have.

// Receives one fully decoded OrcMaps Feature. Nothing passed in outlives
// the call: the same Feature object is reused for every feature in the tile,
// so copy anything you need to keep.
//
// The sink takes a Feature rather than an MvtFeature because the streaming
// parser decodes STRAIGHT into the OrcMaps type. Building an MvtFeature and
// then copying it paid for a dense feature's geometry twice -- about 72 KiB
// each way for the worst feature in these packs -- which no board with
// ~129 KiB of heap can afford.
using StreamedFeatureSink = bool (*)(const Feature& feature, void* ctx);

struct MvtStreamOptions {
  // Layer filter, same contract as MvtDecodeOptions::include_layer.
  bool (*include_layer)(const char* name, size_t name_len, void* ctx) = nullptr;
  void* include_layer_ctx = nullptr;
  StreamedFeatureSink feature_sink = nullptr;
  void* feature_sink_ctx = nullptr;
};

// Reusable scratch. Allocate ONE of these and keep it: every buffer inside
// is reused across tiles, so a steady-state frame performs no large
// allocation at all.
struct MvtStreamScratch {
  // Per-layer tables gathered in pass 1. Only included layers keep their
  // tables; excluded layers keep just enough to be skipped in pass 2.
  struct LayerTables {
    std::string name;
    uint32_t extent = 4096;
    uint32_t version = 1;
    bool included = false;
    std::vector<std::string> keys;
    std::vector<MvtValue> values;
  };

  InflatingByteStream stream;
  std::vector<LayerTables> layers;
  std::vector<uint8_t> feature_bytes;  // one feature's encoded bytes
  Feature feature;                     // reused, decoded in place
  MvtLayer layer;                      // name/extent/version for the decoder
  // Packed tag/geometry command buffers, reused across features.
  std::vector<uint32_t> tags;
  std::vector<uint32_t> geometry;
};

// Refuses a single feature larger than this rather than growing without
// bound on hostile input. The largest feature measured in this project's
// packs is 36,259 bytes.
inline constexpr size_t kMaxStreamedFeatureBytes = 256u * 1024u;

// Point capacity the scratch keeps between TILES. Reuse inside a tile avoids
// one allocation per ring per feature; carrying a dense feature's geometry
// for the rest of the run is a different thing entirely, and it is a leak in
// all but name.
//
// Measured on a CYD 3.5": without this bound one dense feature's retained
// rings pinned ~72 KiB, free heap fell to 2,304 bytes, and every subsequent
// tile was refused by the pipeline's heap floor -- worse than not reusing at
// all. 4,096 points is ~32 KiB, comfortably above the common case.
inline constexpr size_t kRetainedGeometryPoints = 4096;

// Encoded-feature bytes the scratch keeps between tiles. Above this the
// buffer is released, so one unusually large feature does not hold its
// staging buffer for the rest of the run.
inline constexpr size_t kMaxRetainedFeatureBytes = 48u * 1024u;

// Parses the compressed tile at [input_offset, input_offset + input_size)
// pulled through `reader`, emitting each feature to
// `options.feature_sink` (which must be set). `options.include_layer` is
// honoured. Returns false on malformed input, on a feature exceeding
// kMaxStreamedFeatureBytes, or if the sink refuses.
// Claims the scratch's large buffers up front. Call ONCE at startup, as
// early as possible, on a board where the heap fragments: the 32 KiB inflate
// window and the feature buffer each need a sizeable contiguous block, and
// claiming them lazily means competing with the SD stack, filesystem and
// display driver for whatever is left.
//
// `max_feature_bytes` should cover the largest single feature the packs
// contain; the largest measured in this project is 36,259 bytes. Returns
// false if the board cannot supply the buffers, which is worth failing
// loudly at startup rather than mid-frame.
bool ReserveMvtStreamScratch(MvtStreamScratch* scratch,
                             size_t max_feature_bytes);

bool StreamMvtTile(Compression compression, CompressedChunkReader reader,
                   void* ctx, uint64_t input_offset, size_t input_size,
                   const MvtStreamOptions& options, MvtStreamScratch* scratch);

}  // namespace orcmap

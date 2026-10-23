// Host-only PMTiles / MVT inspector and real-geography preview.
// Preview visible-tile enumeration is scaffolding, not the Viewport API.
#ifdef ORCMAP_INSPECT_LABELS
#include <memory>
#include "freetype_label_target.hpp"
#endif

#include "file_byte_source.hpp"
#include "framebuffer_target.hpp"
#include "orcmap/compression.hpp"
#include "orcmap/experimental/mvt_classify.hpp"
#include "orcmap/feature.hpp"
#include "orcmap/geo.hpp"
#include "orcmap/mvt.hpp"
#include "orcmap/mvt_translate.hpp"
#include "orcmap/pmtiles.hpp"
#include "orcmap/renderer.hpp"
#include "orcmap/style.hpp"
#include "orcmap/viewport.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

constexpr size_t kHostDecompressBudget = 8u * 1024u * 1024u;

const char* CompressionName(orcmap::Compression c) {
  switch (c) {
    case orcmap::Compression::kUnknown:
      return "unknown";
    case orcmap::Compression::kNone:
      return "none";
    case orcmap::Compression::kGzip:
      return "gzip";
    case orcmap::Compression::kBrotli:
      return "brotli";
    case orcmap::Compression::kZstd:
      return "zstd";
  }
  return "unknown";
}

const char* TileTypeName(orcmap::TileType t) {
  switch (t) {
    case orcmap::TileType::kUnknown:
      return "unknown";
    case orcmap::TileType::kMvt:
      return "mvt";
    case orcmap::TileType::kPng:
      return "png";
    case orcmap::TileType::kJpeg:
      return "jpeg";
    case orcmap::TileType::kWebp:
      return "webp";
    case orcmap::TileType::kAvif:
      return "avif";
  }
  return "unknown";
}

const char* GeomName(orcmap::GeomType t) {
  switch (t) {
    case orcmap::GeomType::kPoint:
      return "point";
    case orcmap::GeomType::kLineString:
      return "linestring";
    case orcmap::GeomType::kPolygon:
      return "polygon";
    case orcmap::GeomType::kUnknown:
    default:
      return "unknown";
  }
}

const char* KindName(orcmap::FeatureKind k) {
  switch (k) {
    case orcmap::FeatureKind::kBackground:
      return "background";
    case orcmap::FeatureKind::kLand:
      return "land";
    case orcmap::FeatureKind::kWater:
      return "water";
    case orcmap::FeatureKind::kMotorway:
      return "motorway";
    case orcmap::FeatureKind::kPrimaryRoad:
      return "primary_road";
    case orcmap::FeatureKind::kSecondaryRoad:
      return "secondary_road";
    case orcmap::FeatureKind::kMinorRoad:
      return "minor_road";
    case orcmap::FeatureKind::kRail:
      return "rail";
    case orcmap::FeatureKind::kBoundary:
      return "boundary";
    case orcmap::FeatureKind::kBoundaryCountry:
      return "boundary_country";
    case orcmap::FeatureKind::kBoundaryRegion:
      return "boundary_region";
    case orcmap::FeatureKind::kBoundaryLocal:
      return "boundary_local";
    case orcmap::FeatureKind::kBuilding:
      return "building";
    case orcmap::FeatureKind::kPark:
      return "park";
    case orcmap::FeatureKind::kAirport:
      return "airport";
    case orcmap::FeatureKind::kLabelPrimary:
      return "label_primary";
    case orcmap::FeatureKind::kLabelSecondary:
      return "label_secondary";
    case orcmap::FeatureKind::kLabelMuted:
      return "label_muted";
    case orcmap::FeatureKind::kCount:
      return "count";
  }
  return "unknown";
}

std::string ValueString(const orcmap::PropertyValue& value) {
  if (std::holds_alternative<std::string>(value)) {
    return std::get<std::string>(value);
  }
  if (std::holds_alternative<double>(value)) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", std::get<double>(value));
    return buf;
  }
  if (std::holds_alternative<int64_t>(value)) {
    return std::to_string(std::get<int64_t>(value));
  }
  if (std::holds_alternative<uint64_t>(value)) {
    return std::to_string(std::get<uint64_t>(value));
  }
  if (std::holds_alternative<bool>(value)) {
    return std::get<bool>(value) ? "true" : "false";
  }
  return "";
}

struct TileStats {
  bool present = false;
  size_t stored = 0;
  size_t raw = 0;
  size_t features = 0;
  size_t paths = 0;
  size_t points = 0;
  size_t property_keys = 0;
  size_t string_bytes = 0;
  size_t classified = 0;
  size_t skipped = 0;
  size_t approx_feature_bytes = 0;
  double lookup_ms = 0;
  double decompress_ms = 0;
  double decode_ms = 0;
  double translate_ms = 0;
  double classify_ms = 0;
  double render_ms = 0;
  orcmap::FeatureTile features_owned;
};

size_t ApproxFeatureTileBytes(const orcmap::FeatureTile& tile) {
  size_t n = sizeof(orcmap::FeatureTile);
  n += tile.features.capacity() * sizeof(orcmap::Feature);
  for (const orcmap::Feature& f : tile.features) {
    n += f.layer.size();
    n += f.geometry.paths.capacity() * sizeof(orcmap::Path);
    for (const orcmap::Path& path : f.geometry.paths) {
      n += path.capacity() * sizeof(orcmap::Point);
    }
    n += f.property_keys.capacity() * sizeof(std::string);
    for (const std::string& key : f.property_keys) n += key.size();
    n += f.property_values.capacity() * sizeof(orcmap::PropertyValue);
    for (const orcmap::PropertyValue& value : f.property_values) {
      if (std::holds_alternative<std::string>(value)) {
        n += std::get<std::string>(value).size();
      }
    }
  }
  return n;
}

TileStats LoadTile(orcmap::PmTilesReader* reader, uint8_t z, uint32_t x,
                   uint32_t y, bool classify, const std::string& schema) {
  TileStats s;
  using Clock = std::chrono::steady_clock;
  auto t0 = Clock::now();
  std::vector<uint8_t> stored;
  if (!reader->GetTile(z, x, y, &stored)) return s;
  s.present = true;
  s.stored = stored.size();
  s.lookup_ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

  auto t1 = Clock::now();
  std::vector<uint8_t> raw;
  if (!orcmap::DecompressPayload(reader->Header().tile_compression, stored.data(),
                                 stored.size(), kHostDecompressBudget, &raw)) {
    std::fprintf(stderr, "decompress failed z=%u x=%u y=%u stored=%zu\n", z, x, y,
                 stored.size());
    s.present = false;
    return s;
  }
  s.raw = raw.size();
  s.decompress_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - t1).count();
  // Streaming: drop compressed bytes before decode.
  stored.clear();
  stored.shrink_to_fit();

  auto t2 = Clock::now();
  orcmap::MvtTile mvt;
  if (!orcmap::DecodeMvtTile(raw.data(), raw.size(), &mvt)) {
    std::fprintf(stderr, "mvt decode failed z=%u x=%u y=%u\n", z, x, y);
    s.present = false;
    return s;
  }
  s.decode_ms = std::chrono::duration<double, std::milli>(Clock::now() - t2).count();
  raw.clear();
  raw.shrink_to_fit();

  auto t3 = Clock::now();
  if (!orcmap::TranslateMvtToFeatureTile(mvt, &s.features_owned)) {
    s.present = false;
    return s;
  }
  s.translate_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - t3).count();
  mvt = orcmap::MvtTile{};

  if (classify) {
    auto t4 = Clock::now();
    orcmap::experimental::AssignFeatureKindsForProfile(schema, &s.features_owned);
    s.classify_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - t4).count();
  }

  s.features = s.features_owned.features.size();
  for (const orcmap::Feature& f : s.features_owned.features) {
    s.paths += f.geometry.paths.size();
    for (const orcmap::Path& path : f.geometry.paths) s.points += path.size();
    s.property_keys += f.property_keys.size();
    s.string_bytes += f.layer.size();
    for (const std::string& key : f.property_keys) s.string_bytes += key.size();
    for (const orcmap::PropertyValue& value : f.property_values) {
      if (std::holds_alternative<std::string>(value)) {
        s.string_bytes += std::get<std::string>(value).size();
      }
    }
    if (f.kind_assigned) {
      ++s.classified;
    } else {
      ++s.skipped;
    }
  }
  s.approx_feature_bytes = ApproxFeatureTileBytes(s.features_owned);
  return s;
}

void PrintHeader(const orcmap::PmTilesHeader& h) {
  std::printf("pmtiles version=%u tile_type=%s tile_compression=%s "
              "internal_compression=%s\n",
              h.version, TileTypeName(h.tile_type),
              CompressionName(h.tile_compression),
              CompressionName(h.internal_compression));
  std::printf("zoom %u..%u center_zoom=%u tiles_addressed=%llu entries=%llu "
              "contents=%llu clustered=%d\n",
              h.min_zoom, h.max_zoom, h.center_zoom,
              static_cast<unsigned long long>(h.addressed_tiles_count),
              static_cast<unsigned long long>(h.tile_entries_count),
              static_cast<unsigned long long>(h.tile_contents_count),
              h.clustered ? 1 : 0);
  std::printf("bounds lon[%g,%g] lat[%g,%g] center lon=%g lat=%g\n",
              h.min_lon_e7 / 1e7, h.max_lon_e7 / 1e7, h.min_lat_e7 / 1e7,
              h.max_lat_e7 / 1e7, h.center_lon_e7 / 1e7, h.center_lat_e7 / 1e7);
}

void PrintTileReport(uint8_t z, uint32_t x, uint32_t y, const TileStats& s) {
  std::printf("z=%u x=%u y=%u\n", z, x, y);
  if (!s.present) {
    std::printf("  missing\n");
    return;
  }
  const double ratio =
      s.raw == 0 ? 0.0 : static_cast<double>(s.stored) / static_cast<double>(s.raw);
  std::printf("  stored=%zu decompressed=%zu ratio=%.3f\n", s.stored, s.raw, ratio);
  std::printf("  features=%zu paths=%zu points=%zu property_keys=%zu "
              "string_bytes=%zu approx_feature_bytes=%zu\n",
              s.features, s.paths, s.points, s.property_keys, s.string_bytes,
              s.approx_feature_bytes);
  std::printf("  classified=%zu skipped=%zu\n", s.classified, s.skipped);
  std::printf("  HOST ms lookup=%.3f decompress=%.3f decode=%.3f translate=%.3f "
              "classify=%.3f\n",
              s.lookup_ms, s.decompress_ms, s.decode_ms, s.translate_ms,
              s.classify_ms);

  std::map<std::string, size_t> layers;
  std::map<std::string, std::map<orcmap::GeomType, size_t>> geoms;
  std::map<std::string, std::set<std::string>> keys;
  std::map<std::string, std::map<std::string, std::set<std::string>>> samples;
  uint32_t extent = 0;
  for (const orcmap::Feature& f : s.features_owned.features) {
    layers[f.layer]++;
    geoms[f.layer][f.geometry.type]++;
    if (extent == 0) extent = f.extent;
    for (size_t i = 0; i < f.property_keys.size(); ++i) {
      keys[f.layer].insert(f.property_keys[i]);
      if (i < f.property_values.size()) {
        std::set<std::string>& vals = samples[f.layer][f.property_keys[i]];
        if (vals.size() < 8) vals.insert(ValueString(f.property_values[i]));
      }
    }
  }
  std::printf("  extent=%u layers=%zu\n", extent, layers.size());
  for (const auto& kv : layers) {
    std::printf("  layer %s: %zu features", kv.first.c_str(), kv.second);
    auto git = geoms.find(kv.first);
    if (git != geoms.end()) {
      for (const auto& g : git->second) {
        std::printf(" %s=%zu", GeomName(g.first), g.second);
      }
    }
    std::printf("\n");
    auto kit = keys.find(kv.first);
    if (kit != keys.end()) {
      std::printf("    keys:");
      for (const std::string& k : kit->second) std::printf(" %s", k.c_str());
      std::printf("\n");
    }
    auto sit = samples.find(kv.first);
    if (sit != samples.end()) {
      for (const char* interesting : {"class", "subclass", "brunnel", "name",
                                      "ref", "kind", "kind_detail", "admin_level",
                                      "min_zoom", "type", "structure"}) {
        auto vit = sit->second.find(interesting);
        if (vit == sit->second.end() || vit->second.empty()) continue;
        std::printf("    %s:", interesting);
        for (const std::string& v : vit->second) {
          std::printf(" %s", v.c_str());
        }
        std::printf("\n");
      }
    }
  }

  std::map<orcmap::FeatureKind, size_t> kinds;
  std::map<std::string, size_t> skipped_layers;
  for (const orcmap::Feature& f : s.features_owned.features) {
    if (f.kind_assigned) {
      kinds[f.kind]++;
    } else {
      skipped_layers[f.layer]++;
    }
  }
  if (!kinds.empty()) {
    std::printf("  kinds:");
    for (const auto& kv : kinds) {
      std::printf(" %s=%zu", KindName(kv.first), kv.second);
    }
    std::printf("\n");
  }
  if (!skipped_layers.empty()) {
    std::printf("  skipped_layers:");
    for (const auto& kv : skipped_layers) {
      std::printf(" %s=%zu", kv.first.c_str(), kv.second);
    }
    std::printf("\n");
  }
}



int CmdHeader(const std::string& path) {
  orcmap::host::FileByteSource source(path);
  if (!source.Valid()) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return 1;
  }
  orcmap::PmTilesReader reader(&source);
  if (!reader.Open()) {
    std::fprintf(stderr, "open failed\n");
    return 1;
  }
  PrintHeader(reader.Header());
  std::vector<uint8_t> meta;
  if (reader.ReadMetadata(&meta)) {
    std::printf("metadata_bytes=%zu\n", meta.size());
    const size_t n = meta.size() < 800 ? meta.size() : 800;
    std::fwrite(meta.data(), 1, n, stdout);
    if (meta.size() > n) std::printf("...\n");
    else std::printf("\n");
  }
  return 0;
}

int CmdTile(const std::string& path, uint8_t z, uint32_t x, uint32_t y,
            const std::string& schema) {
  orcmap::host::FileByteSource source(path);
  if (!source.Valid()) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return 1;
  }
  orcmap::PmTilesReader reader(&source);
  if (!reader.Open()) {
    std::fprintf(stderr, "open failed\n");
    return 1;
  }
  PrintHeader(reader.Header());
  const TileStats s = LoadTile(&reader, z, x, y, true, schema);
  PrintTileReport(z, x, y, s);
  return s.present ? 0 : 1;
}

int CmdSample(const std::string& path, double lat, double lon, uint8_t zoom,
              int radius, const std::string& schema) {
  orcmap::host::FileByteSource source(path);
  if (!source.Valid()) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return 1;
  }
  orcmap::PmTilesReader reader(&source);
  if (!reader.Open()) {
    std::fprintf(stderr, "open failed\n");
    return 1;
  }
  PrintHeader(reader.Header());
  const orcmap::TileId center = orcmap::LatLonToTile(lat, lon, zoom);
  std::vector<size_t> stored;
  std::vector<size_t> raw;
  std::printf("sample center z=%u x=%u y=%u radius=%d\n", center.z, center.x,
              center.y, radius);
  const uint32_t n = orcmap::TilesPerAxis(zoom);
  for (int dy = -radius; dy <= radius; ++dy) {
    for (int dx = -radius; dx <= radius; ++dx) {
      const int64_t x = static_cast<int64_t>(center.x) + dx;
      const int64_t y = static_cast<int64_t>(center.y) + dy;
      if (x < 0 || y < 0 || static_cast<uint64_t>(x) >= n ||
          static_cast<uint64_t>(y) >= n) {
        continue;
      }
      const TileStats s =
          LoadTile(&reader, zoom, static_cast<uint32_t>(x),
                   static_cast<uint32_t>(y), true, schema);
      PrintTileReport(zoom, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                      s);
      if (s.present) {
        stored.push_back(s.stored);
        raw.push_back(s.raw);
      }
    }
  }
  if (stored.empty()) return 1;
  auto summarize = [](std::vector<size_t> v, const char* label) {
    std::sort(v.begin(), v.end());
    const size_t mid = v[v.size() / 2];
    std::printf("%s n=%zu min=%zu median=%zu max=%zu\n", label, v.size(), v.front(),
                mid, v.back());
  };
  summarize(stored, "stored_bytes");
  summarize(raw, "decompressed_bytes");
  return 0;
}

int CmdPreview(const std::string& path, double lat, double lon, uint8_t zoom,
               int width, int height, const std::string& out,
               const orcmap::MapStyle& style, bool verbose,
               const std::string& schema, const std::string& label_font) {
#ifndef ORCMAP_INSPECT_LABELS
  if (!label_font.empty()) {
    std::fprintf(stderr, "label preview requires ORCMAP_INSPECT_LABELS=ON\n");
    return 1;
  }
#endif
  orcmap::host::FileByteSource source(path);
  if (!source.Valid()) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return 1;
  }
  orcmap::PmTilesReader reader(&source);
  if (!reader.Open()) {
    std::fprintf(stderr, "open failed\n");
    return 1;
  }
  PrintHeader(reader.Header());

  orcmap::Viewport vp;
  vp.center_lat_deg = lat;
  vp.center_lon_deg = lon;
  vp.zoom = zoom;
  vp.width_px = width;
  vp.height_px = height;
  vp.tile_size_px = 256;

  using Clock = std::chrono::steady_clock;
  std::vector<orcmap::TileId> tiles;
  const auto e0 = Clock::now();
  if (!orcmap::EnumerateVisibleTiles(vp, &tiles)) {
    std::fprintf(stderr, "EnumerateVisibleTiles failed\n");
    return 1;
  }
  const double enumerate_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - e0).count();
  std::printf("preview lat=%.6f lon=%.6f zoom=%u %dx%d style=%s "
              "visible_tiles=%zu enumerate_ms=%.3f\n",
              lat, lon, zoom, width, height, style.id, tiles.size(),
              enumerate_ms);

  orcmap::host::FramebufferTarget fb(width, height);
  if (!orcmap::ClearMapBackground(vp, style, &fb)) return 1;
#ifdef ORCMAP_INSPECT_LABELS
  orcmap::LabelLayout labels;
  labels.BeginFrame(width, height);
  std::unique_ptr<orcmap::host::FreeTypeLabelTarget> text;
  if (!label_font.empty()) {
    text = std::make_unique<orcmap::host::FreeTypeLabelTarget>(fb, label_font.c_str(), 24);
    if (!text->valid()) { std::fprintf(stderr, "cannot load label font\n"); return 1; }
  }
#endif

  std::vector<size_t> stored;
  std::vector<size_t> raw;
  size_t present = 0;
  size_t missing = 0;
  double lookup_sum = 0, decomp_sum = 0, decode_sum = 0, translate_sum = 0,
         classify_sum = 0, render_sum = 0, report_sum = 0;
  const auto frame0 = Clock::now();
  for (const orcmap::TileId& tile : tiles) {
    TileStats s = LoadTile(&reader, tile.z, tile.x, tile.y, true, schema);
    const auto p0 = Clock::now();
    if (verbose) {
      PrintTileReport(tile.z, tile.x, tile.y, s);
    } else if (s.present) {
      std::printf("z=%u x=%u y=%u stored=%zu raw=%zu features=%zu\n", tile.z,
                  tile.x, tile.y, s.stored, s.raw, s.features);
    } else {
      std::printf("z=%u x=%u y=%u missing\n", tile.z, tile.x, tile.y);
    }
    report_sum +=
        std::chrono::duration<double, std::milli>(Clock::now() - p0).count();
    if (!s.present) {
      ++missing;
      continue;
    }
    ++present;
    stored.push_back(s.stored);
    raw.push_back(s.raw);
    lookup_sum += s.lookup_ms;
    decomp_sum += s.decompress_ms;
    decode_sum += s.decode_ms;
    translate_sum += s.translate_ms;
    classify_sum += s.classify_ms;
    const auto r0 = Clock::now();
    if (!orcmap::RenderFeatureTile(s.features_owned, tile, vp, style, &fb)) {
      std::fprintf(stderr, "render failed z=%u x=%u y=%u\n", tile.z, tile.x,
                   tile.y);
      return 1;
    }
    s.render_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - r0).count();
    render_sum += s.render_ms;
#ifdef ORCMAP_INSPECT_LABELS
    if (text) {
      orcmap::TileScreenMap map;
      uint32_t extent = 0;
      for (const auto& feature : s.features_owned.features) {
        const uint32_t next = feature.extent ? feature.extent : 4096;
        if (next != extent) {
          if (!orcmap::MakeTileScreenMap(vp, tile, next, &map)) return 1;
          extent = next;
        }
        labels.OfferPlace(feature, map, zoom, style);
      }
    }
#endif
    // Release FeatureTile before the next source tile (streaming).
    s.features_owned.features.clear();
    s.features_owned.features.shrink_to_fit();
  }
  const auto label0 = Clock::now();
#ifdef ORCMAP_INSPECT_LABELS
  if (text) {
    labels.Draw(*text, style.Rule(orcmap::FeatureKind::kLabelPrimary).color,
                 style.Rule(orcmap::FeatureKind::kBackground).color);
    const auto& ls = labels.stats();
    std::printf("HOST_LABELS offered=%u drawn=%u overlap=%u offscreen=%u unsupported=%u pool_overflow=%u invalid=%u oversized=%u\n",
                ls.offered, ls.drawn, ls.overlap, ls.offscreen, ls.unsupported_glyphs,
                ls.pool_overflow, ls.invalid_names, ls.oversized_names);
  }
#endif
  const double label_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - label0).count();
  const auto w0 = Clock::now();
  if (!fb.WritePpm(out)) {
    std::fprintf(stderr, "write failed %s\n", out.c_str());
    return 1;
  }
  const double write_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - w0).count();
  const double frame_ms =
      std::chrono::duration<double, std::milli>(Clock::now() - frame0).count();
  const double accounted = enumerate_ms + lookup_sum + decomp_sum + decode_sum +
                           translate_sum + classify_sum + render_sum +
                           report_sum + label_ms + write_ms;
  std::printf("wrote %s visible=%zu found=%zu missing=%zu HOST_frame_ms=%.3f\n",
              out.c_str(), tiles.size(), present, missing, frame_ms);
  std::printf("HOST ONLY — NOT ESP32 PERFORMANCE\n");
  std::printf("HOST ms enumerate=%.3f lookup=%.3f decompress=%.3f decode=%.3f "
              "translate=%.3f classify=%.3f render=%.3f report=%.3f "
              "labels=%.3f write_ppm=%.3f accounted=%.3f unaccounted=%.3f\n",
              enumerate_ms,
              lookup_sum, decomp_sum, decode_sum, translate_sum, classify_sum,
              render_sum, report_sum, label_ms, write_ms, accounted, frame_ms - accounted);
  if (!stored.empty()) {
    std::sort(stored.begin(), stored.end());
    std::sort(raw.begin(), raw.end());
    std::printf("stored_bytes min=%zu median=%zu max=%zu\n", stored.front(),
                stored[stored.size() / 2], stored.back());
    std::printf("decompressed_bytes min=%zu median=%zu max=%zu\n", raw.front(),
                raw[raw.size() / 2], raw.back());
  }
  return 0;
}

void Usage() {
  std::fprintf(
      stderr,
      "orcmap_pack_inspect header ARCHIVE\n"
      "orcmap_pack_inspect tile ARCHIVE Z X Y\n"
      "orcmap_pack_inspect sample ARCHIVE --lat LAT --lon LON --zoom Z [--radius N]\n"
      "orcmap_pack_inspect preview ARCHIVE --lat LAT --lon LON --zoom Z "
      "[--width W --height H --out FILE --style ID --schema ID --verbose] "
      "[--labels-font FONT.ttf (requires ORCMAP_INSPECT_LABELS=ON)]\n");
}

bool HasFlag(int argc, char** argv, const char* name) {
  const std::string key = std::string("--") + name;
  for (int i = 0; i < argc; ++i) {
    if (key == argv[i]) return true;
  }
  return false;
}

bool Flag(int argc, char** argv, const char* name, std::string* value) {
  const std::string key = std::string("--") + name;
  for (int i = 0; i < argc - 1; ++i) {
    if (key == argv[i]) {
      *value = argv[i + 1];
      return true;
    }
  }
  return false;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    Usage();
    return 1;
  }
  const std::string cmd = argv[1];
  const std::string archive = argv[2];
  std::string schema = "openmaptiles-3.16";
  Flag(argc, argv, "schema", &schema);
  if (cmd == "header") return CmdHeader(archive);
  if (cmd == "tile") {
    if (argc < 6) {
      Usage();
      return 1;
    }
    return CmdTile(archive, static_cast<uint8_t>(std::atoi(argv[3])),
                   static_cast<uint32_t>(std::strtoul(argv[4], nullptr, 10)),
                   static_cast<uint32_t>(std::strtoul(argv[5], nullptr, 10)), schema);
  }
  std::string lat_s, lon_s, zoom_s, radius_s, width_s, height_s, out, style_id, label_font;
  Flag(argc, argv, "lat", &lat_s);
  Flag(argc, argv, "lon", &lon_s);
  Flag(argc, argv, "zoom", &zoom_s);
  Flag(argc, argv, "radius", &radius_s);
  Flag(argc, argv, "width", &width_s);
  Flag(argc, argv, "height", &height_s);
  Flag(argc, argv, "out", &out);
  Flag(argc, argv, "style", &style_id);
  Flag(argc, argv, "labels-font", &label_font);
  const bool verbose = HasFlag(argc, argv, "verbose");
  if (cmd == "sample") {
    if (lat_s.empty() || lon_s.empty() || zoom_s.empty()) {
      Usage();
      return 1;
    }
    const int radius = radius_s.empty() ? 1 : std::atoi(radius_s.c_str());
    return CmdSample(archive, std::atof(lat_s.c_str()), std::atof(lon_s.c_str()),
                     static_cast<uint8_t>(std::atoi(zoom_s.c_str())), radius, schema);
  }
  if (cmd == "preview") {
    if (lat_s.empty() || lon_s.empty() || zoom_s.empty()) {
      Usage();
      return 1;
    }
    const int width = width_s.empty() ? 1280 : std::atoi(width_s.c_str());
    const int height = height_s.empty() ? 720 : std::atoi(height_s.c_str());
    if (style_id.empty()) style_id = "orcsdr-dark";
    const orcmap::MapStyle* style = orcmap::FindBuiltinStyleById(style_id.c_str());
    if (style == nullptr) {
      std::fprintf(stderr, "unknown style '%s'\n", style_id.c_str());
      return 1;
    }
    if (out.empty()) {
      out = std::string("springfield-97477-") + style->id + ".ppm";
    }
    return CmdPreview(archive, std::atof(lat_s.c_str()), std::atof(lon_s.c_str()),
                      static_cast<uint8_t>(std::atoi(zoom_s.c_str())), width,
                      height, out, *style, verbose, schema, label_font);
  }
  Usage();
  return 1;
}

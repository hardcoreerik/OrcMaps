#include "orcmap/pack.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#include "orcmap/geo.hpp"

namespace orcmap {
namespace {

bool IsIdentityPart(const std::string& value) {
  if (value.empty()) return false;
  return std::all_of(value.begin(), value.end(), [](unsigned char c) {
    return std::isalnum(c) || c == '-' || c == '.';
  });
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

bool IsSha256(const std::string& value) {
  return value.size() == 64 &&
         std::all_of(value.begin(), value.end(), [](unsigned char c) {
           return std::isxdigit(c) != 0;
         });
}

bool ValidBounds(const GeoBounds& bounds) {
  return std::isfinite(bounds.min_lon_deg) &&
         std::isfinite(bounds.max_lon_deg) &&
         std::isfinite(bounds.min_lat_deg) &&
         std::isfinite(bounds.max_lat_deg) && bounds.min_lon_deg >= -180.0 &&
         bounds.min_lon_deg <= 180.0 && bounds.max_lon_deg >= -180.0 &&
         bounds.max_lon_deg <= 180.0 &&
         bounds.min_lat_deg >= -kMercatorMaxLatDeg &&
         bounds.max_lat_deg <= kMercatorMaxLatDeg &&
         bounds.min_lat_deg <= bounds.max_lat_deg;
}

struct LonSegment {
  double min;
  double max;
};

int SplitLongitude(const GeoBounds& bounds, LonSegment out[2]) {
  if (bounds.min_lon_deg <= bounds.max_lon_deg) {
    out[0] = {bounds.min_lon_deg, bounds.max_lon_deg};
    return 1;
  }
  out[0] = {bounds.min_lon_deg, 180.0};
  out[1] = {-180.0, bounds.max_lon_deg};
  return 2;
}

bool Contains(const GeoBounds& outer, const GeoBounds& inner) {
  if (inner.min_lat_deg < outer.min_lat_deg ||
      inner.max_lat_deg > outer.max_lat_deg) {
    return false;
  }
  LonSegment outer_segments[2];
  LonSegment inner_segments[2];
  const int outer_count = SplitLongitude(outer, outer_segments);
  const int inner_count = SplitLongitude(inner, inner_segments);
  for (int i = 0; i < inner_count; ++i) {
    bool contained = false;
    for (int j = 0; j < outer_count; ++j) {
      if (inner_segments[i].min >= outer_segments[j].min &&
          inner_segments[i].max <= outer_segments[j].max) {
        contained = true;
        break;
      }
    }
    if (!contained) return false;
  }
  return true;
}

}  // namespace

std::string MakePackId(const PackManifest& manifest) {
  return Lower(manifest.source_snapshot) + "__" + Lower(manifest.schema_version) +
         "__" + Lower(manifest.content_profile) + "__" +
         Lower(manifest.region_id) + "__b" +
         std::to_string(std::llround(manifest.bounds.min_lon_deg * 1e7)) + "_" +
         std::to_string(std::llround(manifest.bounds.min_lat_deg * 1e7)) + "_" +
         std::to_string(std::llround(manifest.bounds.max_lon_deg * 1e7)) + "_" +
         std::to_string(std::llround(manifest.bounds.max_lat_deg * 1e7)) + "__z" +
         std::to_string(static_cast<unsigned>(manifest.min_zoom)) + "-" +
         std::to_string(static_cast<unsigned>(manifest.max_zoom));
}

bool IsSupportedSchemaVersion(std::string_view schema_version) {
  return schema_version == "openmaptiles-3.16" ||
         schema_version == "orcmaps-overview-1" ||
         schema_version == "protomaps-v4";
}

PackValidationError ValidatePackManifest(const PackManifest& manifest) {
  if (manifest.pack_id.empty() || manifest.pack_version.empty() ||
      manifest.display_name.empty() || manifest.region_id.empty() ||
      manifest.region_name.empty() || manifest.content_profile.empty() ||
      manifest.schema_version.empty() || manifest.source_snapshot.empty() ||
      manifest.builder.empty() || manifest.builder_version.empty() ||
      manifest.builder_commit.empty() || manifest.build_date.empty() ||
      manifest.pack_class.empty()) {
    return PackValidationError::kRequiredField;
  }
  if (!ValidBounds(manifest.bounds)) return PackValidationError::kBounds;
  if (!ZoomIsValid(manifest.min_zoom) || !ZoomIsValid(manifest.max_zoom) ||
      manifest.min_zoom > manifest.max_zoom) {
    return PackValidationError::kZoomRange;
  }
  if (manifest.pmtiles_version != 3 ||
      !IsSupportedSchemaVersion(manifest.schema_version) ||
      (manifest.pack_class != "clean" && manifest.pack_class != "permissive" &&
       manifest.pack_class != "open")) {
    return PackValidationError::kCompatibility;
  }
  if (manifest.provenance_ids.empty() ||
      std::any_of(manifest.provenance_ids.begin(), manifest.provenance_ids.end(),
                  [](const std::string& id) { return id.empty(); })) {
    return PackValidationError::kProvenance;
  }
  for (const auto& item : manifest.attribution) {
    if (item.required && item.text.empty()) {
      return PackValidationError::kAttribution;
    }
  }
  if (manifest.size_bytes == 0 || !IsSha256(manifest.output_sha256)) {
    return PackValidationError::kArchive;
  }
  if (!IsIdentityPart(manifest.region_id) ||
      !IsIdentityPart(manifest.content_profile) ||
      !IsIdentityPart(manifest.source_snapshot) ||
      !IsIdentityPart(manifest.schema_version) ||
      manifest.pack_id != MakePackId(manifest)) {
    return PackValidationError::kIdentity;
  }
  return PackValidationError::kNone;
}

bool PackCatalog::Add(PackManifest manifest) {
  if (manifest.archive_path.empty() ||
      ValidatePackManifest(manifest) != PackValidationError::kNone) {
    return false;
  }
  const auto duplicate = std::find_if(
      packs_.begin(), packs_.end(), [&](const PackManifest& existing) {
        return existing.pack_id == manifest.pack_id;
      });
  if (duplicate != packs_.end()) return false;
  packs_.push_back(std::move(manifest));
  return true;
}

const PackManifest* ResolvePack(const PackCatalog& catalog,
                                const GeoBounds& requested, uint8_t zoom) {
  if (!ValidBounds(requested) || !ZoomIsValid(zoom)) return nullptr;
  const PackManifest* best = nullptr;
  for (const auto& pack : catalog.Packs()) {
    if (zoom < pack.min_zoom || zoom > pack.max_zoom ||
        !Contains(pack.bounds, requested)) {
      continue;
    }
    if (best == nullptr || pack.priority > best->priority ||
        (pack.priority == best->priority && pack.pack_id > best->pack_id)) {
      best = &pack;
    }
  }
  return best;
}

}  // namespace orcmap

// pack-verify: run OrcMaps runtime discovery over a directory and report
// exactly what a device would install and what it would refuse.
//
// This is the offline pre-flight for an SD card. It uses the SAME
// DiscoverPacks() the firmware uses, so a card that verifies here behaves
// the same on the device; there is no second implementation to drift.
//
// Usage: pack-verify <directory>
// Exit:  0 = every manifest installed, 1 = something was rejected or the
//        directory could not be listed.

#include <cstdio>
#include <cstring>
#include <string>

#include "orcmap/pack_discovery.hpp"
#include "orcmap/pmtiles.hpp"
#include "pack_filesystem.hpp"
#include "file_byte_source.hpp"

namespace {

const char* ValidationName(orcmap::PackValidationError error) {
  switch (error) {
    case orcmap::PackValidationError::kNone: return "none";
    case orcmap::PackValidationError::kRequiredField: return "required-field";
    case orcmap::PackValidationError::kBounds: return "bounds";
    case orcmap::PackValidationError::kZoomRange: return "zoom-range";
    case orcmap::PackValidationError::kCompatibility: return "compatibility";
    case orcmap::PackValidationError::kProvenance: return "provenance";
    case orcmap::PackValidationError::kAttribution: return "attribution";
    case orcmap::PackValidationError::kArchive: return "archive";
    case orcmap::PackValidationError::kIdentity: return "identity";
  }
  return "unknown";
}

// Compares the manifest's declared bounds against the archive's own PMTiles
// header. Publishing coverage the archive cannot provide must fail preflight,
// even though cheap runtime discovery deliberately does not open every archive.
bool ReportHeaderAgreement(const orcmap::PackManifest& pack) {
  orcmap::host::FileByteSource bytes(pack.archive_path);
  if (!bytes.Valid()) {
    std::printf("      header:   UNREADABLE\n");
    return false;
  }
  orcmap::PmTilesReader reader(&bytes);
  if (!reader.Open()) {
    std::printf("      header:   not a valid PMTiles container\n");
    return false;
  }
  const orcmap::PmTilesHeader& h = reader.Header();
  const double min_lon = h.min_lon_e7 / 1e7;
  const double min_lat = h.min_lat_e7 / 1e7;
  const double max_lon = h.max_lon_e7 / 1e7;
  const double max_lat = h.max_lat_e7 / 1e7;
  std::printf("      archive:  z%u-%u  bounds %.5f,%.5f .. %.5f,%.5f\n",
              static_cast<unsigned>(h.min_zoom),
              static_cast<unsigned>(h.max_zoom), min_lon, min_lat, max_lon,
              max_lat);
  const bool manifest_wider =
      pack.bounds.min_lon_deg < min_lon - 1e-6 ||
      pack.bounds.min_lat_deg < min_lat - 1e-6 ||
      pack.bounds.max_lon_deg > max_lon + 1e-6 ||
      pack.bounds.max_lat_deg > max_lat + 1e-6;
  if (manifest_wider) {
    std::printf("      WARNING:  manifest claims coverage beyond the "
                "archive's own header;\n"
                "                the resolver will select this pack where it "
                "has no tiles\n");
  }
  const bool zoom_wider = pack.min_zoom < h.min_zoom || pack.max_zoom > h.max_zoom;
  if (zoom_wider) {
    std::printf("      WARNING:  manifest zoom range z%u-%u exceeds the "
                "archive's z%u-%u\n",
                static_cast<unsigned>(pack.min_zoom),
                static_cast<unsigned>(pack.max_zoom),
                static_cast<unsigned>(h.min_zoom),
                static_cast<unsigned>(h.max_zoom));
  }
  return !manifest_wider && !zoom_wider;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::printf("usage: pack-verify <directory>\n");
    return 2;
  }
  const std::string dir = argv[1];

  orcmap::host::PackFileSystem filesystem;
  orcmap::PackCatalog catalog;
  orcmap::DiscoveryReport report;
  const bool clean = orcmap::DiscoverPacks(filesystem, dir, &catalog, &report);

  std::printf("OrcMaps pack verification\n=========================\n");
  std::printf("preflight_contract: header-coverage-v1\n");
  std::printf("directory: %s\n\n", dir.c_str());

  if (!report.directory_listed) {
    std::printf("ERROR: directory could not be listed (does it exist?)\n");
    return 1;
  }

  std::printf("manifests found: %u\ninstalled:       %u\nrejected:        %u\n\n",
              static_cast<unsigned>(report.manifests_seen),
              static_cast<unsigned>(report.packs_added),
              static_cast<unsigned>(report.rejected.size()));

  bool headers_valid = true;
  for (const orcmap::PackManifest& pack : catalog.Packs()) {
    std::printf("  INSTALLED %s\n", pack.region_id.c_str());
    std::printf("      pack_id:  %s\n", pack.pack_id.c_str());
    std::printf("      schema:   %s   class: %s   priority: %d\n",
                pack.schema_version.c_str(), pack.pack_class.c_str(),
                pack.priority);
    std::printf("      manifest: z%u-%u  bounds %.5f,%.5f .. %.5f,%.5f\n",
                static_cast<unsigned>(pack.min_zoom),
                static_cast<unsigned>(pack.max_zoom), pack.bounds.min_lon_deg,
                pack.bounds.min_lat_deg, pack.bounds.max_lon_deg,
                pack.bounds.max_lat_deg);
    if (!ReportHeaderAgreement(pack)) headers_valid = false;
    if (pack.attribution.empty()) {
      std::printf("      credits:  (none required)\n");
    }
    for (const orcmap::AttributionInfo& info : pack.attribution) {
      std::printf("      credit:   %s  %s\n", info.text.c_str(),
                  info.url.c_str());
    }
    std::printf("\n");
  }

  for (const orcmap::RejectedPack& bad : report.rejected) {
    std::printf("  REJECTED  %s\n", bad.manifest_name.c_str());
    std::printf("      reason:   %s\n",
                orcmap::PackRejectionName(bad.rejection));
    if (bad.rejection == orcmap::PackRejection::kBadJson) {
      std::printf("      json:     %s\n",
                  orcmap::PackJsonErrorName(bad.json_error));
    }
    if (bad.rejection == orcmap::PackRejection::kInvalidManifest) {
      std::printf("      manifest: %s\n", ValidationName(bad.validation_error));
    }
    if (!bad.detail.empty()) {
      std::printf("      detail:   %s\n", bad.detail.c_str());
    }
    std::printf("\n");
  }

  if (report.packs_added == 0) {
    std::printf("RESULT: NO USABLE PACKS -- a device would show no map.\n");
    return 1;
  }
  if (!headers_valid) {
    std::printf("RESULT: FAILED -- archive header or declared coverage rejected.\n");
    return 1;
  }
  if (!clean) {
    std::printf("RESULT: %u pack(s) usable, but %u were rejected.\n",
                static_cast<unsigned>(report.packs_added),
                static_cast<unsigned>(report.rejected.size()));
    return 1;
  }
  std::printf("RESULT: OK -- all %u pack(s) would install.\n",
              static_cast<unsigned>(report.packs_added));
  return 0;
}

#include <cstdio>
#include <string>

#include "test_util.hpp"

void RunGeoTests();
void RunPmTilesTests(const std::string& fixture_path);
void RunStyleTests();
void RunAttributionTests();
void RunMvtTests(const std::string& fixture_path);
void RunFeatureTests(const std::string& mvt_fixture_path);
void RunViewportTests();
void RunClipTests();
void RunStrokeTests();
void RunRenderTests(const std::string& mvt_fixture_path);
void RunOverlayTests();
void RunCompressionTests(const std::string& pmtiles_path,
                         const std::string& mvt_path);
void RunPackTests();
void RunPackDiscoveryTests();
void RunLocationPickerTests();
void RunByteRangeTests(const std::string& fixture_path);

int main(int argc, char** argv) {
  const std::string fixture_path =
      argc > 1 ? argv[1] : "tests/fixtures/tiny.pmtiles";
  const std::string mvt_fixture_path =
      argc > 2 ? argv[2] : "tests/fixtures/tiny.mvt";

  RunGeoTests();
  RunPmTilesTests(fixture_path);
  RunStyleTests();
  RunAttributionTests();
  RunMvtTests(mvt_fixture_path);
  RunFeatureTests(mvt_fixture_path);
  RunViewportTests();
  RunClipTests();
  RunStrokeTests();
  RunRenderTests(mvt_fixture_path);
  RunOverlayTests();
  RunCompressionTests(fixture_path, mvt_fixture_path);
  RunPackTests();
  RunPackDiscoveryTests();
  RunLocationPickerTests();
  RunByteRangeTests(fixture_path);

  if (orcmap::test::g_failures == 0) {
    std::printf("PASS: all OrcMaps host tests passed\n");
    return 0;
  }
  std::printf("FAIL: %d OrcMaps host test assertion(s) failed\n",
              orcmap::test::g_failures);
  return 1;
}

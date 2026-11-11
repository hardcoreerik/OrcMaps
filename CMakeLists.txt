# OrcMaps -- standalone ESP-IDF component (repo root = component root).
#
# Portable core (geo, viewport, PMTiles, MVT, compression, feature, style,
# renderer). Host framebuffer is adapters/host, not here.
# No M5GFX/M5Unified runtime dependency. adapters/m5gfx/include is exported
# so consumers can #include "orcmap/m5gfx/display_target.hpp" and supply
# their own M5GFX component. adapters/esp_idf/include is exported for
# FileByteSource (FILE* only; the app mounts SD). Core never includes
# M5GFX.h or M5Unified.h.
# Host-side tests do NOT build through this file -- they are a separate
# standalone CMake project at tests/host/CMakeLists.txt, matching the
# pattern already used by hardcoreerik/esp-rtl-sdr, so host tests never
# need an ESP-IDF toolchain. See docs/STATUS.md for implementation state.

idf_component_register(
    SRCS "src/core/geo.cpp"
         "src/core/viewport.cpp"
         "src/core/pack.cpp"
         "src/core/pack_json.cpp"
         "src/core/pack_discovery.cpp"
         "src/core/location_picker.cpp"
         "adapters/esp_idf/pack_filesystem.cpp"
         "src/tiles/pmtiles_reader.cpp"
         "src/tiles/mvt_decoder.cpp"
         "src/tiles/mvt_stream.cpp"
         "src/tiles/feature_replay.cpp"
         "src/tiles/mvt_translate.cpp"
         "src/tiles/mvt_classify_experimental.cpp"
         "src/tiles/compression.cpp"
         "src/render/style.cpp"
         "src/render/renderer.cpp"
         "src/render/labels.cpp"
         "src/render/clip.cpp"
         "adapters/esp_idf/file_byte_source.cpp"
         "adapters/esp_idf/partition_byte_source.cpp"
         "third_party/miniz/miniz.c"
         "third_party/miniz/miniz_tinfl.c"
    INCLUDE_DIRS "include" "adapters/m5gfx/include" "adapters/esp_idf/include"
    PRIV_INCLUDE_DIRS "third_party/miniz"
    REQUIRES ""
    PRIV_REQUIRES "esp_partition"
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MINIZ_NO_DEFLATE_APIS
    MINIZ_NO_ZLIB_APIS
    MINIZ_NO_ARCHIVE_APIS
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

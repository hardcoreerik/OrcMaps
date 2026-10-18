# Standalone host unit tests (no ESP-IDF). Build directly:
#   cmake -S tests/host -B build -G "Visual Studio 18 2026"   (or any generator)
#   cmake --build build --config Release
#   ctest --test-dir build -C Release --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(orcmaps_host_tests LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ORCMAP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(orcmap_host_tests
    main.cpp
    test_geo.cpp
    test_pmtiles.cpp
    test_style.cpp
    test_attribution.cpp
    test_mvt.cpp
    test_feature.cpp
    test_viewport.cpp
    test_clip.cpp
    test_stroke.cpp
    test_render.cpp
    test_compression.cpp
    test_pack.cpp
    test_pack_discovery.cpp
    test_location_picker.cpp
    test_byte_range.cpp
    ${ORCMAP_ROOT}/src/core/geo.cpp
    ${ORCMAP_ROOT}/src/core/viewport.cpp
    ${ORCMAP_ROOT}/src/core/pack.cpp
    ${ORCMAP_ROOT}/src/core/pack_json.cpp
    ${ORCMAP_ROOT}/src/core/pack_discovery.cpp
    ${ORCMAP_ROOT}/src/core/location_picker.cpp
    ${ORCMAP_ROOT}/src/tiles/pmtiles_reader.cpp
    ${ORCMAP_ROOT}/src/tiles/mvt_decoder.cpp
    ${ORCMAP_ROOT}/src/tiles/mvt_stream.cpp
    ${ORCMAP_ROOT}/src/tiles/mvt_translate.cpp
    ${ORCMAP_ROOT}/src/tiles/mvt_classify_experimental.cpp
    ${ORCMAP_ROOT}/src/tiles/compression.cpp
    ${ORCMAP_ROOT}/src/render/style.cpp
    ${ORCMAP_ROOT}/src/render/renderer.cpp
    ${ORCMAP_ROOT}/src/render/clip.cpp
    ${ORCMAP_ROOT}/adapters/host/file_byte_source.cpp
    ${ORCMAP_ROOT}/adapters/host/pack_filesystem.cpp
    ${ORCMAP_ROOT}/adapters/host/framebuffer_target.cpp
    ${ORCMAP_ROOT}/third_party/miniz/miniz.c
    ${ORCMAP_ROOT}/third_party/miniz/miniz_tinfl.c
)

target_compile_definitions(orcmap_host_tests PRIVATE
    MINIZ_NO_DEFLATE_APIS
    MINIZ_NO_ZLIB_APIS
    MINIZ_NO_ARCHIVE_APIS
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

target_include_directories(orcmap_host_tests PRIVATE
    ${ORCMAP_ROOT}/include
    ${ORCMAP_ROOT}/third_party/miniz
    ${ORCMAP_ROOT}/adapters/host
    ${CMAKE_CURRENT_SOURCE_DIR}
)

enable_testing()
add_executable(orcmap_label_tests test_labels.cpp ${ORCMAP_ROOT}/src/render/labels.cpp
    ${ORCMAP_ROOT}/src/core/geo.cpp ${ORCMAP_ROOT}/src/core/viewport.cpp
    ${ORCMAP_ROOT}/src/render/style.cpp)
target_include_directories(orcmap_label_tests PRIVATE ${ORCMAP_ROOT}/include)
add_test(NAME orcmap_label_tests COMMAND orcmap_label_tests)
add_executable(orcmap_profile_tests test_profiles.cpp
    ${ORCMAP_ROOT}/src/tiles/mvt_classify_experimental.cpp)
target_include_directories(orcmap_profile_tests PRIVATE ${ORCMAP_ROOT}/include)
add_test(NAME orcmap_profile_tests COMMAND orcmap_profile_tests)
add_executable(orcmap_screen_bounds_tests
    test_screen_bounds.cpp
    ${ORCMAP_ROOT}/src/core/geo.cpp
    ${ORCMAP_ROOT}/src/core/viewport.cpp
    ${ORCMAP_ROOT}/src/render/style.cpp
    ${ORCMAP_ROOT}/src/render/renderer.cpp
    ${ORCMAP_ROOT}/src/render/clip.cpp
    ${ORCMAP_ROOT}/adapters/host/framebuffer_target.cpp
)
target_include_directories(orcmap_screen_bounds_tests PRIVATE
    ${ORCMAP_ROOT}/include ${ORCMAP_ROOT}/adapters/host)
add_test(NAME orcmap_screen_bounds_tests COMMAND orcmap_screen_bounds_tests)
add_executable(orcmap_tile_composition_tests test_tile_composition.cpp
    ${ORCMAP_ROOT}/src/core/geo.cpp
    ${ORCMAP_ROOT}/src/core/viewport.cpp
    ${ORCMAP_ROOT}/src/render/style.cpp
    ${ORCMAP_ROOT}/src/render/renderer.cpp
    ${ORCMAP_ROOT}/src/render/clip.cpp
    ${ORCMAP_ROOT}/adapters/host/framebuffer_target.cpp)
target_include_directories(orcmap_tile_composition_tests PRIVATE
    ${ORCMAP_ROOT}/include ${ORCMAP_ROOT}/adapters/host)
add_test(NAME orcmap_tile_composition_tests COMMAND orcmap_tile_composition_tests)
add_test(
    NAME orcmap_host_tests
    COMMAND orcmap_host_tests ${ORCMAP_ROOT}/tests/fixtures/tiny.pmtiles
            ${ORCMAP_ROOT}/tests/fixtures/tiny.mvt
)

# External-consumer build test. Proves that a project depending on OrcMaps
# the way OrcSDR eventually will -- as a compiled component, integrated
# through public headers only -- can actually build against it. See
# consumer_smoke_test.cpp and docs/ARCHITECTURE.md "Consumer integration
# test" for what this catches that tests/host doesn't.
cmake_minimum_required(VERSION 3.16)
project(orcmaps_consumer_smoke_test LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ORCMAP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# The engine implementation, compiled with full internal access -- exactly
# as it would be inside the real `orcmap` ESP-IDF component. This is NOT
# what's under test here.
add_library(orcmap_engine STATIC
    ${ORCMAP_ROOT}/src/core/geo.cpp
    ${ORCMAP_ROOT}/src/core/viewport.cpp
    ${ORCMAP_ROOT}/src/core/pack.cpp
    ${ORCMAP_ROOT}/src/tiles/pmtiles_reader.cpp
    ${ORCMAP_ROOT}/src/tiles/mvt_decoder.cpp
    ${ORCMAP_ROOT}/src/tiles/mvt_stream.cpp
    ${ORCMAP_ROOT}/src/tiles/mvt_translate.cpp
    ${ORCMAP_ROOT}/src/tiles/mvt_classify_experimental.cpp
    ${ORCMAP_ROOT}/src/tiles/compression.cpp
    ${ORCMAP_ROOT}/src/render/style.cpp
    ${ORCMAP_ROOT}/src/render/renderer.cpp
    ${ORCMAP_ROOT}/src/render/clip.cpp
    ${ORCMAP_ROOT}/src/overlays/overlay.cpp
    ${ORCMAP_ROOT}/adapters/host/file_byte_source.cpp
    ${ORCMAP_ROOT}/adapters/host/framebuffer_target.cpp
    ${ORCMAP_ROOT}/third_party/miniz/miniz.c
    ${ORCMAP_ROOT}/third_party/miniz/miniz_tinfl.c
)
target_include_directories(orcmap_engine PRIVATE
    ${ORCMAP_ROOT}/third_party/miniz
)
# PUBLIC on purpose: this is the exact set of directories a real consumer
# gets. include/ is the public API; adapters/host is a public integration
# adapter (host tools/tests use it the same way OrcSDR would use
# adapters/esp_idf). third_party/ and src/ itself are never added here --
# see consumer_smoke_test.cpp's header comment for what that enforces.
target_include_directories(orcmap_engine PUBLIC
    ${ORCMAP_ROOT}/include
    ${ORCMAP_ROOT}/adapters/host
)
target_compile_definitions(orcmap_engine PRIVATE
    MINIZ_NO_DEFLATE_APIS
    MINIZ_NO_ZLIB_APIS
    MINIZ_NO_ARCHIVE_APIS
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

# The consumer under test. If this target fails to build because it reached
# for something in src/ or third_party/, that's the check doing its job --
# fix the consumer code, don't widen this target's include dirs.
add_executable(orcmap_consumer_smoke_test consumer_smoke_test.cpp)
target_link_libraries(orcmap_consumer_smoke_test PRIVATE orcmap_engine)

enable_testing()
add_test(
    NAME orcmap_consumer_smoke_test
    COMMAND orcmap_consumer_smoke_test ${ORCMAP_ROOT}/tests/fixtures/tiny.pmtiles
            ${ORCMAP_ROOT}/tests/fixtures/tiny.mvt
            ${ORCMAP_ROOT}/tests/fixtures/tiny-gzip.pmtiles
)

add_executable(orcmap_overlay_consumer_test overlay_consumer_test.cpp)
target_link_libraries(orcmap_overlay_consumer_test PRIVATE orcmap_engine)
add_test(NAME orcmap_overlay_consumer_test COMMAND orcmap_overlay_consumer_test)

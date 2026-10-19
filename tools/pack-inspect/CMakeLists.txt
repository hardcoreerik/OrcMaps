# Host-only pack inspector / Springfield preview.
#   cmake -S tools/pack-inspect -B build-pack-inspect -G "Visual Studio 18 2026"
#   cmake --build build-pack-inspect --config Release
cmake_minimum_required(VERSION 3.16)
project(orcmaps_pack_inspect LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ORCMAP_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_executable(orcmap_pack_inspect
    main.cpp
    ${ORCMAP_ROOT}/src/core/geo.cpp
    ${ORCMAP_ROOT}/src/core/viewport.cpp
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
    ${ORCMAP_ROOT}/adapters/host/framebuffer_target.cpp
    ${ORCMAP_ROOT}/third_party/miniz/miniz.c
    ${ORCMAP_ROOT}/third_party/miniz/miniz_tinfl.c
)
target_include_directories(orcmap_pack_inspect PRIVATE
    ${ORCMAP_ROOT}/include
    ${ORCMAP_ROOT}/third_party/miniz
    ${ORCMAP_ROOT}/adapters/host
)
target_compile_definitions(orcmap_pack_inspect PRIVATE
    MINIZ_NO_DEFLATE_APIS
    MINIZ_NO_ZLIB_APIS
    MINIZ_NO_ARCHIVE_APIS
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)

option(ORCMAP_INSPECT_LABELS "Enable optional host FreeType place labels" OFF)
if(ORCMAP_INSPECT_LABELS)
  find_package(Freetype REQUIRED)
  target_sources(orcmap_pack_inspect PRIVATE
      ${ORCMAP_ROOT}/src/render/labels.cpp
      ${ORCMAP_ROOT}/adapters/host/freetype_label_target.cpp)
  target_link_libraries(orcmap_pack_inspect PRIVATE Freetype::Freetype)
  target_compile_definitions(orcmap_pack_inspect PRIVATE ORCMAP_INSPECT_LABELS=1)
  add_executable(orcmap_font_tests ${ORCMAP_ROOT}/tests/host/test_label_font.cpp
      ${ORCMAP_ROOT}/src/render/labels.cpp ${ORCMAP_ROOT}/src/render/style.cpp
      ${ORCMAP_ROOT}/src/core/geo.cpp ${ORCMAP_ROOT}/src/core/viewport.cpp
      ${ORCMAP_ROOT}/src/render/clip.cpp
      ${ORCMAP_ROOT}/adapters/host/framebuffer_target.cpp
      ${ORCMAP_ROOT}/adapters/host/freetype_label_target.cpp)
  target_include_directories(orcmap_font_tests PRIVATE ${ORCMAP_ROOT}/include ${ORCMAP_ROOT}/adapters/host)
  target_link_libraries(orcmap_font_tests PRIVATE Freetype::Freetype)
  set(ORCMAP_TEST_FONT "" CACHE FILEPATH "Real font for the optional host font test")
  if(ORCMAP_TEST_FONT)
    enable_testing()
    add_test(NAME orcmap_font_tests COMMAND orcmap_font_tests ${ORCMAP_TEST_FONT})
  endif()
endif()

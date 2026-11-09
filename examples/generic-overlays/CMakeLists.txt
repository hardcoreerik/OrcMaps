cmake_minimum_required(VERSION 3.16)
project(orcmaps_generic_overlay_demo LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)
add_executable(generic_overlay_demo main.cpp
  ${ROOT}/src/overlays/overlay.cpp ${ROOT}/src/core/geo.cpp
  ${ROOT}/src/core/viewport.cpp ${ROOT}/src/render/clip.cpp)
target_include_directories(generic_overlay_demo PRIVATE ${ROOT}/include)

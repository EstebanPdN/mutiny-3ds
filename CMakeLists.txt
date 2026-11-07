cmake_minimum_required(VERSION 3.20)
project(mutiny C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(ZLIB REQUIRED)

file(GLOB CORE_SRC src/core/*.c)
file(GLOB GAME_SRC src/game/*.c)
file(GLOB UI_SRC src/ui/*.c)

add_executable(mutiny
  src/main.c
  ${CORE_SRC} ${GAME_SRC} ${UI_SRC}
  src/platform/soft/soft_gfx.c
  src/platform/script.c
  src/platform/mac/plat_mac.c)
target_include_directories(mutiny PRIVATE src)
target_compile_options(mutiny PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(mutiny PRIVATE SDL3::SDL3 ZLIB::ZLIB m)

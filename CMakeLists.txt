cmake_minimum_required(VERSION 3.20)

project(DoubleMonumentFinderBase VERSION 1.0.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# DoubleMonumentFinderBase
#
#   doublemonumentfinder_base   static library with the search core + cubiomes
#                               (linked by the GUI's thin JNI shim)
#   DoubleMonumentFinderBase-cli standalone command line executable
#
# Dependencies are git submodules in this repository:
#   cubiomes/   https://github.com/xpple/cubiomes
#
# After cloning:
#   git submodule update --init --recursive
#
# Standalone build:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   .\build\bin\DoubleMonumentFinderBase-cli.exe --help
#
# Packaging:
#   cmake --install build --prefix dist
# ---------------------------------------------------------------------------

if(MSVC)
  message(FATAL_ERROR "MSVC is not supported. Use MSYS2 UCRT64/MinGW gcc/g++.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(DMF_CUBIOMES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cubiomes"
    CACHE PATH "Path to the cubiomes checkout")
if(NOT EXISTS "${DMF_CUBIOMES_DIR}/generator.c" OR
   NOT EXISTS "${DMF_CUBIOMES_DIR}/finders.h")
  message(FATAL_ERROR
    "cubiomes submodule is missing: ${DMF_CUBIOMES_DIR}\n"
    "Run 'git submodule update --init --recursive', or pass "
    "-DDMF_CUBIOMES_DIR=<path-to-cubiomes>.")
endif()
get_filename_component(DMF_CUBIOMES_PARENT "${DMF_CUBIOMES_DIR}" DIRECTORY)

set(DMF_CORE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/base.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/monument_search.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/monument_search_core.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/monument_search.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/monument_search_core.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Thread.h")

# Only the cubiomes sources used by the monument search. tests.c is
# deliberately excluded (it defines main) and loot is not needed.
set(DMF_CUBIOMES_SOURCES
    "${DMF_CUBIOMES_DIR}/finders.c"
    "${DMF_CUBIOMES_DIR}/carver.c"
    "${DMF_CUBIOMES_DIR}/generator.c"
    "${DMF_CUBIOMES_DIR}/layers.c"
    "${DMF_CUBIOMES_DIR}/biomenoise.c"
    "${DMF_CUBIOMES_DIR}/biomes.c"
    "${DMF_CUBIOMES_DIR}/noise.c"
    "${DMF_CUBIOMES_DIR}/terrainnoise.c"
    "${DMF_CUBIOMES_DIR}/util.c"
    "${DMF_CUBIOMES_DIR}/quadbase.c"
    "${DMF_CUBIOMES_DIR}/xradv.c")
file(GLOB DMF_CUBIOMES_FEATURE_SOURCES "${DMF_CUBIOMES_DIR}/features/*.c")
list(APPEND DMF_CUBIOMES_SOURCES ${DMF_CUBIOMES_FEATURE_SOURCES})

add_library(doublemonumentfinder_base STATIC
    ${DMF_CORE_SOURCES}
    ${DMF_CUBIOMES_SOURCES})

target_include_directories(doublemonumentfinder_base
    PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${DMF_CUBIOMES_PARENT}")

find_package(Threads REQUIRED)
target_link_libraries(doublemonumentfinder_base PUBLIC Threads::Threads)

add_executable(DoubleMonumentFinderBase-cli
    "${CMAKE_CURRENT_SOURCE_DIR}/src/cli/main.cpp")
target_link_libraries(DoubleMonumentFinderBase-cli PRIVATE doublemonumentfinder_base)

set_target_properties(doublemonumentfinder_base PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set_target_properties(DoubleMonumentFinderBase-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

target_compile_options(doublemonumentfinder_base PRIVATE -O3 -DNDEBUG -fwrapv)
target_compile_options(DoubleMonumentFinderBase-cli PRIVATE -O3 -DNDEBUG)

include(CheckCCompilerFlag)
check_c_compiler_flag("-mpopcnt" DMF_HAS_MPOPCNT)
if(DMF_HAS_MPOPCNT)
  set_source_files_properties(${DMF_CUBIOMES_SOURCES}
      PROPERTIES COMPILE_OPTIONS "-O3;-DNDEBUG;-fwrapv;-mpopcnt")
else()
  set_source_files_properties(${DMF_CUBIOMES_SOURCES}
      PROPERTIES COMPILE_OPTIONS "-O3;-DNDEBUG;-fwrapv")
endif()

if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_link_options(DoubleMonumentFinderBase-cli PRIVATE
      -static -static-libgcc -static-libstdc++)
endif()
if(NOT WIN32)
  target_link_libraries(DoubleMonumentFinderBase-cli PRIVATE m)
endif()

include(GNUInstallDirs)
install(TARGETS DoubleMonumentFinderBase-cli
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(TARGETS doublemonumentfinder_base
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DOCDIR}")

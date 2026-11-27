cmake_minimum_required(VERSION 3.21)
project(FormWerk VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/utf-8 /permissive- /D_USE_MATH_DEFINES /DNOMINMAX)
endif()
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(FORMWERK_BUILD_APP   "Desktop-Anwendung (Qt) bauen" ON)
option(FORMWERK_BUILD_TESTS "Tests bauen" ON)

find_package(OpenCASCADE REQUIRED)

# Clipper (Polygon-Offsets und -Booleans fuer den Slicer)
find_path(CLIPPER_INCLUDE_DIR clipper.hpp PATH_SUFFIXES polyclipping)
find_library(CLIPPER_LIBRARY NAMES polyclipping)
if(NOT CLIPPER_INCLUDE_DIR OR NOT CLIPPER_LIBRARY)
  message(FATAL_ERROR "Clipper (polyclipping) nicht gefunden. Linux: libpolyclipping-dev, Windows: vcpkg install polyclipping")
endif()

set(OCC_CORE_LIBS TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo
    TKTopAlgo TKPrim TKBO TKShHealing TKBool TKFillet TKOffset TKFeat TKMesh)

# --- Kern: parametrisches Bauteil, Skizzen, Features -------------------------
add_library(formwerk_core STATIC
  src/core/Sketch.cpp
  src/core/Features.cpp
  src/core/PartDocument.cpp)
target_include_directories(formwerk_core PUBLIC src ${OpenCASCADE_INCLUDE_DIR})
target_link_libraries(formwerk_core PUBLIC ${OCC_CORE_LIBS} TKSTL)

# --- Slicer fuer den Ender 3 V2 ----------------------------------------------
add_library(formwerk_slicer STATIC
  src/slicer/MeshSlicer.cpp
  src/slicer/GcodeWriter.cpp
  src/slicer/Slicer.cpp)
target_include_directories(formwerk_slicer PUBLIC src ${CLIPPER_INCLUDE_DIR})
target_link_libraries(formwerk_slicer PUBLIC formwerk_core ${CLIPPER_LIBRARY})

# --- Desktop-Anwendung ---------------------------------------------------------
if(FORMWERK_BUILD_APP)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)
  set(CMAKE_AUTOMOC ON)
  add_executable(formwerk WIN32
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/app/OcctView.cpp
    src/app/Dialogs.cpp
    src/app/ToolpathPreview.cpp)
  target_link_libraries(formwerk PRIVATE formwerk_slicer Qt6::Widgets TKService TKV3d TKOpenGl)
endif()

# --- Tests -----------------------------------------------------------------------
if(FORMWERK_BUILD_TESTS)
  enable_testing()
  add_executable(formwerk_tests tests/test_main.cpp)
  target_link_libraries(formwerk_tests PRIVATE formwerk_slicer)
  add_test(NAME formwerk_tests COMMAND formwerk_tests)
endif()

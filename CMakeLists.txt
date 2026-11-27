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
# vcpkg legt Release- und Debug-Bibliothek gleichnamig ab: beide gezielt suchen,
# sonst landet die Debug-Version (andere Laufzeitbibliothek) im Release-Build
get_filename_component(_clipper_root "${CLIPPER_INCLUDE_DIR}/../.." ABSOLUTE)
find_library(CLIPPER_LIBRARY_RELEASE NAMES polyclipping PATHS "${_clipper_root}/lib" NO_DEFAULT_PATH)
find_library(CLIPPER_LIBRARY_DEBUG NAMES polyclipping PATHS "${_clipper_root}/debug/lib" NO_DEFAULT_PATH)
if(CLIPPER_LIBRARY_RELEASE AND CLIPPER_LIBRARY_DEBUG)
  set(CLIPPER_LIBRARY optimized "${CLIPPER_LIBRARY_RELEASE}" debug "${CLIPPER_LIBRARY_DEBUG}")
elseif(CLIPPER_LIBRARY_RELEASE)
  set(CLIPPER_LIBRARY "${CLIPPER_LIBRARY_RELEASE}")
else()
  find_library(CLIPPER_LIBRARY NAMES polyclipping)
endif()
if(NOT CLIPPER_INCLUDE_DIR OR NOT CLIPPER_LIBRARY)
  message(FATAL_ERROR "Clipper (polyclipping) nicht gefunden. Linux: libpolyclipping-dev, Windows: vcpkg install polyclipping")
endif()

set(OCC_CORE_LIBS TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep TKGeomAlgo
    TKTopAlgo TKPrim TKBO TKShHealing TKBool TKFillet TKOffset TKFeat TKMesh)

# --- Kern: parametrisches Bauteil, Skizzen, Features -------------------------
add_library(formwerk_core STATIC
  src/core/Sketch.cpp
  src/core/SketchGeometry.cpp
  src/core/Features.cpp
  src/core/PartDocument.cpp
  src/core/Expression.cpp
  src/core/ModifyFeatures.cpp)
target_include_directories(formwerk_core PUBLIC src ${OpenCASCADE_INCLUDE_DIR})
# OpenCascade 7.8+ hat TKSTL in TKDESTL umbenannt (vcpkg liefert 8.x)
if(TARGET TKDESTL)
  set(OCC_STL_LIB TKDESTL)
else()
  set(OCC_STL_LIB TKSTL)
endif()
target_link_libraries(formwerk_core PUBLIC ${OCC_CORE_LIBS} ${OCC_STL_LIB})

# --- Slicer fuer den Ender 3 V2 ----------------------------------------------
add_library(formwerk_slicer STATIC
  src/slicer/MeshSlicer.cpp
  src/slicer/GcodeWriter.cpp
  src/slicer/Slicer.cpp
  src/slicer/Settings.cpp)
target_include_directories(formwerk_slicer PUBLIC src ${CLIPPER_INCLUDE_DIR})
target_link_libraries(formwerk_slicer PUBLIC formwerk_core ${CLIPPER_LIBRARY})

# --- Desktop-Anwendung ---------------------------------------------------------
if(FORMWERK_BUILD_APP)
  find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)
  set(CMAKE_AUTOMOC ON)
  add_executable(formwerk WIN32
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/app/OcctView.cpp
    src/app/Dialogs.cpp
    src/app/ToolpathPreview.cpp
    src/app/SketchCanvas.cpp
    src/app/PropertyPanel.cpp
    src/app/MarkingMenu.cpp)
  target_link_libraries(formwerk PRIVATE formwerk_slicer Qt6::Widgets Qt6::Concurrent TKService TKV3d TKOpenGl)
endif()

# --- Tests -----------------------------------------------------------------------
if(FORMWERK_BUILD_TESTS)
  enable_testing()
  add_executable(formwerk_tests tests/test_main.cpp)
  target_link_libraries(formwerk_tests PRIVATE formwerk_slicer)
  add_test(NAME formwerk_tests COMMAND formwerk_tests)
endif()

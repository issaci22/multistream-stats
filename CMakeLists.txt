cmake_minimum_required(VERSION 3.28...4.3)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(MSD_BUILD_TESTS "Build the core unit tests" ON)
option(MSD_BUILD_MOCK_PLUGIN "Build the test-only mock streaming output plugin (never packaged)" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

# Stricter warnings for our own code only (OBS/Qt headers are SYSTEM includes).
# MSVC flags are unchanged from 1.1.0; GCC/Clang get their equivalent.
if(MSVC)
  set(MSD_WARNING_FLAGS /W4 /w44265 /w44062)
else()
  set(MSD_WARNING_FLAGS -Wall -Wextra)
endif()

# ---------------------------------------------------------------------------
# msd-core: platform-independent statistics logic (no Qt, no libobs).
# Unit-tested in tests/unit.
# ---------------------------------------------------------------------------
add_library(msd-core STATIC)
target_sources(
  msd-core
  PRIVATE
    src/core/aggregate.cpp
    src/core/bitrate-meter.cpp
    src/core/format.cpp
    src/core/frame-window.cpp
    src/core/layout-math.cpp
    src/core/naming.cpp
    src/core/session-registry.cpp
    src/core/status-model.cpp
  PUBLIC
    src/core/aggregate.hpp
    src/core/bitrate-meter.hpp
    src/core/format.hpp
    src/core/frame-window.hpp
    src/core/layout-math.hpp
    src/core/metric.hpp
    src/core/naming.hpp
    src/core/observation.hpp
    src/core/session-registry.hpp
    src/core/snapshot.hpp
    src/core/status-model.hpp
)
target_include_directories(msd-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_options(msd-core PRIVATE ${MSD_WARNING_FLAGS})
# Linked into the plugin's shared module: needs -fPIC on Linux (no effect with MSVC).
set_target_properties(msd-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---------------------------------------------------------------------------
# The OBS plugin module
# ---------------------------------------------------------------------------
add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core Widgets)

target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE msd-core OBS::libobs OBS::obs-frontend-api Qt6::Core Qt6::Widgets
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/third_party")
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE ${MSD_WARNING_FLAGS})
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE MSD_PLUGIN_VERSION="${PROJECT_VERSION}")

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC OFF AUTORCC OFF)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/plugin-log.hpp
    src/obs/aitum-bridge.cpp
    src/obs/aitum-bridge.hpp
    src/obs/output-probe.cpp
    src/obs/output-probe.hpp
    src/obs/output-tracker.cpp
    src/obs/output-tracker.hpp
    src/obs/stats-collector.cpp
    src/obs/stats-collector.hpp
    src/obs/system-stats.cpp
    src/obs/system-stats.hpp
    src/ui/disclosure-arrow.cpp
    src/ui/disclosure-arrow.hpp
    src/ui/elided-label.cpp
    src/ui/elided-label.hpp
    src/ui/output-section.cpp
    src/ui/output-section.hpp
    src/ui/stat-grid.cpp
    src/ui/stat-grid.hpp
    src/ui/stats-dock.cpp
    src/ui/stats-dock.hpp
    src/ui/status-indicator.cpp
    src/ui/status-indicator.hpp
    src/ui/theme.cpp
    src/ui/theme.hpp
    src/ui/value-label.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(MSD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests/unit)
endif()

if(MSD_BUILD_MOCK_PLUGIN)
  add_subdirectory(tests/mock-outputs)
endif()

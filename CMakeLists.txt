cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

# The OBS template defaults to C++17; N Voice uses C++20 (requirements §6.1).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OBS-independent audio engine (DSP, metering, AI interfaces).
add_subdirectory(engine)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs NVoice::engine)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "N Voice for Studio requires ENABLE_FRONTEND_API and ENABLE_QT (dock UI).")
endif()

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(
  ${CMAKE_PROJECT_NAME}
  PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/config/app_settings.cpp
    src/config/app_settings.hpp
    src/config/presets.cpp
    src/config/presets.hpp
    src/obs/filter_registry.cpp
    src/obs/filter_registry.hpp
    src/obs/filter_settings.cpp
    src/obs/filter_settings.hpp
    src/obs/nvoice_filter.cpp
    src/obs/nvoice_filter.hpp
    src/ui/dashboard_dock.cpp
    src/ui/dashboard_dock.hpp
    src/ui/feature_page.cpp
    src/ui/feature_page.hpp
    src/ui/feature_row.cpp
    src/ui/feature_row.hpp
    src/ui/level_meter_widget.cpp
    src/ui/level_meter_widget.hpp
    src/ui/param_control.cpp
    src/ui/param_control.hpp
    src/ui/theme.cpp
    src/ui/theme.hpp
    src/ui/toggle_switch.cpp
    src/ui/toggle_switch.hpp
    src/ui/waveform_widget.cpp
    src/ui/waveform_widget.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Plugin manifest read by the OBS 33+ Plugin Manager (display name, id, version).
# Every value comes from buildspec.json, so bumping its version updates the manifest.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" _buildspec_json)
string(JSON _displayName GET "${_buildspec_json}" displayName)
set(_description "AI audio engine for OBS: voice cleanup, leveling and ducking for streamers")
configure_file(cmake/common/manifest.json.in "${CMAKE_CURRENT_BINARY_DIR}/manifest.json" @ONLY)
if(OS_WINDOWS)
  target_add_resource(${CMAKE_PROJECT_NAME} "${CMAKE_CURRENT_BINARY_DIR}/manifest.json")
endif()

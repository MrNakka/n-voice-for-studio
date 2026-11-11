# N Voice for Studio - Audio Engine (OBS-independent)
#
# Builds standalone (for unit tests on any platform) or as a subdirectory of
# the OBS plugin. Must not depend on libobs or Qt.

cmake_minimum_required(VERSION 3.28...3.30)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(nvoice_engine LANGUAGES CXX)
  set(NVOICE_ENGINE_STANDALONE ON)
endif()

option(NVOICE_BUILD_TESTS "Build N Voice engine unit tests" ${NVOICE_ENGINE_STANDALONE})

add_library(nvoice_engine STATIC)
add_library(NVoice::engine ALIAS nvoice_engine)

target_sources(nvoice_engine PRIVATE src/audio_engine.cpp src/auto_leveler.cpp src/compressor.cpp src/level_meter.cpp)
target_include_directories(nvoice_engine PUBLIC include)

target_compile_features(nvoice_engine PUBLIC cxx_std_20)
set_target_properties(nvoice_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Inside the plugin build, the OBS template's compiler settings apply.
# Standalone builds get strict warnings of their own.
if(NVOICE_ENGINE_STANDALONE)
  if(MSVC)
    target_compile_options(nvoice_engine PRIVATE /W4 /utf-8)
  else()
    target_compile_options(nvoice_engine PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

if(NVOICE_BUILD_TESTS)
  enable_testing()
  add_executable(nvoice_engine_tests tests/engine_tests.cpp)
  target_link_libraries(nvoice_engine_tests PRIVATE nvoice_engine)
  if(MSVC)
    target_compile_options(nvoice_engine_tests PRIVATE /W4 /utf-8)
  else()
    target_compile_options(nvoice_engine_tests PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME nvoice_engine_tests COMMAND nvoice_engine_tests)
endif()

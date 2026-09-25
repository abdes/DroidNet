# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Examples.LightBench"
  DESCRIPTION
    [=[
A dedicated demo for validating physically based lighting and exposure.
It provides a minimal reference scene and uses DemoShell for controls.
]=]
)

asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Build instructions
# ------------------------------------------------------------------------------

add_library(
  oxygen-lightbench-scene
  STATIC
  "LightScene.cpp"
  "LightScene.h"
  "ReferenceScene.h"
  "LightBenchSettings.cpp"
  "LightBenchSettingsFile.cpp"
  "LightBenchSettings.h"
)
target_include_directories(
  oxygen-lightbench-scene
  PUBLIC
    ${OXYGEN_EXAMPLES_SOURCE_DIR}
)
find_package(nlohmann_json_schema_validator CONFIG REQUIRED)
target_link_libraries(
  oxygen-lightbench-scene
  PUBLIC
    oxygen::scene
    oxygen::data
  PRIVATE
    nlohmann_json_schema_validator
)
include(JsonSchemaHelpers)
oxygen_embed_json_schemas(
  TARGET oxygen-lightbench-scene
  OUTPUT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/LightBenchSettings_schema.h"
  NAMESPACE "oxygen::examples::light_bench"
  INCLUDE_BASE_DIR "${CMAKE_CURRENT_BINARY_DIR}"
  SCHEMAS
    kLightBenchSettingsSchema
    "lightbench-settings.schema.json"
)

add_executable(
  ${META_MODULE_TARGET}
  "../main.cpp"
  "LightBenchPanel.h"
  "LightBenchPanel.cpp"
  "LightBenchConsoleBindings.cpp"
  "LightBenchConsoleBindings.h"
  "main_impl.cpp"
  "MainModule.h"
  "MainModule.cpp"
)

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Examples"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)

target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_EXAMPLES_SOURCE_DIR}
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen-lightbench-scene
    oxygen::examples-demoshell
    oxygen::platform
    oxygen::input
    oxygen::core
    oxygen::clap
    oxygen::engine
    oxygen::imgui
    oxygen::graphics-direct3d12
    oxygen::vortex
    oxygen::loader
    oxygen::scene
    oxygen::scenesync
    oxygen::data
)

if(OXYGEN_BUILD_UI_TESTS)
  target_sources(${META_MODULE_TARGET} PRIVATE "Test/Widgets_ui.cpp")
endif()

if(TARGET oxygen-vortex-exposure-test-support)
  include(GTestHelpers)
  set(
    lightbench_reference_dir
    "${PROJECT_SOURCE_DIR}/src/Oxygen/Vortex/Test/Lighting/Reference"
  )
  gtest_program(
    "Oxygen.Examples.LightBench.Tests"
    SOURCES
      "Test/Reference_test.cpp"
      "Test/Settings_test.cpp"
      "Test/SettingsFile_test.cpp"
      "Test/ConsoleBindings_test.cpp"
      "LightBenchConsoleBindings.cpp"
      "${lightbench_reference_dir}/GgxBrdf.cpp"
      "${lightbench_reference_dir}/GgxMoments.cpp"
      "${lightbench_reference_dir}/ReferenceQuadrature.cpp"
      "${lightbench_reference_dir}/MaterialDecode.cpp"
    DEPS
      oxygen-lightbench-scene
      oxygen-vortex-exposure-test-support
      oxygen::scenesync
      oxygen::console
  )
  target_compile_definitions(
    Oxygen.Examples.LightBench.Tests
    PRIVATE
      OXYGEN_LIGHTBENCH_INDOOR_SETTINGS="${CMAKE_CURRENT_SOURCE_DIR}/demo_settings_indoor.json"
  )
endif()

# ------------------------------------------------------------------------------
# Deployment instructions
# ------------------------------------------------------------------------------

asap_pop_module("${META_MODULE_NAME}")

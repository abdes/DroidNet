# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

asap_module_declare(
  MODULE_NAME "Oxygen.Vortex.Test.Support"
  DESCRIPTION
    "Shared Vortex infrastructure for correctness tests and benchmarks"
)
asap_push_module("${META_MODULE_NAME}")

# Test infrastructure is always static and is not installed with the engine.
add_library(${META_MODULE_TARGET} STATIC)
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    OUTPUT_NAME
      "${META_MODULE_NAME}"
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      "${META_MODULE_NAME}"
    FOLDER
      "Testing"
)
target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    "CpuTimingCapture.cpp"
    "CpuTimingCapture.h"
    "D3D12MemoryCapture.cpp"
    "D3D12MemoryCapture.h"
    "ResourceCreationCounter.cpp"
    "ResourceCreationCounter.h"
    "LightingWorkload.cpp"
    "LightingWorkload.h"
    "LightingWorkloadScene.cpp"
    "LightingWorkloadScene.h"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES
      "CpuTimingCapture.h"
      "D3D12MemoryCapture.h"
      "ResourceCreationCounter.h"
      "LightingWorkload.h"
      "LightingWorkloadScene.h"
)
arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_23)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
)
target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::core
    oxygen::profiling
    oxygen::scene
    oxygen::data
    nlohmann_json::nlohmann_json
)

asap_pop_module("${META_MODULE_NAME}")

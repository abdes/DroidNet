# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# Shared helpers for every Cooker test executable, including the tool tests.
# Added from Cooker/CMakeLists.txt before Tools, so oxygen_cooker_test() is
# available to Tools/*/Test.

find_package(GTest REQUIRED CONFIG)
find_package(nlohmann_json_schema_validator REQUIRED)

include(GTestHelpers)

add_library(
  Oxygen.Cooker.TestSupport
  STATIC
  "DescriptorFixtures.h"
  "Diagnostics.h"
  "FileIo.cpp"
  "FileIo.h"
  "ImportHarness.cpp"
  "ImportHarness.h"
  "JsonSchema.cpp"
  "JsonSchema.h"
  "ModelImportTestBase.cpp"
  "ModelImportTestBase.h"
  "PipelineHarness.h"
  "SceneDescriptorTestData.h"
  "SourceLayoutTestSupport.h"
  "TempDir.cpp"
  "TempDir.h"
  "TestImages.cpp"
  "TestImages.h"
  "TestImportJob.cpp"
  "TestImportJob.h"
  "TestPaths.cpp"
  "TestPaths.h"
  "TestValues.h"
)
set_target_properties(
  Oxygen.Cooker.TestSupport
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries(
  Oxygen.Cooker.TestSupport
  PUBLIC
    oxygen::cooker
    oxygen::content
    oxygen::testing
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)
target_compile_definitions(
  Oxygen.Cooker.TestSupport
  PRIVATE
    OXYGEN_COOKER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../.."
    OXYGEN_COOKER_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../Assets"
)

# Declares one Cooker test executable of a single tier.
#
#   oxygen_cooker_test(<Area> <UNIT|FUNCTIONAL|GPU>
#     SOURCES ...
#     [DEPS ...]
#     [TIMEOUT <seconds>])
#
# Produces Oxygen.Cooker.<Area>.<Unit|Functional|Gpu>.Tests, labelled
# "cooker;<tier>", linked to oxygen::cooker and Oxygen.Cooker.TestSupport.
# Default timeouts: unit 120 s, functional and gpu 600 s.
function(oxygen_cooker_test area tier)
  cmake_parse_arguments(x "" "TIMEOUT" "SOURCES;DEPS" ${ARGN})
  set(_gpu)
  if(tier STREQUAL "UNIT")
    set(_suffix Unit)
    set(_label unit)
    set(_timeout 120)
  elseif(tier STREQUAL "FUNCTIONAL")
    set(_suffix Functional)
    set(_label functional)
    set(_timeout 600)
  elseif(tier STREQUAL "GPU")
    set(_suffix Gpu)
    set(_label gpu)
    set(_timeout 600)
    set(_gpu GPU)
  else()
    message(FATAL_ERROR "oxygen_cooker_test: unknown tier '${tier}'")
  endif()
  if(x_TIMEOUT)
    set(_timeout ${x_TIMEOUT})
  endif()
  set(_name "Oxygen.Cooker.${area}.${_suffix}.Tests")
  gtest_program(
    ${_name}
    ${_gpu}
    SOURCES
      ${x_SOURCES}
    DEPS
      oxygen::cooker
      Oxygen.Cooker.TestSupport
      ${x_DEPS}
    LABELS
      cooker
      ${_label}
  )
  set_tests_properties(
    ${_name}
    PROPERTIES
      TIMEOUT
        ${_timeout}
  )
  source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX "${area}"
    FILES
      ${x_SOURCES}
  )
endfunction()

oxygen_cooker_test(
  Support
  FUNCTIONAL
  SOURCES
    "Support_functional_test.cpp"
)

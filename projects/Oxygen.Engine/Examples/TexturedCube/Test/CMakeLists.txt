# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

include(GTestHelpers)
gtest_program(
  Oxygen.Examples.TexturedCube.TextureLoading.Tests
  SOURCES
    "TextureLoadingService_test.cpp"
    "../TextureLoadingService.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../src/Oxygen/Content/Test/AssetLoader_test_fixture.cpp"
  DEPS
    oxygen::content
    oxygen::cooker
    oxygen::testing
)
target_include_directories(
  Oxygen.Examples.TexturedCube.TextureLoading.Tests
  PRIVATE
    "${OXYGEN_EXAMPLES_SOURCE_DIR}"
)

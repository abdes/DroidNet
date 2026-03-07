# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(nlohmann_json_schema_validator REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

include(GTestHelpers)

gtest_program(
  "Oxygen.Cooker.PakBuildReportSchema.Tests"
  SOURCES
    "PakBuildReportJsonSchema_test.cpp"
  DEPS
    oxygen::testing
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

gtest_program(
  "Oxygen.Cooker.PakToolPublication.Tests"
  SOURCES
    "PakToolArtifactPublication_test.cpp"
    "../ArtifactPublication.cpp"
  DEPS
    oxygen::cooker
    oxygen::testing
)

gtest_program(
  "Oxygen.Cooker.PakToolReport.Tests"
  SOURCES
    "PakToolBuildReportJson_test.cpp"
    "../ArtifactPublication.cpp"
    "../BuildReportJson.cpp"
  DEPS
    oxygen::cooker
    oxygen::testing
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)

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
  "Oxygen.Cooker.PakToolCli.Tests"
  SOURCES
    "PakToolCli_test.cpp"
    "../CliBuilder.cpp"
  DEPS
    oxygen::clap
    oxygen::testing
)
target_compile_definitions(
  Oxygen.Cooker.PakToolCli.Tests
  PRIVATE
    OXYGEN_PAKTOOL_VERSION="0.1"
)

gtest_program(
  "Oxygen.Cooker.PakToolRequestPreparation.Tests"
  SOURCES
    "PakToolRequestPreparation_test.cpp"
    "../ArtifactPublication.cpp"
    "../RequestPreparation.cpp"
  DEPS
    oxygen::cooker
    oxygen::testing
)

gtest_program(
  "Oxygen.Cooker.PakToolExecution.Tests"
  SOURCES
    "PakToolCommandExecution_test.cpp"
    "../ArtifactPublication.cpp"
    "../BuildReportJson.cpp"
    "../CommandExecution.cpp"
    "../RequestPreparation.cpp"
    "../ScriptSealing.cpp"
  DEPS
    oxygen::cooker
    oxygen::testing
    nlohmann_json::nlohmann_json
)

gtest_program(
  "Oxygen.Cooker.PakToolApp.Tests"
  SOURCES
    "PakToolApp_test.cpp"
    "../App.cpp"
    "../CliBuilder.cpp"
    "../ArtifactPublication.cpp"
    "../BuildReportJson.cpp"
    "../CommandExecution.cpp"
    "../RequestPreparation.cpp"
    "../ScriptSealing.cpp"
  DEPS
    oxygen::cooker
    oxygen::clap
    oxygen::testing
    nlohmann_json::nlohmann_json
)
target_compile_definitions(
  Oxygen.Cooker.PakToolApp.Tests
  PRIVATE
    OXYGEN_PAKTOOL_VERSION="0.1"
)

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

gtest_program(
  "Oxygen.Cooker.PakToolScriptSealing.Tests"
  SOURCES
    "PakToolScriptSealing_test.cpp"
    "../ScriptSealing.cpp"
  DEPS
    oxygen::cooker
    oxygen::testing
)

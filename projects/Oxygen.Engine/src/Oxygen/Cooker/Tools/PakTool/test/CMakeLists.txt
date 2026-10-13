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

oxygen_cooker_test(
  PakTool
  UNIT
  DEPS
    oxygen-cooker-paktool-core
    nlohmann_json_schema_validator
  SOURCES
    "ArtifactPublication_test.cpp"
    "BuildReportJson_test.cpp"
    "CliBuilder_test.cpp"
    "PakBuildReportSchema_test.cpp"
)

oxygen_cooker_test(
  PakTool
  FUNCTIONAL
  DEPS
    oxygen-cooker-paktool-core
    nlohmann_json_schema_validator
  SOURCES
    "App_functional_test.cpp"
    "ArtifactPublication_functional_test.cpp"
    "BuildReportJson_functional_test.cpp"
    "CommandExecution_functional_test.cpp"
    "RequestPreparation_functional_test.cpp"
    "ScriptSealing_functional_test.cpp"
)

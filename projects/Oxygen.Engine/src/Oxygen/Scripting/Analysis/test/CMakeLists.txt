# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Library Link Test ---------------------------------------------------------

set(link_test_target "Oxygen.Scripting.Analysis.LinkTest")
add_executable(${link_test_target} "Link_test.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_link_libraries(${link_test_target} PRIVATE oxygen::scripting-analysis)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(Analyzer SOURCES "ScriptAnalyzer_test.cpp")

find_package(nlohmann_json REQUIRED)
find_package(nlohmann_json_schema_validator REQUIRED)
include(JsonSchemaHelpers)

m_gtest_program(
  Diagnostics
  SOURCES
    "DiagnosticCatalog_test.cpp"
    "DiagnosticJson_test.cpp"
    "DiagnosticRenderer_test.cpp"
    "LuauCompiler_test.cpp"
)
target_link_libraries(
  "${META_MODULE_NAME}.Diagnostics.Tests"
  PRIVATE
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator
)
oxygen_embed_json_schemas(
  TARGET "${META_MODULE_NAME}.Diagnostics.Tests"
  OUTPUT_HEADER
    "${CMAKE_BINARY_DIR}/generated/Oxygen/Scripting/Analysis/test/ScriptDiagnostic_schema.h"
  NAMESPACE "oxygen::scripting::testing"
  INCLUDE_BASE_DIR "${CMAKE_BINARY_DIR}/generated"
  CHUNK_SIZE 8192
  SCHEMAS
    kScriptDiagnosticSchema
    "${CMAKE_CURRENT_SOURCE_DIR}/../Schemas/oxygen.script-diagnostic.schema.json"
)

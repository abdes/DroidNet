# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(Python3 COMPONENTS Interpreter REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

oxygen_cooker_test(
  ImportTool
  FUNCTIONAL
  DEPS
    oxygen-cooker-importtool-core
  SOURCES
    "BatchCommand_functional_test.cpp"
)

# -- Python CLI Tests ----------------------------------------------------------

add_test(
  NAME "Oxygen.Cooker.ImportTool.SourceAnalysis.Cli"
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/source_analysis_cli_test.py"
    "$<TARGET_FILE:oxygen-cooker-importtool>"
)
set_tests_properties(
  "Oxygen.Cooker.ImportTool.SourceAnalysis.Cli"
  PROPERTIES
    TIMEOUT
      300
    LABELS
      "cooker;functional"
)

add_test(
  NAME "Oxygen.Cooker.ImportTool.RetainedImport.Cli"
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/retained_import_cli_test.py"
    "$<TARGET_FILE:oxygen-cooker-importtool>"
)
set_tests_properties(
  "Oxygen.Cooker.ImportTool.RetainedImport.Cli"
  PROPERTIES
    TIMEOUT
      300
    LABELS
      "cooker;functional"
)

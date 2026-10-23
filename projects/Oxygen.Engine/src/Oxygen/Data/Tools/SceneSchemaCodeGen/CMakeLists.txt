# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# SceneSchemaCodeGen tests
# ------------------------------------------------------------------------------

# Data runs the generator; this file only registers the tool's own tests, with
# the interpreter verified by PythonTools.cmake.
add_test(
  NAME Oxygen.Data.SceneSchemaCodeGen.Tests
  COMMAND
    ${Python3_EXECUTABLE} -m pytest tests/ -v
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
)
set_tests_properties(
  Oxygen.Data.SceneSchemaCodeGen.Tests
  PROPERTIES
    LABELS
      "Tools;SceneSchemaCodeGen;Python"
)

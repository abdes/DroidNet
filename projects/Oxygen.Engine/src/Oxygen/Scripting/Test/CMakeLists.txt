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

add_executable("Oxygen.Scripting.LinkTest" "Link_test.cpp")
set_target_properties(
  "Oxygen.Scripting.LinkTest"
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(
  "Oxygen.Scripting.LinkTest"
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_link_libraries("Oxygen.Scripting.LinkTest" PRIVATE oxygen::scripting)
add_test(NAME "Oxygen.Scripting Link Test" COMMAND "Oxygen.Scripting.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  All
  SOURCES
    "ScriptingModule_test.cpp"
    "ScriptSourceResolver_test.cpp"
    "LuauScriptCompiler_test.cpp"
    "ScriptCompilationService_test.cpp"
)
target_compile_definitions(
  Oxygen.Scripting.All.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)

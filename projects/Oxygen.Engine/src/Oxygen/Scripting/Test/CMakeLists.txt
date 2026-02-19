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
  Module
  SOURCES
    "ScriptingModule_metadata_test.cpp"
    "ScriptingModule_phases_test.cpp"
    "ScriptingModule_lifecycle_test.cpp"
    "ScriptingModule_execution_test.cpp"
)

m_gtest_program(
  CompilationService
  SOURCES
    "ScriptCompilationService_test.cpp"
)
target_compile_definitions(
  Oxygen.Scripting.CompilationService.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)

m_gtest_program(
  Resolver
  SOURCES
    "ScriptSourceResolver_test.cpp"
)

m_gtest_program(
  LuauScriptCompiler
  SOURCES
    "LuauScriptCompiler_test.cpp"
)

m_gtest_program(
  Bindings
  SOURCES
    "Bindings_time_test.cpp"
    "Bindings_app_log_test.cpp"
    "Bindings_math_test.cpp"
    "Bindings_uuid_hash_test.cpp"
    "Bindings_events_test.cpp"
    "Bindings_input_test.cpp"
)
target_compile_definitions(
  Oxygen.Scripting.Bindings.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)

# Aggregate target that builds all scripting test targets (excludes LinkTest)
add_custom_target(
  "Oxygen.Scripting.All.Tests"
  DEPENDS
    "Oxygen.Scripting.Module.Tests"
    "Oxygen.Scripting.CompilationService.Tests"
    "Oxygen.Scripting.Resolver.Tests"
    "Oxygen.Scripting.LuauScriptCompiler.Tests"
    "Oxygen.Scripting.Bindings.Tests"
)

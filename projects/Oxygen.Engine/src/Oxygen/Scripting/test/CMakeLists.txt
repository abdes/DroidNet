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
target_link_libraries("Oxygen.Scripting.LinkTest" PRIVATE oxygen::scripting)
add_test(NAME "Oxygen.Scripting Link Test" COMMAND "Oxygen.Scripting.LinkTest")

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(
  Module
  SOURCES
    "ScriptingModule_test_fixture.h"
    "Fakes/FakeAsyncEngine.h"
    "Fakes/FakeScriptCompilationService.h"
    "ScriptingModule_metadata_test.cpp"
    "ScriptingModule_phases_test.cpp"
    "ScriptingModule_lifecycle_test.cpp"
    "ScriptingModule_execution_test.cpp"
    "ScriptingModule_slot_helpers.h"
    "ScriptingModule_scene_observer_test.cpp"
    "ScriptingModule_instances_test.cpp"
    "ScriptingModule_diagnostics_test.cpp"
)

m_gtest_program(CompilationService SOURCES "ScriptCompilationService_test.cpp")

m_gtest_program(
  Attachment
  SOURCES
    "Fakes/FakeScriptCompilationService.h"
    "SceneScriptAttachment_test.cpp"
    "ScriptSourceReload_test.cpp"
)

m_gtest_program(Resolver SOURCES "ScriptSourceResolver_test.cpp")
target_link_libraries("Oxygen.Scripting.Resolver.Tests" PRIVATE oxygen::loader)

m_gtest_program(LuauScriptCompiler SOURCES "LuauScriptCompiler_test.cpp")

m_gtest_program(Advanced SOURCES "ScriptingModule_HotReload_test.cpp")
target_link_libraries("Oxygen.Scripting.Advanced.Tests" PRIVATE oxygen::loader)

m_gtest_program(
  Bindings
  SOURCES
    "ScriptingModule_test_fixture.h"
    "Fakes/FakeAsyncEngine.h"
    "Fakes/FakeScriptCompilationService.h"
    "Bindings_app_test.cpp"
    "Bindings_conventions_test.cpp"
    "Bindings_time_test.cpp"
    "Bindings_log_test.cpp"
    "Bindings_math_test.cpp"
    "Bindings_uuid_hash_test.cpp"
    "Bindings_events_test.cpp"
    "Bindings_input_test.cpp"
    "Bindings_physics_events_test.cpp"
    "Bindings_physics_surface_test.cpp"
    "Bindings_physics_constants_test.cpp"
    "Bindings_physics_body_test.cpp"
    "Bindings_physics_character_test.cpp"
    "Bindings_physics_shape_test.cpp"
    "Bindings_physics_query_test.cpp"
    "Bindings_physics_joint_test.cpp"
    "Bindings_physics_aggregate_test.cpp"
    "Bindings_physics_articulation_test.cpp"
    "Bindings_physics_vehicle_test.cpp"
    "Bindings_physics_soft_body_test.cpp"
    "Bindings_scene_test.cpp"
    "Bindings_content_test.cpp"
    "Bindings_definitions_test.cpp"
)
target_link_libraries(
  Oxygen.Scripting.Bindings.Tests
  PRIVATE
    oxygen::input
    oxygen::physicsmodule
    oxygen::scripting-analysis
)
# The definitions test type-checks the maintained example scripts.
target_compile_definitions(
  Oxygen.Scripting.Bindings.Tests
  PRIVATE
    OXYGEN_EXAMPLE_SCENES_DIR="${OXYGEN_SOURCE_DIR}/../Examples/Content/scenes"
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

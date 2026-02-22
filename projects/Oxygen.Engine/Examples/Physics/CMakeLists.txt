# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Examples.Physics"
  DESCRIPTION
    [=[
A demo for the end-to-end Oxygen physics integration using a procedural
"Ramp Gauntlet to Bowl" scenario. The demo builds a scene from generated
primitives (inclined ramp, static/dynamic cube obstacles, hinged flippers, and
a bottom settling bowl), then exercises PhysicsModule runtime behavior through
the full gameplay path. It includes a compact ImGui control panel for runtime
reset/spawn controls and live diagnostics (active bodies, contacts, settle
state), serving as both a developer showcase and a practical validation harness
for scene hydration contracts and stable simulation behavior.
]=]
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

# ==============================================================================
# Build instructions
# ==============================================================================

# -- Physics Example -----------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "../main.cpp"
  "main_impl.cpp"
  "PhysicsDemoPanel.h"
  "PhysicsDemoPanel.cpp"
  "MainModule.h"
  "MainModule.cpp"
)
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Examples"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)
target_compile_options(${META_MODULE_TARGET} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_EXAMPLES_SOURCE_DIR}
)
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::clap
    oxygen::core
    oxygen::data
    oxygen::engine
    oxygen::examples-demoshell
    oxygen::graphics-direct3d12
    oxygen::imgui
    oxygen::input
    oxygen::loader
    oxygen::physics
    oxygen::physicsmodule
    oxygen::platform
    oxygen::renderer
    oxygen::scene
)

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")

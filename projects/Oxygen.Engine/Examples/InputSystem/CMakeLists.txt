# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Examples.InputSystem"
  DESCRIPTION [=[
An example demonstrating the Oxygen engine InputSystem: it creates and registers
typed Actions and InputActionMappings, groups mappings into InputMappingContexts
(modifier keys, ground movement, camera controls, swimming), uses ActionTriggers
(Down, Tap, Chain) and trigger chains to express actuation rules and
prerequisites, and binds engine-agnostic InputSlots (keyboard, mouse, wheel,
MouseXY) to actions. It includes a compact ImGui debug overlay that visualises
action state, analog values and per-action sparklines; the sample shows how
gameplay code queries action edge events and Axis2D values to implement camera
pan/zoom and simple movement behaviours.
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

# -- Input System Example ------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "../main.cpp"
  "../Common/AsyncEngineApp.h"
  "../Common/AppWindow.h"
  "../Common/AppWindow.cpp"
  "../Common/RenderGraph.h"
  "../Common/RenderGraph.cpp"
  "../Common/ExampleModuleBase.h"
  "../Common/ExampleModuleBase.cpp"
  # ExampleRenderLifecycle kept for compatibility but not used by this example
  "main_impl.cpp"
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
target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::platform
    oxygen::input
    oxygen::core
    oxygen::clap
    oxygen::engine
    oxygen::imgui
    oxygen::graphics-direct3d12
    oxygen::renderer
    oxygen::loader
)

# -- Utility to dump SDL3 code names -------------------------------------------

find_package(SDL3 CONFIG REQUIRED)

set(program_name "Oxygen.Tools.Sdl.PrintCodeNames")
add_executable(${program_name} "print_sdl_code_names.cpp")
set_target_properties(
  ${program_name}
  PROPERTIES
    FOLDER
      "Tools"
)
target_compile_options(${program_name} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(${program_name} PRIVATE SDL3::SDL3)

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")

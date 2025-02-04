# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.InputSystem.Example"
  DESCRIPTION "An example using the Oxygen input system"
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
  "main.cpp"
  "MainModule.h"
  "MainModule.cpp"
  "ts_init.cpp"
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
    oxygen::platform-sdl
    oxygen::input
    oxygen::core
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

# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Examples.RenderScene"
  DESCRIPTION
    [=[
Loads scenes from loose cooked indexes and PAK files through the native
AssetLoader, then renders them with Vortex and Direct3D12. DemoShell provides
interactive model import, camera, lighting, environment, and diagnostics panels.
Optional preview sunlight illuminates loaded scenes without directional lights.
]=]
)

asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Build instructions
# ------------------------------------------------------------------------------

add_executable(
  ${META_MODULE_TARGET}
  "../main.cpp"
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

target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_EXAMPLES_SOURCE_DIR}
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::examples-demoshell
    oxygen::content
    oxygen::platform
    oxygen::input
    oxygen::data
    oxygen::scene
    oxygen::scenesync
    oxygen::core
    oxygen::clap
    oxygen::engine
    oxygen::imgui
    oxygen::graphics-direct3d12
    oxygen::vortex
    oxygen::loader
)

if(WIN32)
  target_link_libraries(
    ${META_MODULE_TARGET}
    PRIVATE
      ole32
      shell32
  )
endif()

# ------------------------------------------------------------------------------
# Deployment instructions
# ------------------------------------------------------------------------------

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")

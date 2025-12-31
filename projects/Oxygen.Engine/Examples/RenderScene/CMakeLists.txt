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
  DESCRIPTION [=[
An example demonstrating how to mount an existing cooked .pak file, resolve a
Scene asset key (via embedded browse index virtual paths or manual key entry),
load the SceneAsset through the engine AssetLoader, instantiate a runtime
scene::Scene hierarchy, and render it using the standard SingleViewExample
pipeline.

The ImGui overlay is intentionally focused on PAK mounting and scene loading.
]=]
  WITHOUT_VERSION_H
)

asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Build instructions
# ------------------------------------------------------------------------------

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
  "../Common/SingleViewExample.h"
  "../Common/SingleViewExample.cpp"
  "main_impl.cpp"
  "MainModule.h"
  "MainModule.cpp"
)

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER "Examples"
    OUTPUT_NAME ${META_MODULE_NAME}
)

target_compile_options(${META_MODULE_TARGET} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})

target_link_libraries(
  ${META_MODULE_TARGET}
  PRIVATE
    oxygen::platform
    oxygen::input
    oxygen::data
    oxygen::scene
    oxygen::core
    oxygen::clap
    oxygen::engine
    oxygen::imgui
    oxygen::graphics-direct3d12
    oxygen::renderer
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

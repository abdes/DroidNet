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
  "../Common/AppWindow.cpp"
  "../Common/AppWindow.h"
  "../Common/AsyncEngineApp.h"
  "../Common/ExampleModuleBase.cpp"
  "../Common/ExampleModuleBase.h"
  "../Common/RenderGraph.cpp"
  "../Common/RenderGraph.h"
  "../Common/SingleViewExample.cpp"
  "../Common/SingleViewExample.h"
  "../Common/SkyboxManager.cpp"
  "../Common/SkyboxManager.h"
  "../Common/WorkspaceRoot.h"
  "main_impl.cpp"
  "MainModule.h"
  "MainModule.cpp"
  "OrbitCameraController.h"
  "OrbitCameraController.cpp"
  "FlyCameraController.h"
  "FlyCameraController.cpp"
  "UI/FilePicker.h"
  "UI/FilePicker.cpp"
  "UI/ImportPanel.h"
  "UI/ImportPanel.cpp"
  "UI/PakLoaderPanel.h"
  "UI/PakLoaderPanel.cpp"
  "UI/LooseCookedLoaderPanel.h"
  "UI/LooseCookedLoaderPanel.cpp"
  "UI/CameraControlPanel.h"
  "UI/CameraControlPanel.cpp"
  "UI/ContentLoaderPanel.h"
  "UI/ContentLoaderPanel.cpp"
  "UI/LightCullingDebugPanel.h"
  "UI/LightCullingDebugPanel.cpp"
  "UI/RenderingPanel.h"
  "UI/RenderingPanel.cpp"
  "UI/SettingsPanel.h"
  "UI/SettingsPanel.cpp"
  "UI/EnvironmentDebugPanel.h"
  "UI/EnvironmentDebugPanel.cpp"
  "UI/AxesWidget.h"
  "UI/AxesWidget.cpp"
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
    oxygen::examples-demoshell
    oxygen::content
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

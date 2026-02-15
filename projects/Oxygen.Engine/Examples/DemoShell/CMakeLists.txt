# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Examples.DemoShell"
  DESCRIPTION "Reusable demo UI shell for example applications"
  WITHOUT_VERSION_H
)

asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET} STATIC)
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Examples"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXYGEN_EXAMPLES_DEMOSHELL_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

set(
  OXYGEN_DEMOSHELL_HEADERS
  "ActiveScene.h"
  "DemoShell.h"
  "PanelRegistry.h"
  "Runtime/AppWindow.h"
  "Runtime/CompositionView.h"
  "Runtime/DemoAppContext.h"
  "Runtime/DemoModuleBase.h"
  "Runtime/ForwardPipeline.h"
  "Runtime/Internal/CompositionPlanner.h"
  "Runtime/Internal/FramePlanBuilder.h"
  "Runtime/PipelineFeature.h"
  "Runtime/RenderMode.h"
  "Runtime/Internal/FrameViewPacket.h"
  "Runtime/Internal/ForwardPipelineImpl.h"
  "Runtime/Internal/ViewLifecycleService.h"
  "Runtime/Internal/ViewRenderPlan.h"
  "Runtime/Internal/PipelineSettings.h"
  "Runtime/RenderingPipeline.h"
  "Services/CameraSettingsService.h"
  "Services/ContentSettingsService.h"
  "Services/DomainService.h"
  "Services/EnvironmentSettingsService.h"
  "Services/FileBrowserService.h"
  "Services/GridSettingsService.h"
  "Services/LightCullingSettingsService.h"
  "Services/PostProcessSettingsService.h"
  "Services/RenderingSettingsService.h"
  "Services/SceneLoaderService.h"
  "Services/SettingsService.h"
  "Services/SkyboxService.h"
  "Services/UiSettingsService.h"
  "UI/AxesWidget.h"
  "UI/CameraControlPanel.h"
  "UI/CameraRigController.h"
  "UI/CameraVm.h"
  "UI/ContentLoaderPanel.h"
  "UI/ContentVm.h"
  "UI/DemoPanel.h"
  "UI/DemoShellUi.h"
  "UI/DroneCameraController.h"
  "UI/EnvironmentDebugPanel.h"
  "UI/EnvironmentVm.h"
  "UI/FlyCameraController.h"
  "UI/GridPanel.h"
  "UI/GridVm.h"
  "UI/LightCullingDebugPanel.h"
  "UI/LightCullingVm.h"
  "UI/OrbitCameraController.h"
  "UI/PanelSideBar.h"
  "UI/PostProcessPanel.h"
  "UI/PostProcessVm.h"
  "UI/RenderingPanel.h"
  "UI/RenderingVm.h"
  "UI/SidePanel.h"
  "UI/StatsOverlay.h"
  "UI/StatsOverlayConfig.h"
  "UI/UiSettingsPanel.h"
  "UI/UiSettingsVm.h"
)
set(
  OXYGEN_DEMOSHELL_PRIVATE_SOURCES
  "ActiveScene.cpp"
  "DemoShell.cpp"
  "FileBrowser/imfilebrowser.h"
  "FileBrowser/LICENSE.txt"
  "FileBrowser/VERSION.txt"
  "Internal/SceneControlBlock.h"
  "PanelRegistry.cpp"
  "Runtime/AppWindow.cpp"
  "Runtime/DemoModuleBase.cpp"
  "Runtime/ForwardPipeline.cpp"
  "Runtime/Internal/CompositionPlanner.cpp"
  "Runtime/Internal/FrameViewPacket.cpp"
  "Runtime/Internal/FramePlanBuilder.cpp"
  "Runtime/Internal/ForwardPipelineImpl.cpp"
  "Runtime/Internal/PipelineSettings.cpp"
  "Runtime/Internal/ViewLifecycleService.cpp"
  "Services/CameraSettingsService.cpp"
  "Services/ContentSettingsService.cpp"
  "Services/EnvironmentSettingsService.cpp"
  "Services/FileBrowserService.cpp"
  "Services/GridSettingsService.cpp"
  "Services/LightCullingSettingsService.cpp"
  "Services/RenderingSettingsService.cpp"
  "Services/PostProcessSettingsService.cpp"
  "Services/SceneLoaderService.cpp"
  "Services/SettingsService.cpp"
  "Services/SkyboxService.cpp"
  "Services/UiSettingsService.cpp"
  "UI/AxesWidget.cpp"
  "UI/CameraControlPanel.cpp"
  "UI/CameraRigController.cpp"
  "UI/CameraVm.cpp"
  "UI/ContentLoaderPanel.cpp"
  "UI/ContentVm.cpp"
  "UI/DemoShellUi.cpp"
  "UI/DroneCameraController.cpp"
  "UI/EnvironmentDebugPanel.cpp"
  "UI/EnvironmentVm.cpp"
  "UI/FlyCameraController.cpp"
  "UI/GridPanel.cpp"
  "UI/GridVm.cpp"
  "UI/LightCullingDebugPanel.cpp"
  "UI/LightCullingVm.cpp"
  "UI/OrbitCameraController.cpp"
  "UI/PanelSideBar.cpp"
  "UI/RenderingPanel.cpp"
  "UI/RenderingVm.cpp"
  "UI/PostProcessPanel.cpp"
  "UI/PostProcessVm.cpp"
  "UI/SidePanel.cpp"
  "UI/StatsOverlay.cpp"
  "UI/UiSettingsPanel.cpp"
  "UI/UiSettingsVm.cpp"
)

if(NOT DEFINED OXYGEN_EXAMPLES_SOURCE_DIR)
  set(OXYGEN_EXAMPLES_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
endif()

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_DEMOSHELL_PRIVATE_SOURCES}
    ${OXYGEN_DEMOSHELL_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${OXYGEN_EXAMPLES_SOURCE_DIR}
    FILES ${OXYGEN_DEMOSHELL_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_23)
target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_include_directories(
  ${META_MODULE_TARGET}
  PUBLIC
    $<BUILD_INTERFACE:${OXYGEN_EXAMPLES_SOURCE_DIR}>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXYGEN_EXAMPLES_DEMOSHELL_STATIC>
)

target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::content
    oxygen::data
    oxygen::engine
    oxygen::graphics-common
    oxygen::imgui
    oxygen::input
    oxygen::platform
    oxygen::renderer
    oxygen::scene
    fmt::fmt-header-only
    nlohmann_json::nlohmann_json
)

# ------------------------------------------------------------------------------
# Deployment instructions
# ------------------------------------------------------------------------------

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")

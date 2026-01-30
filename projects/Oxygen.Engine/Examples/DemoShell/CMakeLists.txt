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
  "Runtime/DemoAppContext.h"
  "Runtime/AppWindow.h"
  "Runtime/RenderGraph.h"
  "Runtime/DemoModuleBase.h"
  "Runtime/SingleViewModuleBase.h"
  "UI/DemoPanel.h"
  "UI/DemoShellUi.h"
  "PanelRegistry.h"
  "UI/SidePanel.h"
  "UI/PanelSideBar.h"
  "Services/CameraLifecycleService.h"
  "Services/EnvironmentHydrator.h"
  "Services/SceneLoaderService.h"
  "Services/SkyboxService.h"
  "Services/LightCullingSettingsService.h"
  "Services/RenderingSettingsService.h"
  "UI/StatsOverlay.h"
  "UI/StatsOverlayConfig.h"
  "Services/SettingsService.h"
  "Services/UiSettingsService.h"
  "UI/AxesWidget.h"
  "UI/CameraControlPanel.h"
  "UI/CameraRigController.h"
  "UI/ContentLoaderPanel.h"
  "UI/EnvironmentDebugPanel.h"
  "UI/FbxLoaderPanel.h"
  "Services/FileBrowserService.h"
  "UI/ImportPanel.h"
  "UI/LightCullingDebugPanel.h"
  "UI/LooseCookedLoaderPanel.h"
  "UI/PakLoaderPanel.h"
  "UI/RenderingPanel.h"
  "UI/UiSettingsPanel.h"
  "UI/UiSettingsVm.h"
  "UI/OrbitCameraController.h"
  "UI/FlyCameraController.h"
  "UI/RenderingVm.h"
  "UI/LightCullingVm.h"
  "Services/CameraSettingsService.h"
  "UI/CameraVm.h"
)
set(
  OXYGEN_DEMOSHELL_PRIVATE_SOURCES
  "ActiveScene.cpp"
  "DemoShell.cpp"
  "UI/DemoShellUi.cpp"
  "FileBrowser/imfilebrowser.h"
  "FileBrowser/LICENSE.txt"
  "FileBrowser/VERSION.txt"
  "Internal/SceneControlBlock.h"
  "PanelRegistry.cpp"
  "Runtime/AppWindow.cpp"
  "Runtime/DemoModuleBase.cpp"
  "Runtime/RenderGraph.cpp"
  "Runtime/SingleViewModuleBase.cpp"
  "Services/CameraLifecycleService.cpp"
  "Services/EnvironmentHydrator.cpp"
  "Services/FileBrowserService.cpp"
  "Services/SceneLoaderService.cpp"
  "Services/SettingsService.cpp"
  "Services/UiSettingsService.cpp"
  "Services/SkyboxService.cpp"
  "Services/LightCullingSettingsService.cpp"
  "Services/RenderingSettingsService.cpp"
  "UI/AxesWidget.cpp"
  "UI/CameraControlPanel.cpp"
  "UI/CameraRigController.cpp"
  "UI/ContentLoaderPanel.cpp"
  "UI/EnvironmentDebugPanel.cpp"
  "UI/FlyCameraController.cpp"
  "UI/ImportPanel.cpp"
  "UI/LightCullingDebugPanel.cpp"
  "UI/LooseCookedLoaderPanel.cpp"
  "UI/OrbitCameraController.cpp"
  "UI/PakLoaderPanel.cpp"
  "UI/PanelSideBar.cpp"
  "UI/RenderingPanel.cpp"
  "UI/UiSettingsPanel.cpp"
  "UI/UiSettingsVm.cpp"
  "UI/SidePanel.cpp"
  "UI/StatsOverlay.cpp"
  "UI/LightCullingVm.cpp"
  "UI/RenderingVm.cpp"
  "Services/CameraSettingsService.cpp"
  "UI/CameraVm.cpp"
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

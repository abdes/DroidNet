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
  "DemoShell.h"
  "PanelCommands.h"
  "PanelRegistry.h"
  "Runtime/SceneActivationPolicy.h"
  "Runtime/DemoMain.h"
  "Runtime/DemoModuleBase.h"
  "Runtime/PathNormalization.h"
  "Services/CameraSettingsService.h"
  "Services/ContentSettingsService.h"
  "Services/DefaultSceneLighting.h"
  "Services/EnvironmentSceneSnapshot.h"
  "Services/PreviewSunController.h"
  "Services/DomainService.h"
  "Services/EnvironmentSettingsService.h"
  "Services/FileBrowserService.h"
  "Services/GridSettingsService.h"
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
  "UI/EnvironmentPanelCommands.h"
  "UI/EnvironmentVm.h"
  "UI/FlyCameraController.h"
  "UI/GridPanel.h"
  "UI/GridVm.h"
  "UI/OrbitCameraController.h"
  "UI/PanelSideBar.h"
  "UI/PostProcessPanel.h"
  "UI/PostProcessVm.h"
  "UI/DiagnosticsPanel.h"
  "UI/DiagnosticsVm.h"
  "UI/SidePanel.h"
  "UI/UiSettingsVm.h"
)
set(
  OXYGEN_DEMOSHELL_PRIVATE_SOURCES
  "DemoShell.cpp"
  "PanelCommands.cpp"
  "FileBrowser/imfilebrowser.h"
  "FileBrowser/LICENSE.txt"
  "FileBrowser/VERSION.txt"
  "Internal/PostProcessConsoleBindings.h"
  "Internal/PostProcessConsoleBindings.cpp"
  "PanelRegistry.cpp"
  "Runtime/DemoMain.cpp"
  "Runtime/DemoModuleBase.cpp"
  "Services/CameraSettingsService.cpp"
  "Services/ContentSettingsService.cpp"
  "Services/DefaultSceneLighting.cpp"
  "Services/EnvironmentSceneSnapshot.cpp"
  "Services/PreviewSunController.cpp"
  "Services/EnvironmentSettingsService.cpp"
  "Services/FileBrowserService.cpp"
  "Services/GridSettingsService.cpp"
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
  "UI/EnvironmentPanelCommands.cpp"
  "UI/EnvironmentVm.cpp"
  "UI/FlyCameraController.cpp"
  "UI/GridPanel.cpp"
  "UI/GridVm.cpp"
  "UI/OrbitCameraController.cpp"
  "UI/PanelSideBar.cpp"
  "UI/DiagnosticsPanel.cpp"
  "UI/DiagnosticsVm.cpp"
  "UI/PostProcessPanel.cpp"
  "UI/PostProcessVm.cpp"
  "UI/SidePanel.cpp"
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

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_23)
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
    $<$<BOOL:${OXYGEN_BUILD_UI_TESTS}>:OXYGEN_BUILD_UI_TESTS=1>
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
    oxygen::clap
    oxygen::commands
    oxygen::control
    oxygen::cooker
    oxygen::content
    oxygen::data
    oxygen::engine
    oxygen::graphics-common
    oxygen::host
    oxygen::imgui
    oxygen::input
    oxygen::platform
    oxygen::physicsmodule
    oxygen::scene
    oxygen::sceneinstantiation
    oxygen::scripting
    oxygen::vortex
    fmt::fmt-header-only
    nlohmann_json::nlohmann_json
)

if(OXYGEN_BUILD_UI_TESTS)
  if(NOT WIN32)
    message(
      FATAL_ERROR
      "The demo UI test capture backend currently requires Windows"
    )
  endif()
  target_sources(
    ${META_MODULE_TARGET}
    PRIVATE
      "test/UiTestSession.cpp"
      "test/UiTestSession.h"
      "test/UiTestAssets.cpp"
      "test/UiTestAssets.h"
  )
  target_link_libraries(
    ${META_MODULE_TARGET}
    PRIVATE
      gdi32
      user32
  )

  # The runner hosts the DemoShell on an empty scene and runs the DemoShell's
  # own UI tests, so they need no example application.
  add_executable(
    ${META_MODULE_TARGET}-ui-runner
    "test/UiRunnerMain.cpp"
    "test/UiRunnerModule.cpp"
    "test/UiRunnerModule.h"
    "test/FileBrowser_ui.cpp"
    "test/FileBrowser_ui.h"
  )
  set_target_properties(
    ${META_MODULE_TARGET}-ui-runner
    PROPERTIES
      FOLDER
        "Examples"
      OUTPUT_NAME
        "${META_MODULE_NAME}.UiRunner"
  )
  target_compile_features(${META_MODULE_TARGET}-ui-runner PRIVATE cxx_std_23)
  target_include_directories(
    ${META_MODULE_TARGET}-ui-runner
    PRIVATE
      ${OXYGEN_EXAMPLES_SOURCE_DIR}
  )
  target_link_libraries(
    ${META_MODULE_TARGET}-ui-runner
    PRIVATE
      oxygen::host-main
      ${META_MODULE_TARGET}
      oxygen::platform
      oxygen::input
      oxygen::data
      oxygen::scene
      oxygen::core
      oxygen::clap
      oxygen::engine
      oxygen::imgui
      oxygen::graphics-direct3d12
      oxygen::vortex
  )
endif()

arrange_target_files_for_ide(${META_MODULE_TARGET})

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
  add_subdirectory("test")
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

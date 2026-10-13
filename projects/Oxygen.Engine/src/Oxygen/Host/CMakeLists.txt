# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Host"
  DESCRIPTION
    "Runtime host: startup and shutdown, main window, view publishing and scene hand-off"
)
asap_push_module("${META_MODULE_NAME}")

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_HOST_EXPORTS"
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
  OXYGEN_HOST_HEADERS
  "api_export.h"
  "HostConfig.h"
  "HostModule.h"
  "HostWindow.h"
  "ImGuiLayer.h"
  "MainViewOptions.h"
  "PersistenceLock.h"
  "RunHost.h"
  "RuntimeCli.h"
  "SceneStage.h"
  "ViewPublisher.h"
  "GpuWorkOverlay.h"
  "ViewportToolbar.h"
)
set(
  OXYGEN_HOST_PRIVATE_SOURCES
  "HostModule.cpp"
  "HostWindow.cpp"
  "MainViewOptions.cpp"
  "PersistenceLock.cpp"
  "RunHost.cpp"
  "RuntimeCli.cpp"
  "SceneStage.cpp"
  "ViewPublisher.cpp"
  "GpuWorkOverlay.cpp"
  "ViewportToolbar.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_HOST_PRIVATE_SOURCES}
    ${OXYGEN_HOST_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_HOST_HEADERS}
)

arrange_target_files_for_ide(
  ${META_MODULE_TARGET}
  EXCLUDE_PATTERNS
    "Main\\.cpp"
)

target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_HOST_STATIC>
)

# The public headers expose the engine, platform, renderer, ImGui and command
# line types a host module and its entry point work with. The input system and
# the backend loader are used only to start the engine.
target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::clap
    oxygen::config
    oxygen::console
    oxygen::control
    oxygen::core
    oxygen::engine
    oxygen::graphics-common
    oxygen::imgui
    oxygen::oxco
    oxygen::platform
    oxygen::scene
    oxygen::vortex
  PRIVATE
    oxygen::input
    oxygen::loader
)

# ------------------------------------------------------------------------------
# Entry point
# ------------------------------------------------------------------------------

# The shared `main`: logging setup, the type registry and the call to the
# application's `MainImpl`. Always static, so the linker takes `main` from it
# for every executable that links it.
add_library(oxygen-host-main STATIC "Main.cpp")
add_library(oxygen::host-main ALIAS oxygen-host-main)
set_target_properties(
  oxygen-host-main
  PROPERTIES
    OUTPUT_NAME
      "${META_MODULE_NAME}.Main"
    FOLDER
      "Libraries"
)
target_link_libraries(
  oxygen-host-main
  PUBLIC
    oxygen::base
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::cs-init>
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("test")
endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
    oxygen-host-main
)

asap_pop_module("${META_MODULE_NAME}")

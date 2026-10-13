# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.ImGui"
  DESCRIPTION "Integrates ImGui as a module in Oxygen Engine."
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(imgui CONFIG REQUIRED)

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
      "OXGN_IMGUI_EXPORTS"
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
  OXYGEN_IMGUI_HEADERS
  "api_export.h"
  "Console/ConsoleUiState.h"
  "Console/ConsolePanel.h"
  "Console/CommandPalette.h"
  "Console/ConsoleHost.h"
  "Icons/IconsOxygenIcons.h"
  "Icons/OxygenIcons.h"
  "ImGuiModule.h"
  "Styles/FontAwesome-400.h"
  "Styles/IconsFontAwesome.h"
  "Styles/Spectrum.h"
  "Widgets/GpuWorkChart.h"
  "Widgets/StatsOverlay.h"
  "Widgets/Toolbar.h"
)
set(
  OXYGEN_IMGUI_PRIVATE_SOURCES
  "Console/ConsoleUiState.cpp"
  "Console/ConsolePanel.cpp"
  "Console/CommandPalette.cpp"
  "Console/ConsoleHost.cpp"
  "Icons/OxygenIcons.cpp"
  "ImGuiModule.cpp"
  "PlatformBackend.cpp"
  "PlatformBackend.h"
  "Styles/Spectrum.cpp"
  "Styles/SourceSansProRegular.cpp"
  "Styles/FontAwesome-400.cpp"
  "Widgets/GpuWorkChart.cpp"
  "Widgets/StatsOverlay.cpp"
  "Widgets/Toolbar.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_IMGUI_PRIVATE_SOURCES}
    ${OXYGEN_IMGUI_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_IMGUI_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    OXYGEN_IMGUI_USE_DARK_THEME
  PUBLIC
    $<$<BOOL:${OXYGEN_BUILD_UI_TESTS}>:IMGUI_ENABLE_TEST_ENGINE=1>
    $<$<BOOL:${OXYGEN_BUILD_UI_TESTS}>:IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_IMGUI_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::core
    oxygen::console
    oxygen::graphics-common
    oxygen::engine
    oxygen::platform
    imgui::imgui
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

#if(OXYGEN_BUILD_TESTS)
#  add_subdirectory("test")
#endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")

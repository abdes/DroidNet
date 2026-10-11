# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Platform"
  DESCRIPTION "Platform abstraction layer"
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
if(OXYGEN_BUILD_FULL_ENGINE)
  find_package(SDL3 CONFIG REQUIRED)
  find_package(imgui CONFIG REQUIRED)
endif()

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Input vocabulary target
#
# Keys, mouse buttons, input slots and input events: platform-neutral data used
# by the Input module, the cooker and the SDL backend. It has no windowing or
# SDL dependency, so it also builds in reusable (non-full-engine) selections.
# ------------------------------------------------------------------------------

set(OXYGEN_PLATFORM_INPUT_TARGET "${META_MODULE_TARGET}-input")
add_library(${OXYGEN_PLATFORM_INPUT_TARGET})
add_library(
  ${META_MODULE_TARGET_ALIAS}-input
  ALIAS ${OXYGEN_PLATFORM_INPUT_TARGET}
)
set_target_properties(
  ${OXYGEN_PLATFORM_INPUT_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_PLIN_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME}.Input,${META_MODULE_NAME}.Input-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}.Input
)

set(
  OXYGEN_PLATFORM_INPUT_HEADERS
  "Input.h"
  "InputApiExport.h"
  "InputEvent.h"
  "Types.h"
)
set(OXYGEN_PLATFORM_INPUT_SOURCES "Input.cpp")

# The SDL backend's files live in this folder too. They are listed here, ahead
# of both targets, so each target's IDE inventory can skip the other's files.
set(
  OXYGEN_PLATFORM_HEADERS
  "Display.h"
  "Platform.h"
  "PlatformEvent.h"
  "Window.h"
  "SDL/Wrapper.h"
  "ImGui/ImGuiSdl3Backend.h"
  "api_export.h"
)
set(
  OXYGEN_PLATFORM_PRIVATE_SOURCES
  "AsyncOps.cpp"
  "Detail/Window_ManagerInterface.h"
  "Platform.cpp"
  # SDL Specific
  "SDL/Wrapper.cpp"
  "SDL/Window.cpp"
  "SDL/Display.cpp"
  "SDL/EventPump.cpp"
  "SDL/WindowManager.cpp"
  "SDL/InputEvents.cpp"
  "SDL/Init.cpp"
  "SDL/SdlEventUtils.cpp"
  # ImGui
  "ImGui/imgui_impl_sdl3.h"
  "ImGui/imgui_impl_sdl3.cpp"
  "ImGui/ImGuiSdl3Backend.cpp"
)

function(oxygen_platform_exact_file_patterns out_var)
  set(patterns)
  foreach(file IN LISTS ARGN)
    string(REPLACE "." "[.]" escaped "${file}")
    list(APPEND patterns "^${escaped}$")
  endforeach()
  set(${out_var} "${patterns}" PARENT_SCOPE)
endfunction()
oxygen_platform_exact_file_patterns(
  OXYGEN_PLATFORM_INPUT_FILE_PATTERNS
  ${OXYGEN_PLATFORM_INPUT_HEADERS}
  ${OXYGEN_PLATFORM_INPUT_SOURCES}
)
oxygen_platform_exact_file_patterns(
  OXYGEN_PLATFORM_BACKEND_FILE_PATTERNS
  ${OXYGEN_PLATFORM_HEADERS}
  ${OXYGEN_PLATFORM_PRIVATE_SOURCES}
)

target_sources(
  ${OXYGEN_PLATFORM_INPUT_TARGET}
  PRIVATE
    ${OXYGEN_PLATFORM_INPUT_SOURCES}
    ${OXYGEN_PLATFORM_INPUT_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_PLATFORM_INPUT_HEADERS}
)

arrange_target_files_for_ide(
  ${OXYGEN_PLATFORM_INPUT_TARGET}
  EXCLUDE_PATTERNS
    ${OXYGEN_PLATFORM_BACKEND_FILE_PATTERNS}
)

target_compile_features(${OXYGEN_PLATFORM_INPUT_TARGET} PUBLIC cxx_std_23)
target_compile_definitions(
  ${OXYGEN_PLATFORM_INPUT_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_PLIN_STATIC>
)

target_link_libraries(
  ${OXYGEN_PLATFORM_INPUT_TARGET}
  PUBLIC
    fmt::fmt-header-only
    oxygen::base
    oxygen::composition
    oxygen::core
)

# ------------------------------------------------------------------------------
# Main module target (SDL backend; full engine only)
# ------------------------------------------------------------------------------

if(NOT OXYGEN_BUILD_FULL_ENGINE)
  if(OXYGEN_BUILD_TESTS)
    add_subdirectory("Test")
  endif()
  oxygen_module_install(
    EXPORT ${META_MODULE_NAMESPACE}
    TARGETS
      ${OXYGEN_PLATFORM_INPUT_TARGET}
  )
  asap_pop_module("${META_MODULE_NAME}")
  return()
endif()

add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_PLAT_EXPORTS"
    OUTPUT_NAME
      $<IF:$<BOOL:${BUILD_SHARED_LIBS}>,${META_MODULE_NAME},${META_MODULE_NAME}-static>
    VERSION
      ${META_VERSION}
    SOVERSION
      ${META_VERSION_MAJOR}
    PROJECT_LABEL
      ${META_MODULE_NAME}
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_PLATFORM_PRIVATE_SOURCES}
    ${OXYGEN_PLATFORM_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_PLATFORM_HEADERS}
)

arrange_target_files_for_ide(
  ${META_MODULE_TARGET}
  EXCLUDE_PATTERNS
    ${OXYGEN_PLATFORM_INPUT_FILE_PATTERNS}
)

target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<BOOL:${OXYGEN_BUILD_UI_TESTS}>:IMGUI_ENABLE_TEST_ENGINE=1>
    $<$<BOOL:${OXYGEN_BUILD_UI_TESTS}>:IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_PLAT_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    fmt::fmt-header-only
    oxygen::base
    oxygen::composition
    oxygen::oxco
    oxygen::core
    ${META_MODULE_TARGET_ALIAS}-input
    SDL3::SDL3
    imgui::imgui
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Test")
endif()

# ==============================================================================
# Deployment instructions
# ==============================================================================

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${OXYGEN_PLATFORM_INPUT_TARGET}
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")

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
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)

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

set(
  OXYGEN_PLATFORM_HEADERS
  "Display.h"
  "Input.h"
  "InputEvent.h"
  "Platform.h"
  "PlatformEvent.h"
  "Types.h"
  "Window.h"
  "api_export.h"
)
set(
  OXYGEN_PLATFORM_PRIVATE_SOURCES
  "AsyncOps.cpp"
  "Detail/Window_ManagerInterface.h"
  "Input.cpp"
  "Platform.cpp"
  # SDL Specific
  "SDL/Wrapper.h"
  "SDL/Wrapper.cpp"
  "SDL/Window.cpp"
  "SDL/Display.cpp"
  "SDL/EventPump.cpp"
  "SDL/WindowManager.cpp"
  "SDL/InputEvents.cpp"
  "SDL/Init.cpp"
  "SDL/SdlEventUtils.cpp"
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

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_PLAT_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    fmt::fmt-header-only
    oxygen::base
    oxygen::composition
    oxygen::oxco
    SDL3::SDL3
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
    ${META_MODULE_TARGET}
)

asap_pop_module("${META_MODULE_NAME}")

# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Commands"
  DESCRIPTION
    "Commands: typed definitions, argument binding, availability, policy and execution."
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(nlohmann_json REQUIRED)

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
      "OXGN_CMDS_EXPORTS"
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
  OXYGEN_COMMANDS_HEADERS
  "api_export.h"
  "Command.h"
  "CommandRegistry.h"
  "Params.h"
)
set(
  OXYGEN_COMMANDS_PRIVATE_SOURCES
  "Command.cpp"
  "CommandRegistry.cpp"
  "Params.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_COMMANDS_PRIVATE_SOURCES}
    ${OXYGEN_COMMANDS_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_COMMANDS_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_23)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_CMDS_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::oxco
    nlohmann_json::nlohmann_json
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

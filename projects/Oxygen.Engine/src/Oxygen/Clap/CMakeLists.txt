# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Clap"
  DESCRIPTION "Text wrapping and column layout utilities"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
find_package(magic_enum CONFIG REQUIRED)

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
      "OXGN_CLP_EXPORTS"
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
  OXYGEN_CLAP_HEADERS
  "api_export.h"
  "Cli.h"
  "Command.h"
  "CommandLineContext.h"
  "Option.h"
  "OptionValue.h"
  "OptionValuesMap.h"
  "ValueSemantics.h"
  "Detail/ParseValue.h"
  "Fluent/CliBuilder.h"
  "Fluent/CommandBuilder.h"
  "Fluent/DSL.h"
  "Fluent/OptionBuilder.h"
  "Fluent/OptionValueBuilder.h"
  "Fluent/PositionalOptionBuilder.h"
  "Parser/Context.h"
  "Parser/Events.h"
  "Parser/Parser.h"
  "Parser/States.h"
  "Parser/Tokenizer.h"
)

set(
  OXYGEN_CLAP_PRIVATE_SOURCES
  "Cli.cpp"
  "Command.cpp"
  "Option.cpp"
  "Parser/Parser.cpp"
  "Parser/Tokenizer.cpp"
  "Fluent/CliBuilder.cpp"
  "Fluent/OptionBuilder.cpp"
  "Internal/Args.cpp"
  "Internal/Args.h"
  "Internal/Errors.cpp"
  "Internal/Errors.h"
  "Internal/ValueDescriptor.h"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_CLAP_PRIVATE_SOURCES}
    ${OXYGEN_CLAP_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${OXYGEN_SOURCE_DIR}
    FILES ${OXYGEN_CLAP_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_20)
target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_CLP_STATIC>
)

target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    "LOGURU_USE_FMTLIB=1"
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::textwrap
    fmt::fmt-header-only
    magic_enum::magic_enum
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
  add_subdirectory("Test")
endif()

# ------------------------------------------------------------------------------
# Examples
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Examples")
  add_subdirectory("Examples")
endif()

# ------------------------------------------------------------------------------
# API Documentation
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_DOCS AND OXYGEN_WITH_DOXYGEN)
  asap_with_doxygen(
    MODULE_NAME ${META_MODULE_TARGET}
    VERSION ${META_VERSION}
    TITLE "\"Base\""
    BRIEF "\"Base infrastructure for Oxygen\""
    INPUT_PATH "${CMAKE_CURRENT_SOURCE_DIR}"
  )
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

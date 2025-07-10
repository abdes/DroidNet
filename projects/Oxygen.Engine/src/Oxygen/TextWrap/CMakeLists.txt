# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.TextWrap"
  DESCRIPTION "Text wrapping and column layout utilities"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)

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
      "OXGN_TXW_EXPORTS"
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
  OXYGEN_TEXTWRAP_HEADERS
  "api_export.h"
  "TextWrap.h"
)
set(
  OXYGEN_TEXTWRAP_PRIVATE_SOURCES
  "Internal/Tokenizer.h"
  "Internal/Tokenizer.cpp"
  "TextWrap.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_TEXTWRAP_PRIVATE_SOURCES}
    ${OXYGEN_TEXTWRAP_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${OXYGEN_SOURCE_DIR}
    FILES ${OXYGEN_TEXTWRAP_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_20)
target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_TXW_STATIC>
)

target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    "LOGURU_USE_FMTLIB=1"
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)

target_link_libraries(${META_MODULE_TARGET} PUBLIC oxygen::base)

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

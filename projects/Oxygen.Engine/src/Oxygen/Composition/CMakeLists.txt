# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Composition"
  DESCRIPTION
    "Basic infrastructure to support object composition and non-RTTI type system"
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
      "OXYGEN_COMP_EXPORTS"
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
    "Composition.cpp"
    "TypeSystem.cpp"
    "InitializeTypeRegistry.cpp"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES
      "api_export.h"
      "ComponentMacros.h"
      "Composition.h"
      "Named.h"
      "Object.h"
      "ObjectMetaData.h"
      "TypeSystem.h"
)

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_20)
target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXYGEN_COMP_STATIC>
)

target_link_libraries(${META_MODULE_TARGET} PRIVATE oxygen::base)

# ------------------------------------------------------------------------------
# DLL for TypeRegistry initialization function
# Used when the composition library is built as static library, but the program
# may load libraries at runtime that link to the composition library.
# ------------------------------------------------------------------------------
if(NOT BUILD_SHARED_LIBS)
  set(ts_init_target oxygen-ts-init)
  add_library(
    ${ts_init_target}
    SHARED
    "InitializeTypeRegistry.cpp"
    "api_export.h"
  )
  add_library(oxygen::ts-init ALIAS ${ts_init_target})
  set_target_properties(
    ${ts_init_target}
    PROPERTIES
      DEFINE_SYMBOL
        "OXYGEN_COMP_EXPORTS"
      OUTPUT_NAME
        "Oxygen.TypeRegistry"
      VERSION
        ${META_VERSION}
      SOVERSION
        ${META_VERSION_MAJOR}
      PROJECT_LABEL
        "Oxygen.TypeRegistry"
  )
  target_include_directories(
    ${ts_init_target}
    PRIVATE
      "$<BUILD_INTERFACE:${OXYGEN_SOURCE_DIR}>"
  )
  target_compile_features(${ts_init_target} PUBLIC cxx_std_20)
  target_compile_options(
    ${ts_init_target}
    PUBLIC
      ${OXYGEN_COMMON_CXX_FLAGS}
  )
  target_link_libraries(${ts_init_target} PRIVATE oxygen::base oxygen::composition)
endif()

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
    ${ts_init_target}
)

asap_pop_module("${META_MODULE_NAME}")

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

# To achieve the truly global single instances of the `TypeRegistry`,
# `ComponentPoolRegistry` and any other global singletons, we need to ensure
# that in any build configuration, the single instances are always provided by
# the main executable or the same **SHARED LIBRARY** that is linked to the main
# executable.
#
# When everything is statically linked, and no DLLs are being dynamically
# loaded, the `cs_init.cpp` file, linked in the composition library, will
# provide a single place where the global instances are initialized.
#
# When the composition library is built as a shared library, the `cs_init.cpp`
# file, linked in the composition library, will provide a **possible** place
# from which the global instances can be initialized. The truly selected place
# will be the result of the walking through the previsouly loaded dynamic
# modules and finding the first one that provides the `Initialize*` entry
# points. In any case, ONLY ONE module will ever be selected to initialize the
# global instances.
#
# When the program or one of its modules is dynamically loading other modules,
# the above may still work if the composition library is linked dynamically, but
# will not work if it is linked statically. It is critical that the
# `Initialize*` entry points are provided by a **SHARED LIBRARY**. This is why
# we also provide a separate DLL target `oxygen-cs-init` that is built as a
# shared library and only provides the `Initialize*` entry points for the
# composition system. This library **MUST** be linked to the main executable.

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
    "Object.cpp"
    "ObjectMetaData.cpp"
    "TypeSystem.cpp"
    "cs_init.cpp"
    "Detail/FastIntMap.h"
    "Detail/GetTrulySingleInstance.h"
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
# Dedicated DLL for the composition system `Initialize*` entry points.
# ------------------------------------------------------------------------------
#
# **Recommended** to be always linked to the executable, and is **REQUIRED**
# when the composition library is built as static library, but the program may
# load libraries at runtime that link to the composition library. This shared
# library MUST always be the first found when looking for a loaded module that
# can provide the composition system `Initialize*` entry points.
#
# ```cmake
# target_link_libraries(
#   ${MY_MODULE_TARGET}
#   PRIVATE
#     $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::cs-init>
# )
# ```
#
# **IMPORTANT**:
#
# The linker will optimize the library out if it is not being used, so you must
# force link it by calling one of its initialization functions and storing the
# result in an unused variable.
#
# ```cpp
# namespace oxygen {
# class TypeRegistry;
# } // namespace oxygen
# extern "C" auto InitializeTypeRegistry() -> oxygen::TypeRegistry*;
# namespace {
# [[maybe_unused]] const auto* const ts_registry_unused
#   = InitializeTypeRegistry();
# } // namespace
# ```
# ------------------------------------------------------------------------------
if(NOT BUILD_SHARED_LIBS)
  set(cs_init_target oxygen-cs-init)
  add_library(
    ${cs_init_target}
    SHARED
    "cs_init.cpp"
    "api_export.h"
  )
  add_library(oxygen::cs-init ALIAS ${cs_init_target})
  set_target_properties(
    ${cs_init_target}
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
    ${cs_init_target}
    PRIVATE
      "$<BUILD_INTERFACE:${OXYGEN_SOURCE_DIR}>"
  )
  target_compile_features(${cs_init_target} PUBLIC cxx_std_20)
  target_compile_options(${cs_init_target} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
  target_link_libraries(
    ${cs_init_target}
    PRIVATE
      oxygen::base
      oxygen::composition
  )
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
    ${cs_init_target}
)

asap_pop_module("${META_MODULE_NAME}")

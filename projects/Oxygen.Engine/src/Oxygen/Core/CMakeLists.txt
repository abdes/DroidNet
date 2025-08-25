# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Core"
  DESCRIPTION
    "Oxygen engine core module, containing shared core types and classes."
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

# Generate version-header
configure_file("version.h.in" ${CMAKE_CURRENT_SOURCE_DIR}/version-info.h)

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET})
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    DEFINE_SYMBOL
      "OXGN_CORE_EXPORTS"
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
  OXYGEN_CORE_HEADERS
  "AnyCache.h"
  "api_export.h"
  "Detail/FormatUtils.h"
  "Resources.h"
  "SafeCall.h"
  "Types/BindlessHandle.h"
  "Types/Format.h"
  "Types/Frame.h"
  "Types/Scissors.h"
  "Types/ShaderType.h"
  "Types/TextureType.h"
  "Types/ViewPort.h"
  "version-info.h"
  "Version.h"
  # Generated headers (Bindless)
  "Bindless/Generated.Constants.h"
  "Bindless/Generated.Heaps.D3D12.h"
  "Bindless/Generated.Meta.h"
  "Bindless/Generated.RootSignature.h"
)
set(
  OXYGEN_CORE_PRIVATE_SOURCES
  "Detail/FormatUtils.cpp"
  "Detail/ToStringConverters.cpp"
  "Version.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_CORE_PRIVATE_SOURCES}
    ${OXYGEN_CORE_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_CORE_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_CORE_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::composition
)

# ------------------------------------------------------------------------------
# Bindless generation and Tools integration
# ------------------------------------------------------------------------------

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Bindless/CMakeLists.txt")
  add_subdirectory("Bindless")
  if(TARGET generate_bindless_headers)
    add_dependencies(${META_MODULE_TARGET} generate_bindless_headers)
  endif()
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Tools/CMakeLists.txt")
  add_subdirectory("Tools")
endif()

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Test")
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

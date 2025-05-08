# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Base"
  DESCRIPTION "Common utilities and classes for Oxygen modules"
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
      "OXYGEN_BASE_EXPORTS"
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
    "MemoryStream.cpp"
    "Detail/loguru.cpp"
    "Detail/signal.hpp"
    "Windows/ComError.cpp"
    "Windows/Exceptions.cpp"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES
      "api_export.h"
      "Compilers.h"
      "Endian.h"
      "FileStream.h"
      "Finally.h"
      "Logging.h"
      "Macros.h"
      "MemoryStream.h"
      "NoInline.h"
      "NoStd.h"
      "Platforms.h"
      "Reader.h"
      "Resource.h"
      "ResourceHandle.h"
      "ResourceTable.h"
      "Result.h"
      "ReturnAddress.h"
      "Signals.h"
      "Stream.h"
      "StringTypes.h"
      "StringUtils.h"
      "TimeUtils.h"
      "Unreachable.h"
      "VariantHelpers.h"
      "Writer.h"
      "Types/Geometry.h"
      "Types/Overload.h"
      "Types/Viewport.h"
      "Windows/ComError.h"
      "Windows/Exceptions.h"
)
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  target_sources(
    ${META_MODULE_TARGET}
    PRIVATE
      "Windows/ComError.cpp"
      "Windows/Exceptions.cpp"
    PUBLIC
      FILE_SET HEADERS
      FILES "Windows/ComError.h" "Windows/Exceptions.h"
  )
endif()

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_20)
target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXYGEN_BASE_STATIC>
)

target_compile_definitions(
  ${META_MODULE_TARGET}
  PUBLIC
    "LOGURU_USE_FMTLIB=1"
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
)

target_link_libraries(${META_MODULE_TARGET} PUBLIC fmt::fmt-header-only)

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

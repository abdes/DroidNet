# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Serio"
  DESCRIPTION
    "Provides support for serialization and deserialization of Oxygen objects."
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

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
      "OXGN_SERIO_EXPORTS"
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
  OXYGEN_SERIO_HEADERS
  "api_export.h"
  "AlignmentGuard.h"
  "AnyDevice.h"
  "AnyStream.h"
  "AsyncDevice.h"
  "AsyncFile.h"
  "AtomicFile.h"
  "ByteBuffer.h"
  "ByteView.h"
  "CopyRegion.h"
  "Device.h"
  "Error.h"
  "FileLock.h"
  "IoDriver.h"
  "MappedFile.h"
  "MemoryFile.h"
  "MemoryStream.h"
  "NativeFile.h"
  "Operation.h"
  "Reader.h"
  "Region.h"
  "RegionStream.h"
  "Stream.h"
  "Types.h"
  "Writer.h"
  "Detail/ErasedObject.h"
  "Detail/GuardedCopy.h"
)
set(
  OXYGEN_SERIO_PRIVATE_HEADERS
  "Detail/IoBackend.h"
  "Detail/SystemErrors.h"
)
set(
  OXYGEN_SERIO_PRIVATE_SOURCES
  "AtomicFile.cpp"
  "Error.cpp"
  "FileLock.cpp"
  "IoDriver.cpp"
  "MappedFile.cpp"
  "MemoryFile.cpp"
)

# Platform implementations: the directory is the only platform switch; public
# headers carry no platform conditionals.
if(WIN32)
  set(OXYGEN_SERIO_OTHER_PLATFORM "Platform/Posix/")
  list(
    APPEND
    OXYGEN_SERIO_PRIVATE_HEADERS
    "Platform/Windows/Handles.h"
    "Platform/Windows/IocpBackend.h"
  )
  list(
    APPEND
    OXYGEN_SERIO_PRIVATE_SOURCES
    "Platform/Windows/IocpBackend.cpp"
    "Platform/Windows/MappedFile.cpp"
    "Platform/Windows/NativeFile.cpp"
    "Platform/Windows/SystemErrors.cpp"
  )
else()
  set(OXYGEN_SERIO_OTHER_PLATFORM "Platform/Windows/")
  list(APPEND OXYGEN_SERIO_PRIVATE_HEADERS "Platform/Posix/UringBackend.h")
  list(
    APPEND
    OXYGEN_SERIO_PRIVATE_SOURCES
    "Platform/Posix/UringBackend.cpp"
    "Platform/Posix/MappedFile.cpp"
    "Platform/Posix/NativeFile.cpp"
    "Platform/Posix/SystemErrors.cpp"
  )
endif()

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_SERIO_PRIVATE_SOURCES}
    ${OXYGEN_SERIO_PRIVATE_HEADERS}
    ${OXYGEN_SERIO_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_SERIO_HEADERS}
)

# The other platform's sources and the test support headers belong to other
# targets: the other platform's build and Oxygen.Serio.Testing.
arrange_target_files_for_ide(
  ${META_MODULE_TARGET}
  EXCLUDE_PATTERNS
    "^${OXYGEN_SERIO_OTHER_PLATFORM}"
    "^Testing/"
)

target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
  PUBLIC
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_SERIO_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::oxco
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

# Test support (fault injection, the device conformance suite) for Serio's
# tests and for any module that defines its own device. Linked by tests only.
if(OXYGEN_BUILD_TESTS)
  add_library(Oxygen.Serio.Testing INTERFACE)
  add_library(oxygen::serio::testing ALIAS Oxygen.Serio.Testing)
  target_sources(
    Oxygen.Serio.Testing
    INTERFACE
      FILE_SET HEADERS
      BASE_DIRS "${OXYGEN_SOURCE_DIR}"
      FILES
        "Testing/AsyncDeviceConformance.h"
        "Testing/DeviceConformance.h"
        "Testing/FaultInjectingDevice.h"
        "Testing/StreamConformance.h"
        "Testing/TestBytes.h"
  )
  target_link_libraries(
    Oxygen.Serio.Testing
    INTERFACE
      ${META_MODULE_TARGET}
      oxygen::testing
  )
  set_target_properties(
    Oxygen.Serio.Testing
    PROPERTIES
      FOLDER
        "Testing"
  )
endif()

if(OXYGEN_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test")
  add_subdirectory("test")
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

# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.OxCo"
  DESCRIPTION "C++20 coroutines library"
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(asio CONFIG REQUIRED)

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET} INTERFACE)
add_library(${META_MODULE_TARGET_ALIAS} ALIAS ${META_MODULE_TARGET})

# Freeze the selected mode in the installed header. A developer may consume a
# Debug SDK from a differently named application configuration; their headers
# must still agree with the Oxygen binaries' awaiter layout and behavior.
set(
  _checker_enabled
  "$<OR:$<STREQUAL:${OXYGEN_AWAITER_STATE_CHECKER},ON>,$<AND:$<STREQUAL:${OXYGEN_AWAITER_STATE_CHECKER},AUTO>,$<CONFIG:Debug>>>"
)
set(_config_root "${CMAKE_CURRENT_BINARY_DIR}/config/$<CONFIG>")
set(_config_header "${_config_root}/Oxygen/OxCo/Config.h")
file(
  GENERATE
  OUTPUT
  "${_config_header}"
  CONTENT
    "// Generated Oxygen configuration; use the package's build options.\n#pragma once\n#if ${_checker_enabled}\n#ifndef OXCO_AWAITER_STATE_DEBUG\n#define OXCO_AWAITER_STATE_DEBUG 1\n#endif\n#else\n#ifdef OXCO_AWAITER_STATE_DEBUG\n#error Awaiter checker override conflicts with this Oxygen build\n#endif\n#endif\n"
)
target_sources(
  ${META_MODULE_TARGET}
  INTERFACE
    FILE_SET HEADERS
    BASE_DIRS "${_config_root}"
    FILES "${_config_header}"
)

set(
  OXYGEN_OXCO_HEADERS
  "asio.h"
  "Algorithms.h"
  "Awaitables.h"
  "BroadcastChannel.h"
  "Channel.h"
  "Co.h"
  "Coroutine.h"
  "Event.h"
  "EventLoop.h"
  "Executor.h"
  "LiveObject.h"
  "Nursery.h"
  "ParkingLot.h"
  "Run.h"
  "Shared.h"
  "SuspendForever.h"
  "RepeatableShared.h"
  "Semaphore.h"
  "TaskCancelledException.h"
  "ThreadPool.h"
  "Value.h"
  "Yield.h"
  "Detail/SanitizedAwaiter.h"
  "Detail/AwaiterStateChecker.h"
  "Detail/AwaitFn.h"
  "Detail/CallableSignature.h"
  "Detail/CancellableAdapter.h"
  "Detail/CoAwaitFactory.h"
  "Detail/CoRoutineFrame.h"
  "Detail/CoTag.h"
  "Detail/DisposableAdapter.h"
  "Detail/GetAwaiter.h"
  "Detail/IntrusiveList.h"
  "Detail/IntrusivePtr.h"
  "Detail/MuxBase.h"
  "Detail/MuxHelper.h"
  "Detail/MuxRange.h"
  "Detail/MuxTuple.h"
  "Detail/Optional.h"
  "Detail/ParkingLotImpl.h"
  "Detail/PointerBits.h"
  "Detail/Promise.h"
  "Detail/ProxyFrame.h"
  "Detail/Queue.h"
  "Detail/ReadyAwaiter.h"
  "Detail/Result.h"
  "Detail/RunOnCancel.h"
  "Detail/TaskAwaiter.h"
  "Detail/TaskFrame.h"
  "Detail/TaskParent.h"
  "Detail/Sequence.h"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_OXCO_HEADERS}
  INTERFACE
    FILE_SET HEADERS
    TYPE HEADERS
    BASE_DIRS ${OXYGEN_SOURCE_DIR}
    FILES ${OXYGEN_OXCO_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

# Set include directories
target_include_directories(
  ${META_MODULE_TARGET}
  INTERFACE
    # Includes will have the following format: #include <Oxygen/OxCo/Logging.h>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    OUTPUT_NAME
      "${META_MODULE_NAME}"
)

target_compile_features(${META_MODULE_TARGET} INTERFACE cxx_std_20)
target_compile_definitions(
  ${META_MODULE_TARGET}
  INTERFACE
    $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
)
target_link_libraries(
  ${META_MODULE_TARGET}
  INTERFACE
    oxygen::base
    asio::asio
)

# The native interface target lists its headers directly, so Visual Studio
# displays them without a separate utility project.
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    PROJECT_LABEL
      "${META_MODULE_NAME}"
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("Test")
  if(OXYGEN_BUILD_BENCHMARKS)
    add_subdirectory("Benchmarks")
  endif()
endif()

# ------------------------------------------------------------------------------
# API Documentation
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_DOCS AND OXYGEN_WITH_DOXYGEN)
  asap_with_doxygen(
    MODULE_NAME ${META_MODULE_TARGET}
    VERSION ${META_VERSION}
    TITLE "\"OxCo\""
    BRIEF "\"C++ coroutines for Oxygen base on `corral`\""
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

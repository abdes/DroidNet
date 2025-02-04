# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.OxCo"
  DESCRIPTION "C++20 coroutines library"
  WITHOUT_VERSION_H
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

set(
  header_files
  "asio.h"
  "Algorithms.h"
  "Awaitables.h"
  "Channel.h"
  "Co.h"
  "Coroutine.h"
  "Event.h"
  "EventLoop.h"
  "Executor.h"
  "Nursery.h"
  "ParkingLot.h"
  "Run.h"
  "Shared.h"
  "SuspendForever.h"
  "TaskCancelledException.h"
  "Yield.h"
  "Concepts/Awaitable.h"
  "Concepts/Awaiter.h"
  "Concepts/Cancellation.h"
  "Concepts/Promise.h"
  "Detail/AwaitableAdapter.h"
  "Detail/AwaitableStateChecker.h"
  "Detail/AwaitFn.h"
  "Detail/CallableSignature.h"
  "Detail/CancellableAdapter.h"
  "Detail/CoAwaitFactory.h"
  "Detail/CoRoutineFrame.h"
  "Detail/CoTag.h"
  "Detail/DisposableAdapter.h"
  "Detail/GetAwaitable.h"
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
  "Detail/ReadyAwaitable.h"
  "Detail/Result.h"
  "Detail/RunOnCancel.h"
  "Detail/ScopeGuard.h"
  "Detail/TaskAwaitable.h"
  "Detail/TaskFrame.h"
  "Detail/TaskParent.h"
  "Detail/Sequence.h"
  "Semaphore.h"
  "Value.h"
)

source_group(
  TREE ${CMAKE_CURRENT_SOURCE_DIR}
  FILES
    ${source_files}
    ${header_files}
)

target_sources(
  ${META_MODULE_TARGET}
  INTERFACE
    FILE_SET HEADERS
    TYPE HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
    FILES ${header_files}
)

# Set include directories
target_include_directories(
  ${META_MODULE_TARGET}
  INTERFACE
    # Includes will have the following format: #include "Oxygen/OxCo/Logging.h"
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
target_compile_options(
  ${META_MODULE_TARGET}
  INTERFACE
    ${OXYGEN_COMMON_CXX_FLAGS}
    $<$<CXX_COMPILER_ID:MSVC>:/wd4702> # Unreachable code
)
target_compile_definitions(
  ${META_MODULE_TARGET}
  INTERFACE
    $<$<PLATFORM_ID:Windows>:_WIN32_WINNT=0x0A00>
)
target_link_libraries(
  ${META_MODULE_TARGET}
  INTERFACE
    oxygen::base
    asio::asio
)

# Header only libraries don't get a Visual Studio project file generated for
# them. This is a workaround to make sure the header files are visible in the
# IDE.
add_custom_target(_${META_MODULE_NAME} SOURCES ${header_files})
set_target_properties(
  _${META_MODULE_NAME}
  PROPERTIES
    PROJECT_LABEL
      ${META_MODULE_NAME}
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

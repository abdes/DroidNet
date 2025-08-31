# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Graphics.Headless"
  DESCRIPTION "Headless graphics backend for testing and simulation"
  WITHOUT_VERSION_H
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
      "OXGN_HDLS_EXPORTS"
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
  OXYGEN_HEADLESS_HEADERS
  "api_export.h"
  "Buffer.h"
  "CommandQueue.h"
  "CommandRecorder.h"
  "CommandList.h"
  "Commands/BufferToTextureCommand.h"
  "Commands/ClearDepthStencilCommand.h"
  "Commands/ClearFramebufferCommand.h"
  "Commands/CopyBufferCommand.h"
  "Graphics.h"
  "Internal/EngineShaders.h"
  "Internal/QueueManager.h"
  "Surface.h"
  "Texture.h"
)
set(
  OXYGEN_HEADLESS_PRIVATE_SOURCES
  "Bindless/AllocationStrategy.cpp"
  "Bindless/AllocationStrategy.h"
  "Bindless/DescriptorAllocator.cpp"
  "Bindless/DescriptorAllocator.h"
  "Bindless/DescriptorHeapSegment.cpp"
  "Bindless/DescriptorHeapSegment.h"
  "Buffer.cpp"
  "CommandQueue.cpp"
  "CommandRecorder.cpp"
  "CommandList.cpp"
  "Commands/BufferToTextureCommand.cpp"
  "Commands/ClearDepthStencilCommand.cpp"
  "Commands/ClearFramebufferCommand.cpp"
  "Commands/CopyBufferCommand.cpp"
  "Graphics.cpp"
  "Internal/EngineShaders.cpp"
  "Internal/Commander.cpp"
  "Internal/HeadlessModule.cpp"
  "Internal/QueueManager.cpp"
  "Internal/SerialExecutor.cpp"
  "Internal/SerialExecutor.h"
  "Surface.cpp"
  "Texture.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_HEADLESS_PRIVATE_SOURCES}
    ${OXYGEN_HEADLESS_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_HEADLESS_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_HDLS_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    oxygen::base
    oxygen::core
    oxygen::platform
    oxygen::graphics-common
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

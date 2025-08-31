# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Graphics.Common"
  DESCRIPTION "Oxygen graphics layer abstractions"
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

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
      "OXYGEN_GFX_EXPORTS"
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
  OXYGEN_GFX_COMMON_HEADERS
  "api_export.h"
  "BackendModule.h"
  "Buffer.h"
  "CommandList.h"
  "CommandQueue.h"
  "CommandRecorder.h"
  "Concepts.h"
  "Constants.h"
  "DeferredObjectRelease.h"
  "DescriptorAllocator.h"
  "DescriptorHandle.h"
  "Detail/Barriers.h"
  "Detail/BaseDescriptorAllocator.h"
  "Detail/DescriptorHeapSegment.h"
  "Detail/FixedDescriptorHeapSegment.h"
  "Detail/PerFrameResourceManager.h"
  "Forward.h"
  "Framebuffer.h"
  "Graphics.h"
  "MemoryBlock.h"
  "NativeObject.h"
  "ObjectRelease.h"
  "PipelineState.h"
  "Queues.h"
  "RenderController.h"
  "ResourceRegistry.h"
  "Sampler.h"
  "ShaderByteCode.h"
  "ShaderCompiler.h"
  "ShaderManager.h"
  "Shaders.h"
  "Surface.h"
  "Texture.h"
  "Types/ClearFlags.h"
  "Types/Color.h"
  "Types/DescriptorVisibility.h"
  "Types/EngineResources.h"
  "Types/FenceValue.h"
  "Types/IndexRange.h"
  "Types/QueueRole.h"
  "Types/RenderTask.h"
  "Types/ResourceAccessMode.h"
  "Types/ResourceStates.h"
  "Types/ResourceViewType.h"
  "Types/TrackableResource.h"
)
set(
  OXYGEN_GFX_COMMON_PRIVATE_SOURCES
  "Buffer.cpp"
  "CommandList.cpp"
  "CommandQueue.cpp"
  "CommandRecorder.cpp"
  "DescriptorHandle.cpp"
  "Detail/Barriers.cpp"
  "Detail/FixedDescriptorHeapSegment.cpp"
  "Detail/PerFrameResourceManager.cpp"
  "Detail/RenderThread.cpp"
  "Detail/RenderThread.h"
  "Framebuffer.cpp"
  "Graphics.cpp"
  "Internal/Bindless.h"
  "Internal/FramebufferImpl.cpp"
  "Internal/QueueManager.cpp"
  "Internal/QueueManager.h"
  "Internal/FramebufferImpl.h"
  "Internal/ResourceStateTracker.cpp"
  "Internal/ResourceStateTracker.h"
  "Internal/ToStringConverters.cpp"
  "PipelineState.cpp"
  "RenderController.cpp"
  "ResourceRegistry.cpp"
  "ShaderCompiler.cpp"
  "ShaderManager.cpp"
  "Shaders.cpp"
  "Surface.cpp"
  "Texture.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_GFX_COMMON_PRIVATE_SOURCES}
    ${OXYGEN_GFX_COMMON_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_GFX_COMMON_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXYGEN_GFX_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    glm::glm
    oxygen::base
    oxygen::config
    oxygen::core
    oxygen::data
    oxygen::platform
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

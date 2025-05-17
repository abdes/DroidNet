# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause).
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

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    "Detail/Barriers.cpp"
    "Detail/DefaultViewCache.h"
    "Detail/EnumToStringConverters.cpp"
    "Detail/FixedDescriptorHeapSegment.cpp"
    "Detail/FormatUtils.cpp"
    "Detail/PerFrameResourceManager.cpp"
    "Detail/RenderThread.cpp"
    "Detail/RenderThread.h"
    "Detail/ResourceStateTracker.cpp"
    "Detail/TextureViewCache.h"
    "Detail/ViewCache.h"
    #"Bindless.cpp"
    #"BindlessResourceSet.cpp"
    "CommandList.cpp"
    "CommandQueue.cpp"
    "CommandRecorder.cpp"
    "DescriptorHandle.cpp"
    "Framebuffer.cpp"
    "Graphics.cpp"
    "Renderer.cpp"
    "Sampler.cpp"
    "SamplerFeedbackTexture.cpp"
    "ShaderCompiler.cpp"
    "ShaderManager.cpp"
    "Shaders.cpp"
    "Surface.cpp"
    "Texture.cpp"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES
      "api_export.h"
      "BackendModule.h"
      #"Bindless.h"
      #"BindlessResourceSet.h"
      "Buffer.h"
      "CommandList.h"
      "CommandQueue.h"
      "CommandRecorder.h"
      "Constants.h"
      "DeferredObjectRelease.h"
      "DescriptorAllocator.h"
      "DescriptorHandle.h"
      "Forward.h"
      "Framebuffer.h"
      "Graphics.h"
      "MemoryBlock.h"
      "NativeObject.h"
      "ObjectRelease.h"
      "Queues.h"
      "Renderer.h"
      "ResourceRegistry.h"
      "Sampler.h"
      "SamplerFeedbackTexture.h"
      "ShaderByteCode.h"
      "ShaderCompiler.h"
      "ShaderManager.h"
      "Shaders.h"
      "Surface.h"
      "SynchronizationCounter.h"
      "Texture.h"
      "Detail/Barriers.h"
      "Detail/BaseDescriptorAllocator.h"
      "Detail/DescriptorHeapSegment.h"
      "Detail/FixedDescriptorHeapSegment.h"
      "Detail/FormatUtils.h"
      "Detail/PerFrameResourceManager.h"
      "Detail/ResourceStateTracker.h"
      "Detail/StaticDescriptorHeapSegment.h"
      "Types/Color.h"
      "Types/EngineResources.h"
      "Types/Format.h"
      "Types/Queues.h"
      "Types/RenderTask.h"
      "Types/ResourceAccessMode.h"
      "Types/ResourceStates.h"
      "Types/ResourceViewType.h"
      "Types/Scissors.h"
      "Types/ShaderType.h"
      "Types/TrackableResource.h"
      "Types/ViewPort.h"
      "Types/DescriptorVisibility.h"
      "Types/IndexRange.h"
)

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

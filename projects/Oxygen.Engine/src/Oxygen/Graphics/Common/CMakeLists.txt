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
    "Detail/EnumToStringConverters.cpp"
    "Detail/RenderThread.h"
    "Detail/RenderThread.cpp"
    "CommandList.cpp"
    "CommandRecorder.cpp"
    "Shaders.cpp"
    "Graphics.cpp"
    "Renderer.cpp"
    "ShaderCompiler.cpp"
    "ShaderManager.cpp"
    "Surface.cpp"
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES
      "api_export.h"
      "BackendModule.h"
      "Buffer.h"
      "CommandList.h"
      "CommandQueue.h"
      "CommandRecorder.h"
      "Constants.h"
      "DeferredObjectRelease.h"
      "Forward.h"
      "Graphics.h"
      "MemoryBlock.h"
      "MixinDeferredRelease.h"
      "ObjectRelease.h"
      "PerFrameResourceManager.h"
      "Renderer.h"
      "RenderTarget.h"
      "ShaderByteCode.h"
      "ShaderCompiler.h"
      "ShaderManager.h"
      "Shaders.h"
      "Surface.h"
      "SynchronizationCounter.h"
      "Types/EngineResources.h"
      "Types/Format.h"
      "Types/Queues.h"
      "Types/RenderTask.h"
      "Types/ResourceAccessMode.h"
      "Types/ShaderType.h"
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

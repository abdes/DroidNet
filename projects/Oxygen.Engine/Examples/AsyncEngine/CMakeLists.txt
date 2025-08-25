# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about this module
# ------------------------------------------------------------------------------

set(
  description
  "Async engine simulator: structured frame phases, parallel task group, async pipelines."
)

asap_module_declare(
  MODULE_NAME "Oxygen.AsyncEngine.Simulator"
  DESCRIPTION description
  WITHOUT_VERSION_H
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(glm CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(
  ASYNC_ENGINE_CORE_SOURCES
  "AsyncEngineSimulator.cpp"
  "AsyncEngineSimulator.h"
  "EngineTypes.h"
  "GraphicsLayer.cpp"
  "GraphicsLayer.h"
  "IEngineModule.h"
  "FrameContext.h"
  "ResourceHandle.h"
  "FrameContext.cpp"
  "ModuleManager.h"
  # Render Graph System
  "Renderer/Graph/Types.cpp"
  "Renderer/Graph/Resource.cpp"
  "Renderer/Passes/RenderPass.h"
  "Renderer/Passes/RenderPass.cpp"
  "Renderer/Graph/ExecutionContext.cpp"
  "Renderer/Graph/RenderGraphBuilder.cpp"
  "Renderer/Graph/PerViewExpansionService.cpp"
  "Renderer/Graph/SharedReadOnlyPromotionStrategy.cpp"
  "Renderer/Graph/AliasLifetimeAnalysis.cpp"
  "Renderer/Graph/Scheduler.cpp"
  "Renderer/Graph/Cache.cpp"
  "Renderer/Graph/Validator.cpp"
  "Renderer/Graph/RenderGraph.cpp"
  # AsyncEngine Integration
  "Renderer/Integration/GraphicsLayerIntegration.h"
  "Renderer/Integration/GraphicsLayerIntegration.cpp"
  # Core Engine Modules
  "Modules/RenderGraphModule.h"
  "Modules/RenderGraphModule.cpp"
  "Modules/DebugOverlayModule.h"
  "Modules/DebugOverlayModule.cpp"
  "Modules/ConsoleModule.h"
  "Modules/ConsoleModule.cpp"
  "Modules/GeometryRenderModule.h"
  "Modules/GeometryRenderModule.cpp"
  # Game Example Modules
  "Examples/GameModule.h"
  "Examples/GameModule.cpp"
)

add_library(${META_MODULE_TARGET}.core STATIC ${ASYNC_ENGINE_CORE_SOURCES})

add_executable(
  ${META_MODULE_TARGET}
  "main_impl.cpp"
  "../main.cpp"
)

target_link_libraries(${META_MODULE_TARGET} PRIVATE ${META_MODULE_TARGET}.core)

set_target_properties(
  ${META_MODULE_TARGET}.core
  PROPERTIES
    FOLDER
      "Examples"
)
set_target_properties(
  ${META_MODULE_TARGET}
  PROPERTIES
    FOLDER
      "Examples"
    OUTPUT_NAME
      ${META_MODULE_NAME}
)

target_compile_options(
  ${META_MODULE_TARGET}.core
  PRIVATE
    ${OXYGEN_COMMON_CXX_FLAGS}
)
target_compile_features(${META_MODULE_TARGET}.core PRIVATE cxx_std_23)
target_compile_options(${META_MODULE_TARGET} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_features(${META_MODULE_TARGET} PRIVATE cxx_std_23)

target_include_directories(
  ${META_MODULE_TARGET}.core
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

target_link_libraries(
  ${META_MODULE_TARGET}.core
  PUBLIC
    glm::glm
    oxygen::base
    oxygen::renderer
    oxygen::composition
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:oxygen::cs-init>
    oxygen::oxco
    oxygen::clap
)
target_link_libraries(${META_MODULE_TARGET} PRIVATE ${META_MODULE_TARGET}.core)

# ------------------------------------------------------------------------------
# Install
# ------------------------------------------------------------------------------

oxygen_module_install(
  EXPORT ${META_MODULE_NAMESPACE}
  TARGETS
    ${META_MODULE_TARGET}
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

asap_pop_module("${META_MODULE_NAME}")

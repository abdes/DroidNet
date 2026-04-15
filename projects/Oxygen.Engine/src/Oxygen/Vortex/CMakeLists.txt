# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Meta information about the this module
# ------------------------------------------------------------------------------

asap_module_declare(
  MODULE_NAME "Oxygen.Vortex"
  DESCRIPTION "Oxygen Vortex rendering layer"
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
      "OXGN_VRTX_EXPORTS"
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
  OXYGEN_VORTEX_HEADERS
  "api_export.h"
  "CompositionView.h"
  "Errors.h"
  "FacadePresets.h"
  "PreparedSceneFrame.h"
  "RenderContext.h"
  "RendererCapability.h"
  "Renderer.h"
  "RendererTag.h"
  "RenderMode.h"
  "SceneCameraViewResolver.h"
  "SceneRenderer/SceneRenderBuilder.h"
  "SceneRenderer/SceneRenderer.h"
  "SceneRenderer/ShadingMode.h"
  "SceneRenderer/SceneTextures.h"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h"
  "SceneRenderer/Stages/InitViews/InitViewsModule.h"
  "Passes/ComputeRenderPass.h"
  "Passes/GraphicsRenderPass.h"
  "Passes/RenderPass.h"
  "Types/CompositingTask.h"
  "Types/ConventionalShadowDrawRecord.h"
  "Types/DrawMetadata.h"
  "Types/EnvironmentViewData.h"
  "Types/DebugFrameBindings.h"
  "Types/DrawFrameBindings.h"
  "Types/EnvironmentFrameBindings.h"
  "Types/LightCullingConfig.h"
  "Types/LightingFrameBindings.h"
  "Types/MaterialShadingConstants.h"
  "Types/PassMask.h"
  "Types/ProceduralGridMaterialConstants.h"
  "Types/ShadowFrameBindings.h"
  "Types/SyntheticSunData.h"
  "Types/ViewColorData.h"
  "Types/ViewConstants.h"
  "Types/ViewFrameBindings.h"
  "Types/VsmFrameBindings.h"
  "ScenePrep/GeometryRef.h"
  "ScenePrep/Handles.h"
  "ScenePrep/MaterialRef.h"
  "ScenePrep/CollectionConfig.h"
  "ScenePrep/Concepts.h"
  "ScenePrep/FinalizationConfig.h"
  "ScenePrep/RenderItemData.h"
  "ScenePrep/RenderItemProto.h"
  "ScenePrep/ScenePrepContext.h"
  "ScenePrep/ScenePrepState.h"
  "ScenePrep/Types.h"
  "Resources/DrawMetadataEmitter.h"
  "Resources/GeometryUploader.h"
  "Resources/IResourceBinder.h"
  "Resources/MaterialBinder.h"
  "Resources/TextureBinder.h"
  "Resources/TransformUploader.h"
  "Upload/AtlasBuffer.h"
  "Upload/Errors.h"
  "Upload/InlineTransfersCoordinator.h"
  "Upload/RingBufferStaging.h"
  "Upload/StagingProvider.h"
  "Upload/TransientStructuredBuffer.h"
  "Upload/UploadCoordinator.h"
  "Upload/UploadHelpers.h"
  "Upload/UploadPlanner.h"
  "Upload/UploadPolicy.h"
  "Upload/UploadTracker.h"
  "Upload/Types.h"
  "Upload/UploaderTag.h"
)
set(
  OXYGEN_VORTEX_PRIVATE_SOURCES
  "ModuleAnchor.cpp"
  "Internal/BlueNoiseData.h"
  "Internal/CompositingAlphaSanitizer.h"
  "Internal/GpuTimelineProfiler.cpp"
  "Internal/GpuTimelineProfiler.h"
  "Internal/CompositingPass.cpp"
  "Internal/CompositingPass.h"
  "Internal/CompositionPlanner.cpp"
  "Internal/CompositionPlanner.h"
  "Internal/CompositionViewImpl.cpp"
  "Internal/CompositionViewImpl.h"
  "Internal/FrameViewPacket.cpp"
  "Internal/FrameViewPacket.h"
  "Internal/PerViewStructuredPublisher.h"
  "Internal/RenderContextMaterializer.h"
  "Internal/RenderContextPool.h"
  "Internal/RenderScope.h"
  "Internal/ViewLifecycleService.cpp"
  "Internal/ViewLifecycleService.h"
  "Internal/ViewConstantsManager.cpp"
  "Internal/ViewConstantsManager.h"
  "PreparedSceneFrame.cpp"
  "Passes/ComputeRenderPass.cpp"
  "Passes/GraphicsRenderPass.cpp"
  "Passes/RenderPass.cpp"
  "Renderer.cpp"
  "Resources/DrawMetadataEmitter.cpp"
  "Resources/GeometryUploader.cpp"
  "Resources/MaterialBinder.cpp"
  "Resources/TextureBinder.cpp"
  "Resources/TransformUploader.cpp"
  "SceneCameraViewResolver.cpp"
  "SceneRenderer/PostRenderCleanup.cpp"
  "SceneRenderer/ResolveSceneColor.cpp"
  "SceneRenderer/SceneRenderBuilder.cpp"
  "SceneRenderer/SceneRenderer.cpp"
  "SceneRenderer/SceneTextures.cpp"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp"
  "SceneRenderer/Stages/InitViews/InitViewsModule.cpp"
  "ScenePrep/Extractors.h"
  "ScenePrep/Finalizers.h"
  "ScenePrep/ScenePrepPipeline.cpp"
  "ScenePrep/ScenePrepPipeline.h"
  "SceneRenderer/DepthPrePassPolicy.h"
  "SceneRenderer/Internal/FramePlanBuilder.cpp"
  "SceneRenderer/Internal/FramePlanBuilder.h"
  "SceneRenderer/Internal/ShaderDebugMode.h"
  "SceneRenderer/Internal/ShaderPassConfig.h"
  "SceneRenderer/Internal/ToneMapPassConfig.h"
  "SceneRenderer/Internal/ViewRenderPlan.h"
  "Types/ViewConstants.cpp"
  "Upload/AtlasBuffer.cpp"
  "Upload/Errors.cpp"
  "Upload/InlineTransfersCoordinator.cpp"
  "Upload/RingBufferStaging.cpp"
  "Upload/StagingProvider.cpp"
  "Upload/TransientStructuredBuffer.cpp"
  "Upload/UploadCoordinator.cpp"
  "Upload/UploadHelpers.cpp"
  "Upload/UploadPlanner.cpp"
  "Upload/UploadPolicy.cpp"
  "Upload/UploadTracker.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_VORTEX_PRIVATE_SOURCES}
    ${OXYGEN_VORTEX_HEADERS}
  PUBLIC
    FILE_SET HEADERS
      BASE_DIRS
        "${OXYGEN_SOURCE_DIR}"
      FILES
        ${OXYGEN_VORTEX_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_23)
target_compile_options(${META_MODULE_TARGET} PUBLIC ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_SHARED_LIBS}>>:OXGN_VRTX_STATIC>
)

target_link_libraries(
  ${META_MODULE_TARGET}
  PUBLIC
    glm::glm
    oxygen::base
    oxygen::config
    oxygen::console
    oxygen::oxco
    oxygen::data
    oxygen::scene
    oxygen::engine
    oxygen::graphics-common
    oxygen::nexus
  PRIVATE
    oxygen::profiling
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

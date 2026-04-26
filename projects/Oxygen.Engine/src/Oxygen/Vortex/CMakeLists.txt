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
  "ShaderDebugMode.h"
  "Diagnostics/DiagnosticsCaptureManifest.h"
  "Diagnostics/DiagnosticsFrameLedger.h"
  "Diagnostics/DiagnosticsService.h"
  "Diagnostics/ShaderDebugModeRegistry.h"
  "Diagnostics/DiagnosticsTypes.h"
  "SceneCameraViewResolver.h"
  "SceneRenderer/SceneRenderBuilder.h"
  "SceneRenderer/SceneRenderer.h"
  "SceneRenderer/ShadingMode.h"
  "SceneRenderer/SceneTextures.h"
  "SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h"
  "SceneRenderer/Stages/BasePass/BasePassModule.h"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h"
  "SceneRenderer/Stages/Hzb/ScreenHzbModule.h"
  "SceneRenderer/Stages/Occlusion/OcclusionConfig.h"
  "SceneRenderer/Stages/Occlusion/OcclusionModule.h"
  "SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h"
  "SceneRenderer/Stages/Translucency/TranslucencyMeshProcessor.h"
  "SceneRenderer/Stages/Translucency/TranslucencyModule.h"
  "SceneRenderer/Stages/InitViews/InitViewsModule.h"
  "Passes/ComputeRenderPass.h"
  "Passes/GraphicsRenderPass.h"
  "Passes/ImGuiOverlayPass.h"
  "Passes/RenderPass.h"
  "Environment/EnvironmentLightingService.h"
  "Environment/Internal/SkyRenderer.h"
  "Environment/Internal/AtmosphereRenderer.h"
  "Environment/Internal/AtmosphereState.h"
  "Environment/Internal/AtmosphereLightState.h"
  "Environment/Internal/AtmosphereLutCache.h"
  "Environment/Internal/FogRenderer.h"
  "Environment/Internal/IblProcessor.h"
  "Environment/Internal/LocalFogVolumeState.h"
  "Environment/Passes/SkyPass.h"
  "Environment/Passes/AtmosphereComposePass.h"
  "Environment/Passes/AtmosphereTransmittanceLutPass.h"
  "Environment/Passes/AtmosphereMultiScatteringLutPass.h"
  "Environment/Passes/DistantSkyLightLutPass.h"
  "Environment/Passes/AtmosphereSkyViewLutPass.h"
  "Environment/Passes/AtmosphereCameraAerialPerspectivePass.h"
  "Environment/Passes/VolumetricFogPass.h"
  "Environment/Passes/FogPass.h"
  "Environment/Passes/IblProbePass.h"
  "Environment/Passes/LocalFogVolumeTiledCullingPass.h"
  "Environment/Passes/LocalFogVolumeComposePass.h"
  "Environment/Types/EnvironmentProbeState.h"
  "Environment/Types/EnvironmentProbeBindings.h"
  "Environment/Types/EnvironmentEvaluationParameters.h"
  "Environment/Types/EnvironmentAmbientBridgeBindings.h"
  "Environment/Types/AtmosphereModel.h"
  "Environment/Types/AtmosphereLightModel.h"
  "Environment/Types/HeightFogModel.h"
  "Environment/Types/SkyLightEnvironmentModel.h"
  "Environment/Types/VolumetricFogModel.h"
  "Environment/Types/EnvironmentViewProducts.h"
  "Lighting/LightingService.h"
  "Shadows/ShadowService.h"
  "PostProcess/PostProcessService.h"
  "PostProcess/Internal/BloomChain.h"
  "PostProcess/Internal/ExposureCalculator.h"
  "PostProcess/Passes/BloomPass.h"
  "PostProcess/Passes/ExposurePass.h"
  "PostProcess/Passes/TonemapPass.h"
  "PostProcess/Types/PostProcessConfig.h"
  "PostProcess/Types/PostProcessFrameBindings.h"
  "Lighting/Internal/DeferredLightPacketBuilder.h"
  "Lighting/Internal/ForwardLightPublisher.h"
  "Lighting/Internal/LightGridBuilder.h"
  "Shadows/Internal/CascadeShadowSetup.h"
  "Shadows/Internal/ConventionalShadowTargetAllocator.h"
  "Shadows/Internal/ShadowCasterCulling.h"
  "Shadows/Internal/SpotShadowSetup.h"
  "Lighting/Passes/DeferredLightPass.h"
  "Shadows/Passes/CascadeShadowPass.h"
  "Shadows/Passes/ShadowDepthPass.h"
  "Lighting/Types/DirectionalLightForwardData.h"
  "Lighting/Types/ForwardLocalLightRecord.h"
  "Lighting/Types/FrameLightingInputs.h"
  "Lighting/Types/LightGridMetadata.h"
  "Shadows/Types/DirectionalShadowFrameData.h"
  "Shadows/Types/FrameShadowInputs.h"
  "Shadows/Types/ShadowCascadeBinding.h"
  "Shadows/Types/SpotShadowBinding.h"
  "Types/CompositingTask.h"
  "Types/ConventionalShadowDrawRecord.h"
  "Types/AcceptedDrawView.h"
  "Types/DrawMetadata.h"
  "Types/EnvironmentStaticData.h"
  "Types/EnvironmentViewData.h"
  "Types/DebugFrameBindings.h"
  "Types/DrawFrameBindings.h"
  "Types/EnvironmentFrameBindings.h"
  "Types/FrameLightSelection.h"
  "Types/GroundGridConfig.h"
  "Types/LightCullingConfig.h"
  "Types/LightingFrameBindings.h"
  "Types/MaterialShadingConstants.h"
  "Types/PassMask.h"
  "Types/ProceduralGridMaterialConstants.h"
  "Types/ScreenHzbFrameBindings.h"
  "Types/ShadowFrameBindings.h"
  "Types/SyntheticSunData.h"
  "Types/VelocityPublications.h"
  "Types/ViewColorData.h"
  "Types/ViewConstants.h"
  "Types/ViewFrameBindings.h"
  "Types/ViewHistoryFrameBindings.h"
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
  "Internal/DeformationHistoryCache.cpp"
  "Internal/DeformationHistoryCache.h"
  "Internal/FrameViewPacket.cpp"
  "Internal/FrameViewPacket.h"
  "Internal/ImGuiRuntime.cpp"
  "Internal/ImGuiRuntime.h"
  "Internal/PerViewStructuredPublisher.h"
  "Internal/PreviousViewHistoryCache.cpp"
  "Internal/PreviousViewHistoryCache.h"
  "Internal/RigidTransformHistoryCache.cpp"
  "Internal/RigidTransformHistoryCache.h"
  "Internal/RenderContextMaterializer.h"
  "Internal/RenderContextPool.h"
  "Internal/RenderScope.h"
  "Internal/ViewLifecycleService.cpp"
  "Internal/ViewLifecycleService.h"
  "Internal/ViewConstantsManager.cpp"
  "Internal/ViewConstantsManager.h"
  "Diagnostics/DiagnosticsCaptureManifest.cpp"
  "Diagnostics/DiagnosticsFrameLedger.cpp"
  "Diagnostics/DiagnosticsService.cpp"
  "Diagnostics/ShaderDebugModeRegistry.cpp"
  "Diagnostics/DiagnosticsTypes.cpp"
  "PreparedSceneFrame.cpp"
  "Passes/ComputeRenderPass.cpp"
  "Passes/GraphicsRenderPass.cpp"
  "Passes/GroundGridPass.cpp"
  "Passes/ImGuiOverlayPass.cpp"
  "Passes/RenderPass.cpp"
  "Environment/EnvironmentLightingService.cpp"
  "Environment/Internal/SkyRenderer.cpp"
  "Environment/Internal/AtmosphereRenderer.cpp"
  "Environment/Internal/AtmosphereState.cpp"
  "Environment/Internal/AtmosphereLightState.cpp"
  "Environment/Internal/AtmosphereLightTranslation.h"
  "Environment/Internal/AtmosphereLutCache.cpp"
  "Environment/Internal/FogRenderer.cpp"
  "Environment/Internal/IblProcessor.cpp"
  "Environment/Internal/LocalFogVolumeState.cpp"
  "Environment/Passes/SkyPass.cpp"
  "Environment/Passes/AtmosphereComposePass.cpp"
  "Environment/Passes/AtmosphereTransmittanceLutPass.cpp"
  "Environment/Passes/AtmosphereMultiScatteringLutPass.cpp"
  "Environment/Passes/DistantSkyLightLutPass.cpp"
  "Environment/Passes/AtmosphereSkyViewLutPass.cpp"
  "Environment/Passes/AtmosphereCameraAerialPerspectivePass.cpp"
  "Environment/Passes/VolumetricFogPass.cpp"
  "Environment/Passes/FogPass.cpp"
  "Environment/Passes/IblProbePass.cpp"
  "Environment/Passes/LocalFogVolumeTiledCullingPass.cpp"
  "Environment/Passes/LocalFogVolumeComposePass.cpp"
  "Lighting/LightingService.cpp"
  "Shadows/ShadowService.cpp"
  "PostProcess/PostProcessService.cpp"
  "PostProcess/Internal/BloomChain.cpp"
  "PostProcess/Internal/ExposureCalculator.cpp"
  "PostProcess/Passes/BloomPass.cpp"
  "PostProcess/Passes/ExposurePass.cpp"
  "PostProcess/Passes/TonemapPass.cpp"
  "Lighting/Internal/DeferredLightPacketBuilder.cpp"
  "Lighting/Internal/ForwardLightPublisher.cpp"
  "Lighting/Internal/LightGridBuilder.cpp"
  "Shadows/Internal/CascadeShadowSetup.cpp"
  "Shadows/Internal/ConventionalShadowTargetAllocator.cpp"
  "Shadows/Internal/ShadowCasterCulling.cpp"
  "Shadows/Internal/SpotShadowSetup.cpp"
  "Lighting/Passes/DeferredLightPass.cpp"
  "Shadows/Passes/CascadeShadowPass.cpp"
  "Shadows/Passes/ShadowDepthPass.cpp"
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
  "SceneRenderer/Stages/BasePass/BasePassMeshProcessor.cpp"
  "SceneRenderer/Stages/BasePass/BasePassModule.cpp"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.cpp"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp"
  "SceneRenderer/Stages/Hzb/ScreenHzbModule.cpp"
  "SceneRenderer/Stages/Occlusion/OcclusionModule.cpp"
  "SceneRenderer/Stages/Occlusion/Types/OcclusionStats.cpp"
  "SceneRenderer/Stages/Translucency/TranslucencyMeshProcessor.cpp"
  "SceneRenderer/Stages/Translucency/TranslucencyModule.cpp"
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
  "Types/AcceptedDrawView.cpp"
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
    oxygen::scenesync
  PRIVATE
    oxygen::imgui
    oxygen::platform
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

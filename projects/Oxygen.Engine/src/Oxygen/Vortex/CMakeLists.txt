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
)
asap_push_module("${META_MODULE_NAME}")

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(ggx_data_dir "${CMAKE_CURRENT_SOURCE_DIR}/Lighting/Data")
set(ggx_embedded_header "${CMAKE_CURRENT_BINARY_DIR}/GeneratedGgxEnergyData.h")
add_custom_command(
  OUTPUT
    "${ggx_embedded_header}"
  COMMAND
    "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/tools/vortex/BuildGgxEnergyData.py" embed
    "${ggx_data_dir}/GgxEnergy.json" "${ggx_data_dir}/GgxEnergy.bin" --output
    "${ggx_embedded_header}"
  DEPENDS
    "${ggx_data_dir}/GgxEnergy.json"
    "${ggx_data_dir}/GgxEnergy.bin"
    "${PROJECT_SOURCE_DIR}/tools/vortex/BuildGgxEnergyData.py"
  VERBATIM
)

# Shared C++/HLSL contracts are generated from Meta/ShaderContracts.yaml into
# the source tree, where they are versioned like the bindless ABI headers. The
# stamp is the only declared output, so a clean never deletes them.
set(_contracts_spec "${CMAKE_CURRENT_SOURCE_DIR}/Meta/ShaderContracts.yaml")
set(
  _contracts_bindless
  "${PROJECT_SOURCE_DIR}/src/Oxygen/Core/Meta/Bindless.yaml"
)
set(
  _contracts_generator_dir
  "${PROJECT_SOURCE_DIR}/src/Oxygen/Core/Tools/BindlessCodeGen/src/bindless_codegen"
)
set(_contracts_stamp "${CMAKE_CURRENT_BINARY_DIR}/shader_contracts.stamp")
add_custom_command(
  OUTPUT
    "${_contracts_stamp}"
  COMMAND
    "${Python3_EXECUTABLE}" -m bindless_codegen.contracts --spec
    "${_contracts_spec}" --bindless "${_contracts_bindless}" --cpp-root
    "${PROJECT_SOURCE_DIR}/src" --hlsl-root
    "${PROJECT_SOURCE_DIR}/src/Oxygen/Graphics/Direct3D12/Shaders"
  COMMAND
    "${CMAKE_COMMAND}" -E touch "${_contracts_stamp}"
  DEPENDS
    "${_contracts_spec}"
    "${_contracts_bindless}"
    "${_contracts_generator_dir}/contracts.py"
    "${_contracts_generator_dir}/formatting.py"
    "${PROJECT_SOURCE_DIR}/.clang-format"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Generating Vortex shader contracts"
  VERBATIM
)
add_custom_target(oxygen-vortex_contracts_gen DEPENDS "${_contracts_stamp}")
add_dependencies(oxygen-vortex_contracts_gen oxygen-core_bindless_gen)
set_property(
  GLOBAL
  APPEND
  PROPERTY
    OXYGEN_SHADER_HEADER_GENERATORS
      oxygen-vortex_contracts_gen
)
if(TARGET oxygen-graphics-direct3d12_shaders)
  add_dependencies(
    oxygen-graphics-direct3d12_shaders
    oxygen-vortex_contracts_gen
  )
endif()

# ==============================================================================
# Build instructions
# ==============================================================================

# ------------------------------------------------------------------------------
# Main module target
# ------------------------------------------------------------------------------

add_library(${META_MODULE_TARGET})
add_dependencies(${META_MODULE_TARGET} oxygen-vortex_contracts_gen)
target_sources(${META_MODULE_TARGET} PRIVATE "${ggx_embedded_header}")
target_include_directories(
  ${META_MODULE_TARGET}
  PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
)
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
  "Internal/PerViewStructuredPublisher.h"
  "Internal/ChangedLogLine.h"
  "Internal/GpuFeedback.h"
  "Internal/RigidTransformHistoryCache.h"
  "Lighting/Types/LightGridResources.h"
  "ScenePrep/Extractors.h"
  "ScenePrep/Finalizers.h"
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
  "SurfaceLayerSource.h"
  "ViewExtension.h"
  "ViewFeatureProfile.h"
  "Diagnostics/DiagnosticsCaptureManifest.h"
  "Diagnostics/DiagnosticsFrameLedger.h"
  "Diagnostics/DiagnosticsService.h"
  "Diagnostics/GpuWorkSummary.h"
  "Diagnostics/ShaderDebugModeRegistry.h"
  "Diagnostics/ViewModeCatalog.h"
  "Diagnostics/DiagnosticsTypes.h"
  "Diagnostics/Passes/GpuDebugPrimitivePass.h"
  "SceneCameraViewResolver.h"
  "SceneRenderer/SceneRenderBuilder.h"
  "SceneRenderer/SceneRenderer.h"
  "SceneRenderer/SceneTextureLeasePool.h"
  "SceneRenderer/DepthPrePassPolicy.h"
  "SceneRenderer/ShadingMode.h"
  "SceneRenderer/Generated.SceneTextureBindings.h"
  "SceneRenderer/SceneTextures.h"
  "SceneRenderer/Stages/BasePass/BasePassMeshProcessor.h"
  "SceneRenderer/Stages/BasePass/BasePassModule.h"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.h"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.h"
  "SceneRenderer/Stages/Hzb/HzbPyramidBuilder.h"
  "SceneRenderer/Stages/Hzb/HzbPyramidTexture.h"
  "SceneRenderer/Stages/Hzb/ScreenHzbModule.h"
  "SceneRenderer/Stages/Occlusion/Internal/CullingViewHistory.h"
  "SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h"
  "SceneRenderer/Stages/Occlusion/Internal/Generated.OcclusionContracts.h"
  "SceneRenderer/Stages/Occlusion/Internal/HistorySlotAllocator.h"
  "SceneRenderer/Stages/Occlusion/Internal/IndirectListBuilder.h"
  "SceneRenderer/Stages/Occlusion/OcclusionConfig.h"
  "SceneRenderer/Stages/Occlusion/OcclusionModule.h"
  "SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h"
  "SceneRenderer/Stages/Occlusion/Types/OcclusionStats.h"
  "SceneRenderer/Stages/Translucency/TranslucencyMeshProcessor.h"
  "SceneRenderer/Stages/Translucency/TranslucencyModule.h"
  "SceneRenderer/Stages/InitViews/InitViewsModule.h"
  "Passes/GraphicsRenderPass.h"
  "Passes/GroundGridPass.h"
  "Passes/SelectionOutlinePass.h"
  "Passes/ViewPickPass.h"
  "Passes/ViewOverlayPass.h"
  "Passes/RenderPass.h"
  "Environment/EnvironmentLightingService.h"
  "Environment/SceneBackground.h"
  "Environment/Internal/SkyRenderer.h"
  "Environment/Internal/AtmosphereRenderer.h"
  "Environment/Internal/AtmosphereState.h"
  "Environment/Internal/AtmosphereView.h"
  "Environment/Internal/CapturedSkySource.h"
  "Environment/Internal/AtmosphereLightState.h"
  "Environment/Internal/AtmosphereLutCache.h"
  "Environment/Internal/FogRenderer.h"
  "Environment/Internal/IblProcessor.h"
  "IndirectLighting/IndirectLightingService.h"
  "Environment/Internal/LocalFogVolumeState.h"
  "Environment/Internal/StaticSkyLightProcessor.h"
  "Environment/Internal/IblBrdfLookup.h"
  "Environment/Internal/IblGpuProcessor.h"
  "Environment/Internal/IblWorkBudget.h"
  "Environment/Types/IblProductMetadata.h"
  "Environment/Types/IblCaptureLease.h"
  "Environment/Passes/SkyPass.h"
  "Environment/Passes/AtmosphereComposePass.h"
  "Environment/Passes/Generated.AtmosphereContracts.h"
  "Environment/Passes/Generated.FogContracts.h"
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
  "Environment/Types/StaticSkyLightProducts.h"
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
  "Lighting/Internal/DeferredLightProxyGeometry.h"
  "Lighting/Internal/ForwardLightPublisher.h"
  "Lighting/Internal/LightGridBuilder.h"
  "Lighting/Internal/LightEvaluationRecords.h"
  "Lighting/Types/LightingPreparationFailure.h"
  "Lighting/Types/DeferredLightConstants.h"
  "Lighting/Types/DeferredShadowInputs.h"
  "Lighting/Internal/BrdfEnergyData.h"
  "Lighting/Internal/BrdfEnergyResources.h"
  "Shadows/Internal/CascadeShadowSetup.h"
  "Shadows/Internal/ConventionalShadowTargetAllocator.h"
  "Shadows/Internal/PointShadowSetup.h"
  "Shadows/Internal/LocalShadowProjection.h"
  "Shadows/Internal/ShadowEligibility.h"
  "Shadows/Internal/ShadowCasterDependencies.h"
  "Shadows/Internal/LocalShadowRequest.h"
  "Shadows/Internal/SharedShadowMap.h"
  "Shadows/Types/ShadowContentLease.h"
  "Shadows/Types/ShadowSharingDiagnostics.h"
  "Shadows/Types/ShadowUseError.h"
  "Types/ShadowCasterSource.h"
  "Shadows/Internal/LocalShadowQuality.h"
  "Shadows/Internal/SpotShadowSetup.h"
  "Lighting/Passes/DeferredLightPass.h"
  "Shadows/Passes/CascadeShadowPass.h"
  "Shadows/Passes/ShadowDepthPass.h"
  "Shadows/Passes/ShadowReceiverFitPass.h"
  "Shadows/Passes/Generated.ShadowPassContracts.h"
  "Lighting/Types/DirectionalLightForwardData.h"
  "Lighting/Types/ClusterLightRange.h"
  "Lighting/Types/ForwardLocalLightRecord.h"
  "Lighting/Types/FrameLightingInputs.h"
  "Lighting/Types/LightGridMetadata.h"
  "Lighting/Types/LightGridBuildStatus.h"
  "Lighting/Types/LightGridPassConstants.h"
  "Types/LightingIndices.h"
  "Shadows/Types/ShadowFrameData.h"
  "Shadows/Types/ShadowReceiverView.h"
  "Shadows/Types/FrameShadowInputs.h"
  "Shadows/Types/Generated.ShadowContracts.h"
  "Types/CompositingTask.h"
  "Types/AcceptedDrawView.h"
  "Types/DrawCullRecord.h"
  "Types/DrawMetadata.h"
  "Types/EnvironmentStaticData.h"
  "Types/EnvironmentViewData.h"
  "Types/DebugFrameBindings.h"
  "Types/BindlessSlots.h"
  "Types/Generated.DrawContracts.h"
  "Types/EnvironmentFrameBindings.h"
  "Types/FrameLightSelection.h"
  "Types/GroundGridConfig.h"
  "Types/ViewOutline.h"
  "Types/ViewPick.h"
  "Types/ViewOverlay.h"
  "Types/LightCullingConfig.h"
  "Types/LightingFrameBindings.h"
  "Types/MaterialShadingConstants.h"
  "Types/PassMask.h"
  "Types/ProceduralGridMaterialConstants.h"
  "Types/ScreenHzbFrameBindings.h"
  "Types/SyntheticSunData.h"
  "Types/VelocityPublications.h"
  "Types/ExposureTransition.h"
  "Types/ExposureSettingsStatus.h"
  "Types/EnvironmentLightingState.h"
  "Types/SkyLightRuntimeState.h"
  "Types/ResidentContentRevision.h"
  "Types/ExposureTargetData.h"
  "Types/ViewConstants.h"
  "Types/ViewRenderStatus.h"
  "Types/Generated.ExposureContracts.h"
  "Types/Generated.ViewConstants.h"
  "Types/Generated.ViewContracts.h"
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
  "Resources/CubeIlluminance.h"
  "Resources/DrawMetadataEmitter.h"
  "Resources/GeometryUploader.h"
  "Resources/IResourceBinder.h"
  "Resources/MaterialBinder.h"
  "Resources/TextureBinder.h"
  "Resources/TransformUploader.h"
  "Upload/AtlasBuffer.h"
  "Upload/Errors.h"
  "Upload/ImmutableTextureUpload.h"
  "Upload/InlineTransfersCoordinator.h"
  "Upload/RingBufferStaging.h"
  "Upload/StagingProvider.h"
  "Upload/TransientConstantBuffer.h"
  "Upload/TransientStructuredBuffer.h"
  "Upload/UploadCoordinator.h"
  "Upload/UploadHelpers.h"
  "Upload/UploadPlanner.h"
  "Upload/UploadPolicy.h"
  "Upload/UploadTicket.h"
  "Upload/UploadTracker.h"
  "Upload/Types.h"
  "Upload/UploaderTag.h"
)
set(
  OXYGEN_VORTEX_PRIVATE_SOURCES
  "ModuleAnchor.cpp"
  "ViewExtension.cpp"
  "Internal/BlueNoiseData.h"
  "Internal/CompositingAlphaSanitizer.h"
  "Internal/GpuTimelineAccess.h"
  "Internal/GpuTimelineProfiler.cpp"
  "Internal/GpuTimelineProfiler.h"
  "Internal/CompositingPass.cpp"
  "Internal/CompositingPass.h"
  "Internal/CompositionViewImpl.cpp"
  "Internal/CompositionViewImpl.h"
  "Internal/DeformationHistoryCache.cpp"
  "Internal/DeformationHistoryCache.h"
  "Internal/MeshRasterState.h"
  "Internal/SurfaceLayers.cpp"
  "Internal/SurfaceLayers.h"
  "Internal/BindlessRootBindings.h"
  "Internal/ViewConstantsGuard.h"
  "Internal/TextureViews.h"
  "Lighting/Internal/SpatialLightGrid.h"
  "Lighting/Internal/SpatialLightGrid.cpp"
  "Internal/PerViewScope.h"
  "Internal/PreviousViewHistoryCache.cpp"
  "Internal/PreviousViewHistoryCache.h"
  "Internal/RigidTransformHistoryCache.cpp"
  "Internal/RenderContextMaterializer.h"
  "Internal/RenderContextPool.h"
  "Internal/RenderScope.h"
  "Internal/MeshDepthPipeline.h"
  "Internal/MeshDepthPipeline.cpp"
  "Internal/GpuFeedback.cpp"
  "Internal/RetainedTexturePool.cpp"
  "Internal/RetainedTexturePool.h"
  "Internal/StructuredGpuBuffer.cpp"
  "Internal/StructuredGpuBuffer.h"
  "Internal/ViewportClamp.h"
  "Internal/ViewLifecycleService.cpp"
  "Internal/ViewLifecycleService.h"
  "Diagnostics/DiagnosticsCaptureManifest.cpp"
  "Diagnostics/DiagnosticsFrameLedger.cpp"
  "Diagnostics/DiagnosticsService.cpp"
  "Diagnostics/GpuWorkSummary.cpp"
  "Diagnostics/Internal/GpuWorkStageTable.cpp"
  "Diagnostics/Internal/GpuWorkStageTable.h"
  "Diagnostics/Internal/GpuWorkSummarizer.cpp"
  "Diagnostics/Internal/GpuWorkSummarizer.h"
  "Diagnostics/ShaderDebugModeRegistry.cpp"
  "Diagnostics/ViewModeCatalog.cpp"
  "Diagnostics/DiagnosticsTypes.cpp"
  "Diagnostics/Passes/GpuDebugPrimitivePass.cpp"
  "PreparedSceneFrame.cpp"
  "Passes/GraphicsRenderPass.cpp"
  "Passes/GroundGridPass.cpp"
  "Passes/SelectionOutlinePass.cpp"
  "Passes/ViewPickPass.cpp"
  "Passes/ViewOverlayPass.cpp"
  "Passes/RenderPass.cpp"
  "Environment/EnvironmentLightingService.cpp"
  "Environment/Internal/SkyRenderer.cpp"
  "Environment/Internal/AtmosphereRenderer.cpp"
  "Environment/Internal/AtmosphereState.cpp"
  "Environment/Internal/AtmosphereView.cpp"
  "Environment/Internal/CapturedSkySource.cpp"
  "Environment/Internal/AtmosphereLightState.cpp"
  "Environment/Internal/AtmosphereGpuPacking.h"
  "Environment/Internal/AtmosphereLightTranslation.h"
  "Environment/Internal/AtmosphereLutCache.cpp"
  "Environment/Internal/FogRenderer.cpp"
  "Environment/Internal/IblProcessor.cpp"
  "IndirectLighting/IndirectLightingService.cpp"
  "Environment/Internal/LocalFogVolumeState.cpp"
  "Environment/Internal/StaticSkyLightProcessor.cpp"
  "Environment/Internal/IblBrdfLookup.cpp"
  "Environment/Internal/IblBrdfResources.cpp"
  "Environment/Internal/IblGpuProcessor.cpp"
  "Environment/Internal/IblGpuValidation.h"
  "Environment/Internal/IblGpuValidation.cpp"
  "Environment/Internal/IblWorkBudget.cpp"
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
  "Lighting/Internal/DeferredLightProxyGeometry.cpp"
  "Lighting/Internal/ForwardLightPublisher.cpp"
  "Lighting/Internal/LightGridBuilder.cpp"
  "Lighting/Internal/LightEvaluationRecords.cpp"
  "Lighting/Internal/BrdfEnergyData.cpp"
  "Lighting/Internal/BrdfEnergyResources.cpp"
  "Shadows/Internal/CascadeShadowSetup.cpp"
  "Shadows/Internal/ShadowReferenceBuilder.cpp"
  "Shadows/Internal/ShadowReferenceBuilder.h"
  "Shadows/Internal/ConventionalShadowTargetAllocator.cpp"
  "Shadows/Internal/PointShadowSetup.cpp"
  "Shadows/Internal/ShadowCasterDependencies.cpp"
  "Shadows/Internal/LocalShadowRequest.cpp"
  "Shadows/Internal/SharedShadowMap.cpp"
  "Shadows/Types/ShadowContentLease.cpp"
  "Shadows/Internal/SpotShadowSetup.cpp"
  "Lighting/Passes/DeferredLightPass.cpp"
  "Shadows/Passes/CascadeShadowPass.cpp"
  "Shadows/Passes/ShadowDepthPass.cpp"
  "Shadows/Passes/ShadowReceiverFitPass.cpp"
  "Shadows/Passes/ContactShadowCasterDepthPass.h"
  "Shadows/Passes/ContactShadowCasterDepthPass.cpp"
  "Renderer.cpp"
  "Resources/CubeIlluminance.cpp"
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
  "SceneRenderer/SceneTextureLeasePool.cpp"
  "SceneRenderer/SceneTextures.cpp"
  "SceneRenderer/Stages/BasePass/BasePassMeshProcessor.cpp"
  "SceneRenderer/Stages/BasePass/BasePassModule.cpp"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassMeshProcessor.cpp"
  "SceneRenderer/Stages/DepthPrepass/DepthPrepassModule.cpp"
  "SceneRenderer/Stages/Hzb/HzbPyramidBuilder.cpp"
  "SceneRenderer/Stages/Hzb/HzbPyramidTexture.cpp"
  "SceneRenderer/Stages/Hzb/ScreenHzbModule.cpp"
  "SceneRenderer/Stages/Occlusion/Internal/CullingViewHistory.cpp"
  "SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.cpp"
  "SceneRenderer/Stages/Occlusion/Internal/HistorySlotAllocator.cpp"
  "SceneRenderer/Stages/Occlusion/Internal/IndirectListBuilder.cpp"
  "SceneRenderer/Stages/Occlusion/OcclusionModule.cpp"
  "SceneRenderer/Stages/Translucency/TranslucencyMeshProcessor.cpp"
  "SceneRenderer/Stages/Translucency/TranslucencyModule.cpp"
  "SceneRenderer/Stages/InitViews/InitViewsModule.cpp"
  "ScenePrep/ScenePrepPipeline.cpp"
  "ScenePrep/ScenePrepPipeline.h"
  "Types/ViewConstants.cpp"
  "Types/AcceptedDrawView.cpp"
  "Upload/AtlasBuffer.cpp"
  "Upload/Errors.cpp"
  "Upload/ImmutableTextureUpload.cpp"
  "Upload/InlineTransfersCoordinator.cpp"
  "Upload/RingBufferStaging.cpp"
  "Upload/StagingProvider.cpp"
  "Upload/TransientConstantBuffer.cpp"
  "Upload/TransientStructuredBuffer.cpp"
  "Upload/TextureUploadPacking.cpp"
  "Upload/UploadCoordinator.cpp"
  "Upload/UploadHelpers.cpp"
  "Upload/UploadPlanner.cpp"
  "Upload/UploadPolicy.cpp"
  "Upload/Internal/UploadTimeline.h"
  "Upload/UploadTicket.cpp"
  "Upload/UploadTracker.cpp"
)

target_sources(
  ${META_MODULE_TARGET}
  PRIVATE
    ${OXYGEN_VORTEX_PRIVATE_SOURCES}
    ${OXYGEN_VORTEX_HEADERS}
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS "${OXYGEN_SOURCE_DIR}"
    FILES ${OXYGEN_VORTEX_HEADERS}
)

arrange_target_files_for_ide(${META_MODULE_TARGET})

target_compile_features(${META_MODULE_TARGET} PUBLIC cxx_std_23)
target_compile_definitions(
  ${META_MODULE_TARGET}
  PRIVATE
    $<$<PLATFORM_ID:Windows>:WIN32_LEAN_AND_MEAN>
    $<$<PLATFORM_ID:Windows>:NOMINMAX>
  PUBLIC
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
    oxygen::profiling
)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------

if(OXYGEN_BUILD_TESTS)
  add_subdirectory("test")
  # GPU benchmark workloads share the GoogleTest exposure fixtures. Keep that
  # existing opt-in contract; the other modules' CPU benchmarks are independent.
  if(OXYGEN_BUILD_BENCHMARKS)
    add_subdirectory("Benchmarks")
  endif()
elseif(OXYGEN_BUILD_BENCHMARKS)
  message(
    STATUS
    "Vortex GPU benchmarks require OXYGEN_BUILD_TESTS; standalone CPU benchmarks remain enabled."
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

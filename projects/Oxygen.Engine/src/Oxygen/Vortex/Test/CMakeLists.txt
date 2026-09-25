# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)

# ==============================================================================
# Build instructions
# ==============================================================================

include(GTestHelpers)

# -- Library Link Test ---------------------------------------------------------

set(link_test_target "Oxygen.Vortex.LinkTest")
add_executable(
  ${link_test_target}
  "Fakes/Graphics.h"
  "Link_test.cpp"
)
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_definitions(
  ${link_test_target}
  PRIVATE
    OXYGEN_VORTEX_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/Oxygen/Vortex"
)
target_link_libraries(${link_test_target} PRIVATE oxygen::vortex)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

m_gtest_program(RendererCapability SOURCES "RendererCapability_test.cpp")

m_gtest_program(GpuTimelineProfiler SOURCES "GpuTimelineProfiler_test.cpp")
target_link_libraries(
  Oxygen.Vortex.GpuTimelineProfiler.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
)

m_gtest_program(
  DiagnosticsService
  SOURCES
    "Fakes/Graphics.h"
    "DiagnosticsService_test.cpp"
)

m_gtest_program(
  DiagnosticsFrameLedger
  SOURCES
    "DiagnosticsFrameLedger_test.cpp"
)

m_gtest_program(
  DiagnosticsCaptureManifest
  SOURCES
    "DiagnosticsCaptureManifest_test.cpp"
)
target_link_libraries(
  Oxygen.Vortex.DiagnosticsCaptureManifest.Tests
  PRIVATE
    nlohmann_json::nlohmann_json
)

m_gtest_program(
  ShaderDebugModeRegistry
  SOURCES
    "ShaderDebugModeRegistry_test.cpp"
)

m_gtest_program(RenderContext SOURCES "RenderContext_test.cpp")

m_gtest_program(AcceptedDrawView SOURCES "Types/AcceptedDrawView_test.cpp")

m_gtest_program(SceneRendererShell SOURCES "SceneRendererShell_test.cpp")

m_gtest_program(SceneTextures SOURCES "SceneTextures_test.cpp")

m_gtest_program(SceneTextureLeasePool SOURCES "SceneTextureLeasePool_test.cpp")

m_gtest_program(RetainedTexturePool SOURCES "RetainedTexturePool_test.cpp")

m_gtest_program(
  LightingService
  SOURCES
    "LightingService_test.cpp"
    "Lighting/LightPhotometry_test.cpp"
    "Lighting/LightEvaluationRecords_test.cpp"
    "Lighting/DeferredLightConstantsPublisher_test.cpp"
    "Fixtures/UploadCoordinatorTest.cpp"
)
target_sources(
  Oxygen.Vortex.LightingService.Tests
  PRIVATE
    "../Lighting/Internal/LightGridBuilder.cpp"
    "../Lighting/Internal/LightEvaluationRecords.cpp"
    "../Lighting/Internal/DeferredLightConstantsPublisher.cpp"
)

m_gtest_program(PostProcessService SOURCES "PostProcessService_test.cpp")

m_gtest_program(
  ShadowService
  SOURCES
    "Fixtures/MeshRasterStateTest.h"
    "ShadowService_test.cpp"
)
target_compile_definitions(
  Oxygen.Vortex.ShadowService.Tests
  PRIVATE
    OXYGEN_D3D12_VORTEX_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex"
)

m_gtest_program(
  EnvironmentLightingService
  SOURCES
    "EnvironmentLightingService_test.cpp"
)
target_compile_definitions(
  Oxygen.Vortex.EnvironmentLightingService.Tests
  PRIVATE
    OXYGEN_D3D12_VORTEX_SHADER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex"
)

m_gtest_program(
  SceneRendererPublication
  SOURCES
    "SceneRendererPublication_test.cpp"
)

m_gtest_program(
  SceneRendererDeferredCore
  SOURCES
    "Fixtures/MeshRasterStateTest.h"
    "SceneRendererDeferredCore_test.cpp"
)

m_gtest_program(
  OcclusionModule
  SOURCES
    "Fakes/Graphics.h"
    "OcclusionModule_test.cpp"
)

add_custom_target(
  Oxygen.Vortex.SceneRendererShell
  DEPENDS
    Oxygen.Vortex.SceneRendererShell.Tests
)

add_custom_target(
  Oxygen.Vortex.SceneTextures
  DEPENDS
    Oxygen.Vortex.SceneTextures.Tests
)

add_custom_target(
  Oxygen.Vortex.SceneTextureLeasePool
  DEPENDS
    Oxygen.Vortex.SceneTextureLeasePool.Tests
)

add_custom_target(
  Oxygen.Vortex.RetainedTexturePool
  DEPENDS
    Oxygen.Vortex.RetainedTexturePool.Tests
)

add_custom_target(
  Oxygen.Vortex.LightingService
  DEPENDS
    Oxygen.Vortex.LightingService.Tests
)

add_custom_target(
  Oxygen.Vortex.PostProcessService
  DEPENDS
    Oxygen.Vortex.PostProcessService.Tests
)

add_custom_target(
  Oxygen.Vortex.DiagnosticsService
  DEPENDS
    Oxygen.Vortex.DiagnosticsService.Tests
)

add_custom_target(
  Oxygen.Vortex.DiagnosticsFrameLedger
  DEPENDS
    Oxygen.Vortex.DiagnosticsFrameLedger.Tests
)

add_custom_target(
  Oxygen.Vortex.DiagnosticsCaptureManifest
  DEPENDS
    Oxygen.Vortex.DiagnosticsCaptureManifest.Tests
)

add_custom_target(
  Oxygen.Vortex.ShaderDebugModeRegistry
  DEPENDS
    Oxygen.Vortex.ShaderDebugModeRegistry.Tests
)

add_custom_target(
  Oxygen.Vortex.ShadowService
  DEPENDS
    Oxygen.Vortex.ShadowService.Tests
)

add_custom_target(
  Oxygen.Vortex.EnvironmentLightingService
  DEPENDS
    Oxygen.Vortex.EnvironmentLightingService.Tests
)

add_custom_target(
  Oxygen.Vortex.SceneRendererPublication
  DEPENDS
    Oxygen.Vortex.SceneRendererPublication.Tests
)

add_custom_target(
  Oxygen.Vortex.SceneRendererDeferredCore
  DEPENDS
    Oxygen.Vortex.SceneRendererDeferredCore.Tests
)

add_custom_target(
  Oxygen.Vortex.OcclusionModule
  DEPENDS
    Oxygen.Vortex.OcclusionModule.Tests
)

m_gtest_program(
  SceneCameraViewResolver
  SOURCES
    "SceneCameraViewResolver_test.cpp"
)

m_gtest_program(
  RenderContextMaterializer
  SOURCES
    "Fakes/Graphics.h"
    "Internal/RenderContextMaterializer_test.cpp"
)

m_gtest_program(
  AuxiliaryDependencyGraph
  SOURCES
    "Fakes/Graphics.h"
    "Internal/AuxiliaryDependencyGraph_test.cpp"
)

m_gtest_program(
  CompositionPlanner
  SOURCES
    "Fakes/Graphics.h"
    "Internal/CompositionPlanner_test.cpp"
)

m_gtest_program(CompositingPass SOURCES "Internal/CompositingPass_test.cpp")

m_gtest_program(
  RendererCompositionQueue
  SOURCES
    "RendererCompositionQueue_test.cpp"
)

m_gtest_program(
  SinglePassHarnessFacade
  SOURCES
    "Fakes/Graphics.h"
    "Harness/SinglePassHarnessFacade_test.cpp"
)

m_gtest_program(
  RenderGraphHarnessFacade
  SOURCES
    "Fakes/Graphics.h"
    "Harness/RenderGraphHarnessFacade_test.cpp"
)

m_gtest_program(
  OffscreenSceneFacade
  SOURCES
    "Fakes/Graphics.h"
    "Harness/OffscreenSceneFacade_test.cpp"
)

m_gtest_program(
  RendererPublicationSplit
  SOURCES
    "Fakes/Graphics.h"
    "Internal/RuntimeViewPublication_test.cpp"
)

m_gtest_program(
  ViewConstantsManager
  SOURCES
    "Fakes/Graphics.h"
    "Internal/ViewConstantsManager_test.cpp"
)

m_gtest_program(
  DeformationHistoryCache
  SOURCES
    "Internal/DeformationHistoryCache_test.cpp"
)

m_gtest_program(
  PreviousViewHistoryCache
  SOURCES
    "Internal/PreviousViewHistoryCache_test.cpp"
)

m_gtest_program(
  RendererFacadePresets
  SOURCES
    "Fakes/Graphics.h"
    "Harness/FacadePresets_test.cpp"
)

m_gtest_program(
  UploadCoordinator
  SOURCES
    "Fakes/Graphics.h"
    "Fixtures/UploadCoordinatorTest.cpp"
    "Fixtures/UploadCoordinatorTest.h"
    "Upload/UploadCoordinator_buffer_test.cpp"
    "Upload/UploadCoordinator_texture2d_test.cpp"
    "Upload/UploadCoordinator_texture3d_test.cpp"
    "Upload/UploadCoordinator_shutdown_test.cpp"
)

m_gtest_program(
  RingBufferStaging
  SOURCES
    "Fakes/Graphics.h"
    "Upload/RingBufferStaging_basic_test.cpp"
    "Upload/RingBufferStaging_error_test.cpp"
    "Fixtures/RingBufferStagingFixture.h"
    "Fixtures/UploadCoordinatorTest.cpp"
    "Fixtures/UploadCoordinatorTest.h"
)

m_gtest_program(UploadTracker SOURCES "Upload/UploadTracker_basic_test.cpp")

m_gtest_program(AtlasBuffer SOURCES "Upload/AtlasBuffer_basic_test.cpp")

m_gtest_program(
  UploadPlanner
  SOURCES
    "Upload/UploadPlanner_basic_test.cpp"
    "Upload/UploadPlanner_buffer_test.cpp"
    "Upload/UploadPlanner_texture_test.cpp"
)

m_gtest_program(
  TransientStructuredBuffer
  SOURCES
    "Fakes/Graphics.h"
    "Upload/TransientStructuredBuffer_test.cpp"
    "Fixtures/RingBufferStagingFixture.h"
    "Fixtures/UploadCoordinatorTest.cpp"
    "Fixtures/UploadCoordinatorTest.h"
)

m_gtest_program(
  TextureBinder
  SOURCES
    "Fakes/Graphics.h"
    "Fixtures/TextureBinderTest.cpp"
    "Fixtures/TextureBinderTest.h"
    "Fixtures/TextureBinderPayloads.h"
    "Resources/TextureBinder_basic_test.cpp"
    "Resources/TextureBinder_upload_test.cpp"
    "Resources/TextureBinder_eviction_test.cpp"
    "Resources/TextureBinder_failures_test.cpp"
)

m_gtest_program(
  MaterialBinder
  SOURCES
    "Fakes/Graphics.h"
    "Fixtures/MaterialBinderTest.cpp"
    "Fixtures/MaterialBinderTest.h"
    "Resources/MaterialBinder_basic_test.cpp"
    "Resources/MaterialBinder_edge_test.cpp"
    "Resources/MaterialBinder_binding_test.cpp"
    "Resources/MaterialBinder_placeholder_test.cpp"
    "Resources/MaterialBinder_lifecycle_test.cpp"
    "Resources/MaterialBinder_error_stress_test.cpp"
)

m_gtest_program(
  TransformUploader
  SOURCES
    "Fakes/Graphics.h"
    "Resources/TransformUploader_test.cpp"
)

m_gtest_program(
  DrawMetadataEmitter
  SOURCES
    "Fakes/Graphics.h"
    "Fixtures/GeometryUploaderTest.h"
    "Resources/DrawMetadataEmitter_test.cpp"
)

m_gtest_program(
  GeometryUploader
  SOURCES
    "Fakes/Graphics.h"
    "Fixtures/GeometryUploaderTest.cpp"
    "Fixtures/GeometryUploaderTest.h"
    "Resources/GeometryUploader_basic_test.cpp"
    "Resources/GeometryUploader_lifecycle_test.cpp"
    "Resources/GeometryUploader_residency_test.cpp"
    "Resources/GeometryUploader_upload_test.cpp"
    "Resources/GeometryUploader_retire_test.cpp"
    "Resources/GeometryUploader_update_test.cpp"
    "Resources/GeometryUploader_eviction_test.cpp"
    "Resources/GeometryUploader_failures_test.cpp"
    "Resources/GeometryUploader_edge_test.cpp"
)

m_gtest_program(
  ScenePrep
  SOURCES
    "Fakes/Graphics.h"
    "ScenePrep/ScenePrepHelpers.h"
    "Fixtures/ScenePrepTestFixture.h"
    "ScenePrep/CollectionConfig_test.cpp"
    "ScenePrep/EmitPerVisibleSubmesh_test.cpp"
    "ScenePrep/ExtractionPreFilter_test.cpp"
    "ScenePrep/MeshResolver_test.cpp"
    "ScenePrep/RenderItemProto_test.cpp"
    "ScenePrep/ScenePrepPipeline_collection_test.cpp"
    "ScenePrep/SubMeshVisibilityFilter_test.cpp"
)

# Production exposure compute against controlled RGBA32 inputs on native D3D12.
if(WIN32)
  add_subdirectory(Support)
  add_subdirectory(Lighting)
  # Compile native fixture implementations once for correctness and benchmarks.
  add_library(
    oxygen-vortex-exposure-test-support
    STATIC
    "Exposure/Fixtures/ExposureAuxiliaryHandoff.cpp"
    "Exposure/Fixtures/ExposureGpuFixture.cpp"
    "Exposure/Fixtures/ExposureGpuFixture.h"
    "Exposure/Fixtures/ExposureGpuFixtureLifecycle.cpp"
    "Exposure/Fixtures/ExposureGpuFixtureService.cpp"
    "Exposure/Fixtures/ExposureLightingFixture.cpp"
    "Exposure/Fixtures/ExposureLightingFixture.h"
    "Exposure/Fixtures/ExposureSharedLifecycle.cpp"
    "Exposure/Fixtures/ExposureTestEngine.h"
    "Exposure/Fixtures/ExposureTestGraphics.cpp"
    "Exposure/Fixtures/ExposureTestGraphics.h"
    "Exposure/Fixtures/ExposureTestTags.cpp"
    "Exposure/Fixtures/ExposureTestTags.h"
  )
  set_target_properties(
    oxygen-vortex-exposure-test-support
    PROPERTIES
      FOLDER
        "Testing"
  )
  target_link_libraries(
    oxygen-vortex-exposure-test-support
    PUBLIC
      oxygen::vortex-test-support
      oxygen::cooker
      oxygen::vortex
      oxygen::graphics-direct3d12
      oxygen::testing
      nlohmann_json::nlohmann_json
  )
  gtest_program(
    "Oxygen.Vortex.Exposure.Tests"
    SOURCES
      "Exposure/Adaptation_test.cpp"
      "Exposure/CheckedConversion_test.cpp"
      "Exposure/ComposedAdmission_test.cpp"
      "Exposure/Diagnostics_test.cpp"
      "Exposure/EnvironmentLifetime_test.cpp"
      "Exposure/FilterBounds_test.cpp"
      "Exposure/FogComposition_test.cpp"
      "Exposure/FogHistory_test.cpp"
      "Exposure/FogDirectionalShadows_test.cpp"
      "Exposure/Fp32Only_test.cpp"
      "Exposure/Fp32Reference_test.cpp"
      "Exposure/FrameDomain_test.cpp"
      "Exposure/LightingRadiance_test.cpp"
      "Exposure/LocalFog_test.cpp"
      "Exposure/Masks_test.cpp"
      "Exposure/MaterialTextures_test.cpp"
      "Exposure/Metering_test.cpp"
      "Exposure/PrecisionLifecycle_test.cpp"
      "Exposure/ProducerComposition_test.cpp"
      "Exposure/ProducerMaximum_test.cpp"
      "Exposure/ProducerPublication_test.cpp"
      "Exposure/QueuedConsumers_test.cpp"
      "Exposure/RangeGuards_test.cpp"
      "Exposure/SceneLifecycle_test.cpp"
      "Exposure/ScenePrecision_test.cpp"
      "Exposure/ScenePreparation_test.cpp"
      "Exposure/SceneSharing_test.cpp"
      "Exposure/Sharing_test.cpp"
      "Exposure/SkyRadiance_test.cpp"
      "Exposure/SourceLoss_test.cpp"
      "Exposure/StateLifetime_test.cpp"
      "Exposure/Suitability_test.cpp"
      "Exposure/Tonemap_test.cpp"
      "Exposure/Transitions_test.cpp"
    DEPS
      oxygen-vortex-exposure-test-support
  )
  target_compile_definitions(
    oxygen-vortex-exposure-test-support
    PUBLIC
      OXYGEN_EXPOSURE_WORKSPACE="${PROJECT_SOURCE_DIR}"
  )
  # The native GPU fixtures exceed the standard COFF section-count limit.
  target_compile_options(
    oxygen-vortex-exposure-test-support
    PUBLIC
      $<$<CXX_COMPILER_ID:MSVC>:/bigobj>
  )
  # Compile the arithmetic probe separately; it must not enter shaders.bin.
  set(
    exposure_shader_root
    "${PROJECT_SOURCE_DIR}/src/Oxygen/Graphics/Direct3D12/Shaders"
  )
  set(
    exposure_probe_output
    "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/ToneBoundsProbe.dxil"
  )
  add_custom_command(
    OUTPUT
      "${exposure_probe_output}"
    COMMAND
      ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>"
    COMMAND
      "${OXYGEN_DXC_EXECUTABLE}" -T cs_6_6 -E CS -O3 -HV 2021 -I
      "${exposure_shader_root}" -I "${PROJECT_SOURCE_DIR}/src/Oxygen" -Fo
      "${exposure_probe_output}"
      "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ToneBoundsProbe.hlsl"
    # DXC dependency generation is a separate preprocessing action; -MD does
    # not compile DXIL, even when -Fo is supplied.
    COMMAND
      "${OXYGEN_DXC_EXECUTABLE}" -T cs_6_6 -E CS -O3 -HV 2021 -I
      "${exposure_shader_root}" -I "${PROJECT_SOURCE_DIR}/src/Oxygen" -MD -MF
      "${exposure_probe_output}.d" -Fo "${exposure_probe_output}"
      "${CMAKE_CURRENT_SOURCE_DIR}/Shaders/ToneBoundsProbe.hlsl"
    DEPENDS
      "${OXYGEN_DXC_EXECUTABLE}"
      "Shaders/ToneBoundsProbe.hlsl"
      "${exposure_shader_root}/Vortex/Contracts/View/HdrHardwareSampling.hlsli"
      "${exposure_shader_root}/Vortex/Contracts/View/HdrIntervalMath.hlsli"
      "${exposure_shader_root}/Vortex/Contracts/View/HdrRadianceRange.hlsli"
      "${exposure_shader_root}/Vortex/Services/Environment/TransmittanceMath.hlsli"
      "${exposure_shader_root}/Vortex/Services/Environment/LocalFogVolumeCommon.hlsli"
      "${exposure_shader_root}/Vortex/Services/Environment/VolumetricFog.hlsl"
      "${exposure_shader_root}/Vortex/Contracts/View/ExposureStateData.hlsli"
      "${exposure_shader_root}/Vortex/Services/PostProcess/HdrSceneComposition.hlsli"
      "${exposure_shader_root}/Vortex/Services/PostProcess/ToneMappingBounds.hlsli"
      "${exposure_shader_root}/Vortex/Services/PostProcess/ToneMappingFastBounds.hlsli"
      "${exposure_shader_root}/Vortex/Services/PostProcess/ToneMapping.hlsli"
      "${exposure_shader_root}/Vortex/Shared/ColorSpace.hlsli"
    DEPFILE "${exposure_probe_output}.d"
    VERBATIM
  )
  add_custom_target(
    oxygen-vortex-tone-bounds-probe
    DEPENDS
      "${exposure_probe_output}"
  )
  add_dependencies(
    oxygen-vortex-exposure-test-support
    oxygen-vortex-tone-bounds-probe
  )
  target_compile_definitions(
    oxygen-vortex-exposure-test-support
    PRIVATE
      OXYGEN_EXPOSURE_TONE_PROBE="${exposure_probe_output}"
  )
endif()

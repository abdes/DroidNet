# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ------------------------------------------------------------------------------
# Dependencies
# ------------------------------------------------------------------------------

find_package(GTest REQUIRED CONFIG)

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
target_compile_options(${link_test_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_compile_definitions(
  ${link_test_target}
  PRIVATE
    OXYGEN_VORTEX_SOURCE_DIR="${PROJECT_SOURCE_DIR}/src/Oxygen/Vortex"
)
target_link_libraries(${link_test_target} PRIVATE oxygen::vortex)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

m_gtest_program(
  RendererCapability
  SOURCES
    "RendererCapability_test.cpp"
)

m_gtest_program(
  GpuTimelineProfiler
  SOURCES
    "GpuTimelineProfiler_test.cpp"
)

m_gtest_program(
  RenderContext
  SOURCES
    "RenderContext_test.cpp"
)

m_gtest_program(
  AcceptedDrawView
  SOURCES
    "Types/AcceptedDrawView_test.cpp"
)

m_gtest_program(
  SceneRendererShell
  SOURCES
    "SceneRendererShell_test.cpp"
)

m_gtest_program(
  SceneTextures
  SOURCES
    "SceneTextures_test.cpp"
)

m_gtest_program(
  LightingService
  SOURCES
    "LightingService_test.cpp"
)

m_gtest_program(
  PostProcessService
  SOURCES
    "PostProcessService_test.cpp"
)

m_gtest_program(
  ShadowService
  SOURCES
    "ShadowService_test.cpp"
)

m_gtest_program(
  EnvironmentLightingService
  SOURCES
    "EnvironmentLightingService_test.cpp"
)

m_gtest_program(
  SceneRendererPublication
  SOURCES
    "SceneRendererPublication_test.cpp"
)

m_gtest_program(
  SceneRendererDeferredCore
  SOURCES
    "SceneRendererDeferredCore_test.cpp"
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
  CompositionPlanner
  SOURCES
    "Fakes/Graphics.h"
    "Internal/CompositionPlanner_test.cpp"
)

m_gtest_program(
  CompositingPass
  SOURCES
    "Internal/CompositingPass_test.cpp"
)

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

m_gtest_program(
  UploadTracker
  SOURCES
    "Upload/UploadTracker_basic_test.cpp"
)

m_gtest_program(
  AtlasBuffer
  SOURCES
    "Upload/AtlasBuffer_basic_test.cpp"
)

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

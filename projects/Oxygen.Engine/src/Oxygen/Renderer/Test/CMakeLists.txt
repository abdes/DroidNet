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

# -- Library Link Test ---------------------------------------------------------

set(link_test_target "Oxygen.Renderer.LinkTest")
add_executable(${link_test_target} "Link_test.cpp")
set_target_properties(
  ${link_test_target}
  PROPERTIES
    FOLDER
      "Testing"
)
target_compile_options(${link_test_target} PRIVATE ${OXYGEN_COMMON_CXX_FLAGS})
target_link_libraries(${link_test_target} PRIVATE oxygen::renderer)
add_test(NAME ${link_test_target} COMMAND ${link_test_target})

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(Frustum SOURCES "Frustum_test.cpp")

m_gtest_program(GpuTimelineProfiler SOURCES "GpuTimelineProfiler_test.cpp")

m_gtest_program(
  GpuTimelinePresentationSmoother
  SOURCES
    "GpuTimelinePresentationSmoother_test.cpp"
)

m_gtest_program(PassDebugNames SOURCES "PassDebugNames_test.cpp")

m_gtest_program(
  CompositionPlanner
  SOURCES
    "Fakes/Graphics.h"
    "CompositionPlanner_test.cpp"
)

# MaterialPermutations unit tests (no backend dependency)
m_gtest_program(MaterialPermutations SOURCES "MaterialPermutations_test.cpp")

m_gtest_program(
  "Oxygen.Renderer.VirtualShadows.Tests"
  SOURCES
    "VirtualShadow/VirtualShadowTestFixtures.h"
    "VirtualShadow/VsmPhysicalPoolCompatibility_test.cpp"
    "VirtualShadow/VsmPhysicalPoolAddressing_test.cpp"
    "VirtualShadow/VsmPhysicalPagePoolManager_test.cpp"
    "VirtualShadow/VsmCacheManagerFrameLifecycle_test.cpp"
    "VirtualShadow/VsmCacheManagerInvalidation_test.cpp"
    "VirtualShadow/VsmCacheManagerOrchestration_test.cpp"
    "VirtualShadow/VsmCacheManagerRetention_test.cpp"
    "VirtualShadow/VsmCacheManagerScaffold_test.cpp"
    "VirtualShadow/VsmCacheManagerSeam_test.cpp"
    "VirtualShadow/VsmSceneInvalidationCollector_test.cpp"
    "VirtualShadow/VsmShaderTypes_test.cpp"
    "VirtualShadow/VsmPageRequestGeneration_test.cpp"
    "VirtualShadow/VsmCacheManagerState_test.cpp"
    "VirtualShadow/VsmCacheManagerTypes_test.cpp"
    "VirtualShadow/VsmPageAllocationPlanner_test.cpp"
    "VirtualShadow/VsmShadowRasterJobs_test.cpp"
    "VirtualShadow/VsmVirtualClipmapHelpers_test.cpp"
    "VirtualShadow/VsmVirtualRemapBuilder_test.cpp"
    "VirtualShadow/VsmVirtualAddressSpaceIds_test.cpp"
    "VirtualShadow/VsmVirtualAddressSpaceLayouts_test.cpp"
)

source_group(
  TREE ${CMAKE_CURRENT_SOURCE_DIR}
  PREFIX VirtualShadows
  FILES
    "VirtualShadow/VirtualShadowTestFixtures.h"
    "VirtualShadow/VsmPhysicalPoolCompatibility_test.cpp"
    "VirtualShadow/VsmPhysicalPoolAddressing_test.cpp"
    "VirtualShadow/VsmPhysicalPagePoolManager_test.cpp"
    "VirtualShadow/VsmCacheManagerFrameLifecycle_test.cpp"
    "VirtualShadow/VsmCacheManagerInvalidation_test.cpp"
    "VirtualShadow/VsmCacheManagerOrchestration_test.cpp"
    "VirtualShadow/VsmCacheManagerRetention_test.cpp"
    "VirtualShadow/VsmCacheManagerScaffold_test.cpp"
    "VirtualShadow/VsmCacheManagerSeam_test.cpp"
    "VirtualShadow/VsmSceneInvalidationCollector_test.cpp"
    "VirtualShadow/VsmShaderTypes_test.cpp"
    "VirtualShadow/VsmPageRequestGeneration_test.cpp"
    "VirtualShadow/VsmCacheManagerState_test.cpp"
    "VirtualShadow/VsmCacheManagerTypes_test.cpp"
    "VirtualShadow/VsmPageAllocationPlanner_test.cpp"
    "VirtualShadow/VsmShadowRasterJobs_test.cpp"
    "VirtualShadow/VsmVirtualClipmapHelpers_test.cpp"
    "VirtualShadow/VsmVirtualRemapBuilder_test.cpp"
    "VirtualShadow/VsmVirtualAddressSpaceIds_test.cpp"
    "VirtualShadow/VsmVirtualAddressSpaceLayouts_test.cpp"
)

gtest_program(
  "Oxygen.Renderer.VirtualShadowSceneObserver.Tests"
  SOURCES
    "VirtualShadow/VsmSceneObserverIntegration_test.cpp"
  DEPS
    oxygen::renderer
    oxygen::testing
)

source_group(
  TREE ${CMAKE_CURRENT_SOURCE_DIR}
  PREFIX VirtualShadowsIntegration
  FILES
    "VirtualShadow/VsmSceneObserverIntegration_test.cpp"
)

if(TARGET oxygen::graphics-direct3d12)
  m_gtest_program(
    "ScreenHzb"
    SOURCES
      "ScreenHzbBuildPass_test.cpp"
      "Fixtures/RendererOffscreenGpuTestFixture.h"
      DEPS
      oxygen::graphics-direct3d12
  )

  m_gtest_program(
    "VirtualShadowGpuLifecycle"
    SOURCES
      "VirtualShadow/VirtualShadowGpuTestFixtures.h"
      "VirtualShadow/VsmCacheManagerGpuResources_test.cpp"
      "VirtualShadow/VsmPageLifecyclePasses_test.cpp"
      "VirtualShadow/VsmPageManagementPass_test.cpp"
      "VirtualShadow/VsmPageRequestGeneratorPass_test.cpp"
      "VirtualShadow/VsmShadowRendererBridge_test.cpp"
      "VirtualShadow/VsmInvalidationPass_test.cpp"
      "VirtualShadow/VsmHzbUpdaterPass_test.cpp"
      "VirtualShadow/VsmProjectionPass_test.cpp"
      "VirtualShadow/VsmPhysicalPagePoolGpuLifecycle_test.cpp"
      "VirtualShadow/VsmStaticDynamicMergePass_test.cpp"
      "VirtualShadow/VsmShadowRasterizerPass_test.cpp"
      DEPS
      oxygen::graphics-direct3d12
  )

  source_group(
    TREE ${CMAKE_CURRENT_SOURCE_DIR}
    PREFIX VirtualShadowsDedicated
    FILES
      "VirtualShadow/VirtualShadowGpuTestFixtures.h"
      "ScreenHzbBuildPass_test.cpp"
      "Fixtures/RendererOffscreenGpuTestFixture.h"
      "VirtualShadow/VsmCacheManagerGpuResources_test.cpp"
      "VirtualShadow/VsmPageLifecyclePasses_test.cpp"
      "VirtualShadow/VsmPageManagementPass_test.cpp"
      "VirtualShadow/VsmPageRequestGeneratorPass_test.cpp"
      "VirtualShadow/VsmShadowRendererBridge_test.cpp"
      "VirtualShadow/VsmInvalidationPass_test.cpp"
      "VirtualShadow/VsmHzbUpdaterPass_test.cpp"
      "VirtualShadow/VsmProjectionPass_test.cpp"
      "VirtualShadow/VsmPhysicalPagePoolGpuLifecycle_test.cpp"
      "VirtualShadow/VsmStaticDynamicMergePass_test.cpp"
      "VirtualShadow/VsmShadowRasterizerPass_test.cpp"
  )
endif()

m_gtest_program(SunResolver SOURCES "SunResolver_basic_test.cpp")

m_gtest_program(
  ShadowManagerPolicy
  SOURCES
    "Fakes/Graphics.h"
    "ShadowManager_policy_test.cpp"
)

m_gtest_program(
  ScenePrep
  SOURCES
    "Fakes/Graphics.h"
    "ScenePrep/ScenePrepHelpers.h"
    "ScenePrep/ScenePrepTestFixture.h"
    "ScenePrep/CollectionConfig_test.cpp"
    "ScenePrep/EmitPerVisibleSubmesh_test.cpp"
    "ScenePrep/ExtractionPreFilter_test.cpp"
    "ScenePrep/LightManager_pipeline_integration_test.cpp"
    "ScenePrep/MeshResolver_test.cpp"
    "ScenePrep/RenderItemProto_test.cpp"
    "ScenePrep/ScenePrepPipeline_collection_test.cpp"
    "ScenePrep/SubMeshVisibilityFilter_test.cpp"
)

# UploadCoordinator (buffer path) mocked tests
m_gtest_program(
  UploadCoordinator
  SOURCES
    "Fakes/Graphics.h"
    "Upload/UploadCoordinatorTest.cpp"
    "Upload/UploadCoordinatorTest.h"
    "Upload/UploadCoordinator_buffer_test.cpp"
    "Upload/UploadCoordinator_texture2d_test.cpp"
    "Upload/UploadCoordinator_texture3d_test.cpp"
)

# Upload unit tests (no backend dependency)
m_gtest_program(
  RingBufferStaging
  SOURCES
    "Fakes/Graphics.h"
    "Upload/RingBufferStaging_basic_test.cpp"
    "Upload/RingBufferStaging_error_test.cpp"
    "Upload/UploadCoordinatorTest.cpp"
    "Upload/UploadCoordinatorTest.h"
)

# Upload unit tests (no backend dependency)
m_gtest_program(UploadTracker SOURCES "Upload/UploadTracker_basic_test.cpp")

# AtlasBuffer unit tests (no backend dependency)
m_gtest_program(AtlasBuffer SOURCES "Upload/AtlasBuffer_basic_test.cpp")

# UploadPlanner unit tests (backend-free)
m_gtest_program(
  UploadPlanner
  SOURCES
    "Upload/UploadPlanner_texture_test.cpp"
    "Upload/UploadPlanner_buffer_test.cpp"
)

# TransientStructuredBuffer unit tests
m_gtest_program(
  TransientStructuredBuffer
  SOURCES
    "Fakes/Graphics.h"
    "Upload/TransientStructuredBuffer_test.cpp"
    "Upload/RingBufferStagingFixture.h"
    "Upload/UploadCoordinatorTest.cpp"
    "Upload/UploadCoordinatorTest.h"
)

# TextureBinder unit tests (no backend dependency)
m_gtest_program(
  TextureBinder
  SOURCES
    "Fakes/Graphics.h"
    "Resources/TextureBinderTest.cpp"
    "Resources/TextureBinderTest.h"
    "Resources/TextureBinder_basic_test.cpp"
    "Resources/TextureBinder_upload_test.cpp"
    "Resources/TextureBinder_eviction_test.cpp"
    "Resources/TextureBinder_failures_test.cpp"
)

# MaterialBinder contract tests (backend-free)
m_gtest_program(
  MaterialBinder
  SOURCES
    "Fakes/Graphics.h"
    "Resources/MaterialBinderTest.cpp"
    "Resources/MaterialBinderTest.h"
    "Resources/MaterialBinder_basic_test.cpp"
    "Resources/MaterialBinder_edge_test.cpp"
    "Resources/MaterialBinder_binding_test.cpp"
    "Resources/MaterialBinder_placeholder_test.cpp"
    "Resources/MaterialBinder_lifecycle_test.cpp"
    "Resources/MaterialBinder_error_stress_test.cpp"
)

# TextureBinder unit tests (no backend dependency)
m_gtest_program(
  TransformUploader
  SOURCES
    "Fakes/Graphics.h"
    "Resources/TransformUploader_test.cpp"
)

# DrawMetadataEmitter unit tests
m_gtest_program(
  DrawMetadataEmitter
  SOURCES
    "Fakes/Graphics.h"
    "Resources/GeometryUploaderTest.h"
    "Resources/DrawMetadataEmitter_test.cpp"
)

# LightManager unit tests
m_gtest_program(
  LightManager
  SOURCES
    "Fakes/Graphics.h"
    "LightManager_basic_test.cpp"
)

# GeometryUploader unit tests
m_gtest_program(
  GeometryUploader
  SOURCES
    "Fakes/Graphics.h"
    "Resources/GeometryUploaderTest.cpp"
    "Resources/GeometryUploaderTest.h"
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

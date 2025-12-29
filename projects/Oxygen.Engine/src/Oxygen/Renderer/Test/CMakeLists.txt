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

m_gtest_program(
  ScenePrep
  SOURCES
    "ScenePrep/ScenePrepHelpers.h"
    "ScenePrep/ScenePrepTestFixture.h"
    "ScenePrep/CollectionConfig_test.cpp"
    "ScenePrep/EmitPerVisibleSubmesh_test.cpp"
    "ScenePrep/ExtractionPreFilter_test.cpp"
    "ScenePrep/MeshResolver_test.cpp"
    "ScenePrep/RenderItemProto_test.cpp"
    "ScenePrep/ScenePrepPipeline_collection_test.cpp"
    "ScenePrep/SubMeshVisibilityFilter_test.cpp"
)
target_compile_definitions(
  Oxygen.Renderer.ScenePrep.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
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
target_compile_definitions(
  Oxygen.Renderer.UploadCoordinator.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)

# Upload unit tests (no backend dependency)
m_gtest_program(
  RingBufferStaging
  SOURCES
    "Fakes/Graphics.h"
    "Upload/RingBufferStaging_basic_test.cpp"
    "Upload/RingBufferStaging_error_test.cpp"
    "Upload/RingBufferStagingFixture.cpp"
    "Upload/RingBufferStagingFixture.h"
    "Upload/UploadCoordinatorTest.cpp"
    "Upload/UploadCoordinatorTest.h"
)
target_compile_definitions(
  Oxygen.Renderer.RingBufferStaging.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)

# Upload unit tests (no backend dependency)
m_gtest_program(UploadTracker SOURCES "Upload/UploadTracker_basic_test.cpp")
target_compile_definitions(
  Oxygen.Renderer.UploadTracker.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)

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
target_compile_definitions(
  Oxygen.Renderer.TransientStructuredBuffer.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
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
    "Resources/TextureBinder_failures_test.cpp"
)
target_compile_definitions(
  Oxygen.Renderer.TextureBinder.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
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
target_compile_definitions(
  Oxygen.Renderer.MaterialBinder.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)

# TextureBinder unit tests (no backend dependency)
m_gtest_program(
  TransformUploader
  SOURCES
    "Fakes/Graphics.h"
    "Resources/TransformUploader_test.cpp"
)
target_compile_definitions(
  Oxygen.Renderer.TransformUploader.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
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
    "Resources/GeometryUploader_failures_test.cpp"
    "Resources/GeometryUploader_edge_test.cpp"
)
target_compile_definitions(
  Oxygen.Renderer.GeometryUploader.Tests
  PRIVATE
    OXYGEN_ENGINE_TESTING
)

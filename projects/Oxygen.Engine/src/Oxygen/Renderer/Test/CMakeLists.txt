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
    "ScenePrep/CollectionConfig_basic_test.cpp"
    "ScenePrep/DrawMetadata_material_handle_test.cpp"
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
m_gtest_program(UploadTracker SOURCES "Upload/UploadTracker_basic_test.cpp")

# UploadPlanner unit tests (backend-free)
m_gtest_program(UploadPlanner SOURCES "Upload/UploadPlanner_basic_test.cpp")

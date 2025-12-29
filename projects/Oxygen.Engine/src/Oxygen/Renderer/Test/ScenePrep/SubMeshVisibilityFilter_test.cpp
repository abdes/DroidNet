//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/ScenePrep/Extractors.h>
#include <Oxygen/Renderer/ScenePrep/RenderItemProto.h>
#include <Oxygen/Renderer/ScenePrep/ScenePrepState.h>
#include <Oxygen/Renderer/ScenePrep/Types.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

#include <Oxygen/Renderer/Test/Sceneprep/ScenePrepHelpers.h>
#include <Oxygen/Renderer/Test/Sceneprep/ScenePrepTestFixture.h>

using oxygen::View;

using oxygen::engine::sceneprep::MeshResolver;
using oxygen::engine::sceneprep::RenderItemProto;
using oxygen::engine::sceneprep::ScenePrepContext;
using oxygen::engine::sceneprep::ScenePrepState;
using oxygen::engine::sceneprep::SubMeshVisibilityFilter;

using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::scene::FixedPolicy;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;

using namespace oxygen::engine::sceneprep::testing;

namespace {

class SubMeshVisibilityFilterTest : public ScenePrepTestFixture {
protected:
  auto SetUp() -> void override
  {
    ScenePrepTestFixture::SetUp();
    EmplaceContextWithView();
  }
};

//! Death test: SubMeshVisibilityFilter must not accept a dropped proto.
/*!
 Passing a proto marked dropped is an invalid precondition and
 should cause the filter to terminate (death test).
*/
NOLINT_TEST_F(SubMeshVisibilityFilterTest, DroppedItem_Death)
{
  // Arrange
  MarkDropped();

  // Act + Assert
  NOLINT_EXPECT_DEATH(
    SubMeshVisibilityFilter(Context(), State(), Proto()), ".*");
}

//! Death test: calling SubMeshVisibilityFilter with no geometry should die.
/*!
 The filter requires geometry to inspect submesh bounds; missing
 geometry is a precondition violation and should result in death.
*/
NOLINT_TEST_F(SubMeshVisibilityFilterTest, ProtoNoGeometry_Death)
{
  // Geometry not explicitly set
  NOLINT_EXPECT_DEATH(
    SubMeshVisibilityFilter(Context(), State(), Proto()), ".*");
}

//! If no mesh is resolved, the proto should be marked dropped and no
//! visible submeshes collected.
/*!
 SubMeshVisibilityFilter expects a resolved mesh. When ResolvedMesh() is null
 the filter should mark the proto dropped and leave VisibleSubmeshes empty.
*/
NOLINT_TEST_F(SubMeshVisibilityFilterTest, NoResolvedMesh_MarksDropped)
{
  // Arrange
  const auto geom = MakeGeometryWithLODSubmeshes({ 3 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();

  // Do NOT run MeshResolver here -> ResolvedMesh() is null

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert
  EXPECT_TRUE(Proto().IsDropped());
  EXPECT_TRUE(Proto().VisibleSubmeshes().empty());
}

//! All submeshes visible -> indices [0..N-1]
/*!
 With a single LOD containing three submeshes and the object in
 the frustum, all submesh indices should be collected in order.
*/
NOLINT_TEST_F(SubMeshVisibilityFilterTest, AllVisible_CollectsAllIndices)
{
  // Arrange: 1 LOD with 3 submeshes, resolve mesh
  const auto geom = MakeGeometryWithLODSubmeshes({ 3 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();

  // Resolve via MeshResolver (fixed policy default to LOD0)
  // Use a proper perspective view to keep the mesh in frustum
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  // Proto should now have a resolved mesh and not be dropped
  EXPECT_FALSE(Proto().IsDropped());
  EXPECT_TRUE(static_cast<bool>(Proto().ResolvedMesh()));

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert
  const auto vis = Proto().VisibleSubmeshes();
  ASSERT_EQ(vis.size(), 3u);
  EXPECT_EQ(vis[0], 0u);
  EXPECT_EQ(vis[1], 1u);
  EXPECT_EQ(vis[2], 2u);
}

//! Some hidden -> only visible indices are collected
/*!
 When certain submeshes are marked hidden on the renderable, the
filter must exclude them from the visible list while preserving others.
*/
NOLINT_TEST_F(SubMeshVisibilityFilterTest, SomeHidden_FiltersOutHidden)
{
  // Arrange: 1 LOD with 4 submeshes
  const auto geom = MakeGeometryWithLODSubmeshes({ 4 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  const auto lod = Proto().ResolvedMeshIndex();
  // Hide 1 and 3
  DLOG_F(INFO, "TEST: Before SetSubmeshVisible: Node.IsValid={}, has_obj={}",
    Node().IsValid(), Node().GetImpl().has_value());
  Node().GetRenderable().SetSubmeshVisible(lod, 1, false);
  DLOG_F(INFO,
    "TEST: After first SetSubmeshVisible: Node.IsValid={}, has_obj={}",
    Node().IsValid(), Node().GetImpl().has_value());
  Node().GetRenderable().SetSubmeshVisible(lod, 3, false);
  DLOG_F(INFO,
    "TEST: After second SetSubmeshVisible: Node.IsValid={}, has_obj={}",
    Node().IsValid(), Node().GetImpl().has_value());

  // Ensure scene reflects the renderable state changes before extraction
  UpdateScene();

  // Proto should still be valid for visibility filtering
  EXPECT_FALSE(Proto().IsDropped());
  EXPECT_TRUE(static_cast<bool>(Proto().ResolvedMesh()));

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert
  const auto vis = Proto().VisibleSubmeshes();
  ASSERT_EQ(vis.size(), 2u);
  EXPECT_EQ(vis[0], 0u);
  EXPECT_EQ(vis[1], 2u);
}

//! Different LODs: ensure selection uses active LOD submesh set
/*!
 For multi-LOD geometry the filter must inspect the active LOD's
submesh set when building the visible indices.
*/
NOLINT_TEST_F(SubMeshVisibilityFilterTest, MultiLOD_UsesActiveLODSubmeshes)
{
  // Arrange: LOD0 has 2 submeshes, LOD1 has 1
  const auto geom = MakeGeometryWithLODSubmeshes({ 2, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  // Force LOD1 (coarser) via fixed policy
  Node().GetRenderable().SetLodPolicy(FixedPolicy { 1 });
  // Ensure LOD policy change is applied to the scene/component state
  DLOG_F(INFO,
    "TEST: Before UpdateScene (SetLodPolicy): Node.IsValid={}, has_obj={}",
    Node().IsValid(), Node().GetImpl().has_value());
  UpdateScene();
  DLOG_F(INFO,
    "TEST: After UpdateScene (SetLodPolicy): Node.IsValid={}, has_obj={}",
    Node().IsValid(), Node().GetImpl().has_value());
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  // Proto must be valid and have the resolved mesh for LOD1
  EXPECT_FALSE(Proto().IsDropped());
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 1u);

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert: only submesh 0 exists at LOD1
  const auto vis = Proto().VisibleSubmeshes();
  ASSERT_EQ(vis.size(), 1u);
  EXPECT_EQ(vis[0], 0u);
}

//! All hidden -> visible list becomes empty
/*!
 When all submeshes are marked hidden, the filter should return an
empty visible list.
*/
NOLINT_TEST_F(SubMeshVisibilityFilterTest, AllHidden_ResultsInEmptyList)
{
  const auto geom = MakeGeometryWithLODSubmeshes({ 3 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  Node().GetRenderable().SetAllSubmeshesVisible(false);

  // Ensure scene reflects the renderable state changes before extraction
  DLOG_F(INFO,
    "TEST: Before UpdateScene (SetAllSubmeshesVisible): Node.IsValid={}, "
    "has_obj={}",
    Node().IsValid(), Node().GetImpl().has_value());
  UpdateScene();
  DLOG_F(INFO,
    "TEST: After UpdateScene (SetAllSubmeshesVisible): Node.IsValid={}, "
    "has_obj={}",
    Node().IsValid(), Node().GetImpl().has_value());

  // Ensure visibility changes are applied
  UpdateScene();

  SubMeshVisibilityFilter(Context(), State(), Proto());

  EXPECT_TRUE(Proto().VisibleSubmeshes().empty());
}

//! Frustum: looking away from the object -> all submeshes culled
/*!
 When the camera is oriented away from the object the frustum
tests exclude all submeshes and the visible list must be empty.
*/
NOLINT_TEST_F(SubMeshVisibilityFilterTest, Frustum_AllOutside_RemovesAll)
{
  // Arrange: 1 LOD with 3 submeshes
  const auto geom = MakeGeometryWithLODSubmeshes({ 3 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  // Camera looks away from origin so geometry at z≈0 is behind frustum
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 10));
  MeshResolver(Context(), State(), Proto());

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert: all culled
  EXPECT_TRUE(Proto().VisibleSubmeshes().empty());
}

//! Frustum: spread submeshes across X; only the center is visible
/*!
 Submeshes located far left/right should be culled by the
frustum while the center remains visible.
*/
NOLINT_TEST_F(SubMeshVisibilityFilterTest, Frustum_PartialVisible_SelectsSubset)
{
  // Arrange: single LOD with 3 submeshes at X=-100,0,100
  using oxygen::data::Mesh;
  const std::vector<glm::vec3> centers
    = { { -100.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }, { 100.f, 0.f, 0.f } };
  const auto mesh = MakeSpreadMesh(0, centers);
  oxygen::data::pak::GeometryAssetDesc desc {};
  desc.lod_count = 1;
  const auto geom = std::make_shared<GeometryAsset>(
    oxygen::data::AssetKey {}, desc, std::vector { mesh });

  SetGeometry(geom);
  SeedVisibilityAndTransform();
  ConfigurePerspectiveView(glm::vec3(0, 0, 5), glm::vec3(0, 0, 0));
  MeshResolver(Context(), State(), Proto());

  // Act
  SubMeshVisibilityFilter(Context(), State(), Proto());

  // Assert: only the middle submesh (index 1) is inside the frustum
  const auto vis = Proto().VisibleSubmeshes();
  ASSERT_EQ(vis.size(), 1u);
  EXPECT_EQ(vis[0], 1u);
}

} // namespace

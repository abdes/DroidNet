//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Testing/GTest.h>

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

using oxygen::engine::sceneprep::ExtractionPreFilter;
using oxygen::engine::sceneprep::RenderItemProto;
using oxygen::engine::sceneprep::ScenePrepContext;
using oxygen::engine::sceneprep::ScenePrepState;

using oxygen::data::GeometryAsset;
using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::scene::DistancePolicy;
using oxygen::scene::FixedPolicy;
using oxygen::scene::Scene;
using oxygen::scene::SceneNode;
using oxygen::scene::SceneNodeFlags;
using oxygen::scene::ScreenSpaceErrorPolicy;

using namespace oxygen::engine::sceneprep::testing;

namespace {

class MeshResolverTest : public ScenePrepTestFixture {
protected:
  auto SetUp() -> void override
  {
    ScenePrepTestFixture::SetUp();
    // default context with base's default view
    EmplaceContextWithView();
  }
};

//! Verifies MeshResolver asserts (death) when the proto is marked dropped.
/*!
 This is a death test: a dropped proto is an invalid input to MeshResolver and
 should cause the implementation to terminate (test framework death
 expectation). No further state is asserted.
*/
NOLINT_TEST_F(MeshResolverTest, DroppedItem_Death)
{
  // Arrange
  MarkDropped();

  // Act + Assert: MeshResolver must fail fast for dropped protos
  NOLINT_EXPECT_DEATH(MeshResolver(Context(), State(), Proto()), ".*");
}

//! Verifies MeshResolver dies if the proto has no geometry set.
/*!
 MeshResolver requires a geometry pointer in the proto. Passing a proto without
 geometry is undefined behaviour for the resolver and should result in process
 termination (death test). The test does not examine state after the call.
*/
NOLINT_TEST_F(MeshResolverTest, ProtoNoGeometry_Death)
{
  // Arrange: proto is not dropped but geometry was not provided

  // Act + Assert: expect death due to missing geometry
  NOLINT_EXPECT_DEATH(MeshResolver(Context(), State(), Proto()), ".*");
}

//=== Positive paths: fixed LOD policy ---------------------------------------//

//! Verifies FixedPolicy selects LOD 0 when policy requests index 0.
/*!
 Arrange: geometry with 3 LODs and a FixedPolicy requesting LOD 0. Act: run
 MeshResolver. Assert: the proto's resolved mesh index matches the policy and a
 mesh pointer (possibly null only if the asset had null meshes) is set. We only
 require that the index is 0 and that ResolvedMesh() is truthy when the asset
 contains a mesh for that LOD.
*/
NOLINT_TEST_F(MeshResolverTest, FixedPolicy_SelectsLOD0)
{
  // Arrange: 3 LOD geometry and fixed policy 0
  const auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  Node().GetRenderable().SetLodPolicy(FixedPolicy { 0 });

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: index must match requested LOD
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 0u);
  EXPECT_TRUE(static_cast<bool>(Proto().ResolvedMesh()));
}

//! Verifies FixedPolicy selects the requested LOD index when within range.
NOLINT_TEST_F(MeshResolverTest, FixedPolicy_SelectsLOD2)
{
  // Arrange: 3 LOD geometry and fixed policy 2
  const auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  Node().GetRenderable().SetLodPolicy(FixedPolicy { 2 });

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 2u);
  EXPECT_TRUE(static_cast<bool>(Proto().ResolvedMesh()));
}

//=== Distance policy: select based on normalized distance =================//

//! DistancePolicy chooses finer LOD when camera is very near the object.
NOLINT_TEST_F(MeshResolverTest, DistancePolicy_Near_SelectsFineLOD)
{
  // Arrange
  const auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  const DistancePolicy dp { .thresholds = { 2.0f, 10.0f },
    .hysteresis_ratio = 0.1f };
  Node().GetRenderable().SetLodPolicy(dp);

  // Place camera at the world-sphere center to get distance ~ 0
  const auto center = glm::vec3(WorldMatrix()[3]); // translation (0.2)
  ConfigureView(center, /*viewport_height*/ 720.0f, /*m11*/ 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: choose the finest LOD 0
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 0u);
}

//! DistancePolicy chooses coarser LOD when camera is far from the object.
NOLINT_TEST_F(MeshResolverTest, DistancePolicy_Far_SelectsCoarseLOD)
{
  // Arrange
  const auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  const DistancePolicy dp { .thresholds = { 2.0f, 10.0f },
    .hysteresis_ratio = 0.1f };
  Node().GetRenderable().SetLodPolicy(dp);

  // Far camera to make normalized distance >> thresholds
  const auto center = glm::vec3(WorldMatrix()[3]);
  ConfigureView(center + glm::vec3(100.0f, 0.0f, 0.0f), 720.0f, 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: choose coarsest LOD 2
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 2u);
}

//=== Screen-space error policy: selection via sse = f * r / z ========--=====//

//! ScreenSpaceErrorPolicy selects finer LOD when SSE indicates high error.
NOLINT_TEST_F(MeshResolverTest, ScreenSpaceErrorPolicy_NearHighSSE_SelectsFine)
{
  // Arrange
  const auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  const ScreenSpaceErrorPolicy sp { .enter_finer_sse = { 50.0f, 25.0f },
    .exit_coarser_sse = { 40.0f, 20.0f } };
  Node().GetRenderable().SetLodPolicy(sp);

  // Camera ~ at center -> z ~= 0 -> clamped to 1e-6 -> very large SSE
  const auto center = glm::vec3(WorldMatrix()[3]);
  ConfigureView(center, /*viewport_height*/ 1000.0f, /*m11*/ 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: select finest LOD 0
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 0u);
}

//! ScreenSpaceErrorPolicy selects coarser LOD when SSE is low (far camera).
NOLINT_TEST_F(MeshResolverTest, ScreenSpaceErrorPolicy_FarLowSSE_SelectsCoarse)
{
  // Arrange
  const auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  const ScreenSpaceErrorPolicy sp { .enter_finer_sse = { 50.0f, 25.0f },
    .exit_coarser_sse = { 40.0f, 20.0f } };
  Node().GetRenderable().SetLodPolicy(sp);

  // Far camera -> small sse -> coarser LOD
  const auto center = glm::vec3(WorldMatrix()[3]);
  ConfigureView(center + glm::vec3(100.0f, 0.0f, 0.0f), /*height*/ 1000.0f,
    /*m11*/ 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: coarsest LOD 2
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 2u);
}

//! If focal length cannot be computed (viewport height zero) SSE is skipped.
/*!
 When viewport height is zero, focal length computation yields zero and the
 SSE-based selection must be skipped. The resolver should fall back to the
 default LOD (index 0).
*/
NOLINT_TEST_F(MeshResolverTest, ScreenSpaceErrorPolicy_NoFocal_FallbackLOD0)
{
  // Arrange: SSE policy but zero viewport height => focal length 0 => skip SSE
  const auto geom = MakeGeometryWithLods(3, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  const ScreenSpaceErrorPolicy sp { .enter_finer_sse = { 10.0f, 5.0f },
    .exit_coarser_sse = { 8.0f, 4.0f } };
  Node().GetRenderable().SetLodPolicy(sp);
  const auto center = glm::vec3(WorldMatrix()[3]);
  ConfigureView(center + glm::vec3(10.0f, 0.0f, 0.0f), /*height*/ 0.0f,
    /*m11*/ 1.0f);

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: no SSE selection performed -> default/fallback LOD 0
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 0u);
}

//=== Negative: Fixed policy index beyond LOD count clamps to last =========//

//! Verifies that FixedPolicy index beyond available LODs clamps to the last
//! available LOD.
NOLINT_TEST_F(MeshResolverTest, FixedPolicy_IndexBeyondRange_ClampsToLast)
{
  // Arrange: geometry with 2 LODs but request LOD 10
  const auto geom = MakeGeometryWithLods(2, { -1, -1, -1 }, { 1, 1, 1 });
  SetGeometry(geom);
  SeedVisibilityAndTransform();
  Node().GetRenderable().SetLodPolicy(FixedPolicy { 10 });

  // Act
  MeshResolver(Context(), State(), Proto());

  // Assert: clamped to last LOD (index 1)
  EXPECT_EQ(Proto().ResolvedMeshIndex(), 1u);
  EXPECT_TRUE(static_cast<bool>(Proto().ResolvedMesh()));
}

} // namespace

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "./SceneNode_test.h"
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/MeshType.h>
#include <Oxygen/Data/PakFormat_geometry.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/ActiveMesh.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::scene::testing::SceneNodeTestBase;

namespace {

//------------------------------------------------------------------------------
// Geometry/Renderable Component Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneNode geometry/renderable component scenarios.
class SceneNodeGeometryTest : public SceneNodeTestBase {
protected:
  static auto MakeSingleLodGeometry(std::shared_ptr<oxygen::data::Mesh> mesh)
    -> std::shared_ptr<const oxygen::data::GeometryAsset>
  {
    using oxygen::data::AssetKey;
    using oxygen::data::GeometryAsset;
    using oxygen::data::pak::geometry::GeometryAssetDesc;

    GeometryAssetDesc desc {};
    desc.lod_count = 1;
    // Bounding boxes left default for these tests; not asserted here.

    std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
    lods.push_back(std::move(mesh));
    return std::make_shared<GeometryAsset>(AssetKey {}, desc, std::move(lods));
  }

  static auto MakeTwoLodGeometry(std::shared_ptr<oxygen::data::Mesh> lod0,
    std::shared_ptr<oxygen::data::Mesh> lod1)
    -> std::shared_ptr<const oxygen::data::GeometryAsset>
  {
    using oxygen::data::AssetKey;
    using oxygen::data::GeometryAsset;
    using oxygen::data::pak::geometry::GeometryAssetDesc;

    GeometryAssetDesc desc {};
    desc.lod_count = 2;

    std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
    lods.push_back(std::move(lod0));
    lods.push_back(std::move(lod1));
    return std::make_shared<GeometryAsset>(AssetKey {}, desc, std::move(lods));
  }
};

/*! Test that attaching a geometry asset works as expected.
  Scenario: Attach a geometry asset and verify it is present. */
NOLINT_TEST_F(SceneNodeGeometryTest, AttachGeometry_AttachesGeometryAsset)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  EXPECT_FALSE(r.HasGeometry());

  // Act
  r.SetGeometry(geometry);

  // Assert
  EXPECT_TRUE(r.HasGeometry());
  auto mesh_ref = r.GetGeometry();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, geometry);
}

/*! Geometry attached to a node whose transform is already resolved takes that
  transform: culling and bounds use the scaled extent, not a unit mesh at the
  origin. The editor attaches geometry after the node's transform is set, so a
  scaled floor would otherwise be culled whenever the world origin leaves the
  view. */
NOLINT_TEST_F(SceneNodeGeometryTest, AttachGeometry_TakesTheResolvedTransform)
{
  // Arrange
  auto node = scene_->CreateNode("Floor");
  ASSERT_TRUE(node.GetTransform().SetLocalScale({ 2566.8F, 1566.0F, 1.0F }));
  scene_->Update();
  auto geometry
    = MakeSingleLodGeometry(oxygen::data::GenerateMesh("Plane/Mesh1", {}));

  // Act
  node.GetRenderable().SetGeometry(geometry);

  // Assert
  const auto bounds = node.GetRenderable().GetWorldSubMeshBoundingBox(0);
  if (!bounds.has_value()) {
    ADD_FAILURE() << "the renderable has no world bounds";
    return;
  }
  const auto& [bounds_min, bounds_max] = *bounds;
  EXPECT_NEAR(bounds_min.x, -1283.4F, 0.01F);
  EXPECT_NEAR(bounds_max.x, 1283.4F, 0.01F);
  EXPECT_NEAR(bounds_min.y, -783.0F, 0.01F);
  EXPECT_NEAR(bounds_max.y, 783.0F, 0.01F);
  EXPECT_GE(node.GetRenderable().GetWorldBoundingSphere().w,
    glm::length(oxygen::Vec3 { 1283.4F, 783.0F, 0.0F }));
}

/*! Test that attaching a geometry asset fails if one already exists.
  Scenario: Attach a second geometry asset and verify the first remains. */
NOLINT_TEST_F(
  SceneNodeGeometryTest, AttachGeometry_FailsIfGeometryAlreadyExists)
{
  using oxygen::data::Mesh;

  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  std::shared_ptr<Mesh> mesh1 = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  std::shared_ptr<Mesh> mesh2 = oxygen::data::GenerateMesh("Plane/Mesh1", {});
  auto geometry1 = MakeSingleLodGeometry(mesh1);
  auto geometry2 = MakeSingleLodGeometry(mesh2);
  r.SetGeometry(geometry1);
  EXPECT_TRUE(r.HasGeometry());

  // Act
  r.SetGeometry(geometry2);

  // Assert
  auto mesh_ref = r.GetGeometry();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, geometry2);
}

/*! Test detaching geometry from a SceneNode.
  Scenario: Remove geometry and verify state. */
NOLINT_TEST_F(SceneNodeGeometryTest, DetachGeometry_RemovesRenderableComponent)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  r.SetGeometry(geometry);
  EXPECT_TRUE(r.HasGeometry());

  // Act
  const bool detached = r.Detach();

  // Assert
  EXPECT_TRUE(detached);
  EXPECT_FALSE(r.HasGeometry());
  EXPECT_FALSE(r.GetGeometry());
}

/*! Test that detaching geometry when none is attached returns false.
  Scenario: Detach geometry from node with no geometry. */
NOLINT_TEST_F(SceneNodeGeometryTest, DetachGeometry_NoGeometry_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  EXPECT_FALSE(r.HasGeometry());

  // Act
  const bool detached = r.Detach();

  // Assert
  EXPECT_FALSE(detached);
}

/*! Test replacing an existing geometry asset with a new one.
  Scenario: Replace geometry and verify new geometry is present. */
NOLINT_TEST_F(SceneNodeGeometryTest, ReplaceGeometry_ReplacesExistingGeometry)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  std::shared_ptr<oxygen::data::Mesh> mesh1
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  std::shared_ptr<oxygen::data::Mesh> mesh2
    = oxygen::data::GenerateMesh("Plane/Mesh1", {});
  auto geometry1 = MakeSingleLodGeometry(mesh1);
  auto geometry2 = MakeSingleLodGeometry(mesh2);
  r.SetGeometry(geometry1);
  EXPECT_TRUE(r.HasGeometry());

  // Act
  r.SetGeometry(geometry2);

  // Assert
  EXPECT_TRUE(r.HasGeometry());
  auto mesh_ref = r.GetGeometry();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, geometry2);
}

/*! Test that replacing geometry when none is attached acts as attach.
  Scenario: Replace geometry on node with no geometry. */
NOLINT_TEST_F(SceneNodeGeometryTest, ReplaceGeometry_NoGeometry_ActsLikeAttach)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  EXPECT_FALSE(r.HasGeometry());

  // Act
  r.SetGeometry(geometry);

  // Assert
  EXPECT_TRUE(r.HasGeometry());
  auto mesh_ref = r.GetGeometry();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, geometry);
}

//! Test that GetGeometry returns nullptr if no geometry asset is attached.
NOLINT_TEST_F(SceneNodeGeometryTest, GetGeometry_ReturnsNullIfNoGeometry)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  EXPECT_FALSE(r.HasGeometry());

  // Act & Assert
  EXPECT_FALSE(r.GetGeometry());
}

//! Test that HasGeometry returns true if a geometry asset is attached.
NOLINT_TEST_F(SceneNodeGeometryTest, HasGeometry_ReturnsTrueIfGeometryAttached)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  EXPECT_FALSE(r.HasGeometry());

  // Act
  r.SetGeometry(geometry);

  // Assert
  EXPECT_TRUE(r.HasGeometry());
}

//! Test that attaching a nullptr geometry asset returns false.
NOLINT_TEST_F(SceneNodeGeometryTest, AttachGeometry_Nullptr_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  std::shared_ptr<const oxygen::data::GeometryAsset> null_geometry;

  // Act: SetGeometry should no-op on null; assert component remains absent
  r.SetGeometry(null_geometry);
  EXPECT_FALSE(r.HasGeometry());
}

//! Test that replacing with nullptr returns false and keeps existing geometry.
NOLINT_TEST_F(
  SceneNodeGeometryTest, ReplaceGeometry_Nullptr_ReturnsFalse_KeepsExisting)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto r = node.GetRenderable();
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  r.SetGeometry(geometry);

  // Act
  std::shared_ptr<const oxygen::data::GeometryAsset> null_geometry;
  r.SetGeometry(null_geometry);

  // Assert
  EXPECT_TRUE(r.HasGeometry());
  EXPECT_EQ(r.GetGeometry(), geometry);
}

//! Test that GetActiveMesh returns empty when no geometry is attached.
NOLINT_TEST_F(SceneNodeGeometryTest, GetActiveMesh_NoGeometry_ReturnsEmpty)
{
  auto node = scene_->CreateNode("Node");
  auto r = node.GetRenderable();
  EXPECT_FALSE(r.HasGeometry());
  EXPECT_FALSE(r.GetActiveMesh());
}

//! Test that GetActiveMesh returns LOD 0 mesh for single-LOD geometry.
NOLINT_TEST_F(
  SceneNodeGeometryTest, GetActiveMesh_SingleLodGeometry_ReturnsLod0)
{
  // Arrange
  auto node = scene_->CreateNode("Node");
  auto r = node.GetRenderable();
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  r.SetGeometry(geometry);

  // Act
  auto active_opt = r.GetActiveMesh();

  // Assert
  if (!active_opt.has_value()) {
    ADD_FAILURE() << "the renderable has no active mesh";
    return;
  }
  const auto& active = *active_opt;
  EXPECT_EQ(active.lod, 0U);
  EXPECT_EQ(active.mesh, geometry->MeshAt(0));
}

//! Test that with two LODs, default policy selects LOD 0.
NOLINT_TEST_F(SceneNodeGeometryTest, GetActiveMesh_TwoLods_DefaultsToLod0)
{
  // Arrange
  auto node = scene_->CreateNode("Node");
  auto r = node.GetRenderable();
  std::shared_ptr<oxygen::data::Mesh> lod0
    = oxygen::data::GenerateMesh("Cube/LOD0", {});
  std::shared_ptr<oxygen::data::Mesh> lod1
    = oxygen::data::GenerateMesh("Cube/LOD1", {});
  auto geometry = MakeTwoLodGeometry(lod0, lod1);
  r.SetGeometry(geometry);

  // Act
  auto active_opt = r.GetActiveMesh();

  // Assert
  if (!active_opt.has_value()) {
    ADD_FAILURE() << "the renderable has no active mesh";
    return;
  }
  const auto& active = *active_opt;
  EXPECT_EQ(active.lod, 0U);
  EXPECT_EQ(active.mesh, geometry->MeshAt(0));
}

NOLINT_TEST_F(SceneNodeGeometryTest,
  WorldBoundingSphere_FallsBackToAssetBoundsWhenMeshSphereIsZero)
{
  using oxygen::data::AssetKey;
  using oxygen::data::GeometryAsset;
  using oxygen::data::MaterialAsset;
  using oxygen::data::MeshBuilder;
  using oxygen::data::Vertex;
  using oxygen::data::pak::geometry::GeometryAssetDesc;

  auto node = scene_->CreateNode("Node");
  auto renderable = node.GetRenderable();

  const auto make_vertex = [](const glm::vec3 position) -> Vertex {
    return Vertex {
      .position = position,
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {},
    };
  };
  std::vector<Vertex> vertices {
    make_vertex({ -1.0F, -1.0F, -1.0F }),
    make_vertex({ 1.0F, -1.0F, -1.0F }),
    make_vertex({ 1.0F, 1.0F, -1.0F }),
    make_vertex({ -1.0F, 1.0F, -1.0F }),
    make_vertex({ -1.0F, -1.0F, 1.0F }),
    make_vertex({ 1.0F, -1.0F, 1.0F }),
    make_vertex({ 1.0F, 1.0F, 1.0F }),
    make_vertex({ -1.0F, 1.0F, 1.0F }),
  };
  std::vector<std::uint32_t> indices { 0, 1, 2, 2, 3, 0 };

  // A standard descriptor with zero authored mesh bounds exercises the
  // asset-bound fallback. Descriptor-free meshes derive bounds from vertices.
  oxygen::data::pak::geometry::MeshDesc mesh_desc {};
  mesh_desc.mesh_type
    = static_cast<std::uint8_t>(oxygen::data::MeshType::kStandard);
  MeshBuilder builder;
  builder.WithDescriptor(mesh_desc)
    .WithVertices(vertices)
    .WithIndices(indices)
    .BeginSubMesh("Cube", MaterialAsset::CreateDefault())
    .WithMeshView({
      .first_index = 0,
      .index_count = static_cast<std::uint32_t>(indices.size()),
      .first_vertex = 0,
      .vertex_count = static_cast<std::uint32_t>(vertices.size()),
    })
    .EndSubMesh();
  auto mesh = builder.Build();
  ASSERT_NE(mesh, nullptr);
  EXPECT_FLOAT_EQ(mesh->BoundingSphere().w, 0.0F);

  GeometryAssetDesc desc {};
  desc.lod_count = 1;
  desc.bounding_box_min[0] = -1.0F;
  desc.bounding_box_min[1] = -1.0F;
  desc.bounding_box_min[2] = -1.0F;
  desc.bounding_box_max[0] = 1.0F;
  desc.bounding_box_max[1] = 1.0F;
  desc.bounding_box_max[2] = 1.0F;

  std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
  lods.push_back(std::shared_ptr<oxygen::data::Mesh>(std::move(mesh)));
  auto geometry
    = std::make_shared<GeometryAsset>(AssetKey {}, desc, std::move(lods));

  renderable.SetGeometry(geometry);

  const auto sphere = renderable.GetWorldBoundingSphere();
  EXPECT_GT(sphere.w, 0.0F);
  EXPECT_NEAR(sphere.x, 0.0F, 1e-5F);
  EXPECT_NEAR(sphere.y, 0.0F, 1e-5F);
  EXPECT_NEAR(sphere.z, 0.0F, 1e-5F);
  EXPECT_NEAR(sphere.w, std::sqrt(3.0F), 1e-5F);
}

} // namespace

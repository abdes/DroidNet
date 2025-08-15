//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./SceneNode_test.h"

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/Types/ActiveMesh.h>

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
    using oxygen::data::GeometryAsset;
    using oxygen::data::pak::GeometryAssetDesc;

    GeometryAssetDesc desc {};
    desc.lod_count = 1;
    // Bounding boxes left default for these tests; not asserted here.

    std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
    lods.push_back(std::move(mesh));
    return std::make_shared<GeometryAsset>(std::move(desc), std::move(lods));
  }

  static auto MakeTwoLodGeometry(std::shared_ptr<oxygen::data::Mesh> lod0,
    std::shared_ptr<oxygen::data::Mesh> lod1)
    -> std::shared_ptr<const oxygen::data::GeometryAsset>
  {
    using oxygen::data::GeometryAsset;
    using oxygen::data::pak::GeometryAssetDesc;

    GeometryAssetDesc desc {};
    desc.lod_count = 2;

    std::vector<std::shared_ptr<oxygen::data::Mesh>> lods;
    lods.push_back(std::move(lod0));
    lods.push_back(std::move(lod1));
    return std::make_shared<GeometryAsset>(std::move(desc), std::move(lods));
  }
};

/*! Test that attaching a geometry asset works as expected.
  Scenario: Attach a geometry asset and verify it is present. */
NOLINT_TEST_F(SceneNodeGeometryTest, AttachGeometry_AttachesGeometryAsset)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  EXPECT_FALSE(node.HasGeometry());

  // Act
  const bool attached = node.AttachGeometry(geometry);

  // Assert
  EXPECT_TRUE(attached);
  EXPECT_TRUE(node.HasGeometry());
  auto mesh_ref = node.GetGeometry();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, geometry);
}

/*! Test that attaching a geometry asset fails if one already exists.
  Scenario: Attach a second geometry asset and verify the first remains. */
NOLINT_TEST_F(
  SceneNodeGeometryTest, AttachGeometry_FailsIfGeometryAlreadyExists)
{
  using oxygen::data::Mesh;

  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  std::shared_ptr<Mesh> mesh1 = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  std::shared_ptr<Mesh> mesh2 = oxygen::data::GenerateMesh("Plane/Mesh1", {});
  auto geometry1 = MakeSingleLodGeometry(mesh1);
  auto geometry2 = MakeSingleLodGeometry(mesh2);
  EXPECT_TRUE(node.AttachGeometry(geometry1));
  EXPECT_TRUE(node.HasGeometry());

  // Act
  const bool attached = node.AttachGeometry(geometry2);

  // Assert
  EXPECT_FALSE(attached);
  auto mesh_ref = node.GetGeometry();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, geometry1);
}

/*! Test detaching geometry from a SceneNode.
  Scenario: Remove geometry and verify state. */
NOLINT_TEST_F(SceneNodeGeometryTest, DetachGeometry_RemovesRenderableComponent)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  EXPECT_TRUE(node.AttachGeometry(geometry));
  EXPECT_TRUE(node.HasGeometry());

  // Act
  const bool detached = node.DetachRenderable();

  // Assert
  EXPECT_TRUE(detached);
  EXPECT_FALSE(node.HasGeometry());
  EXPECT_FALSE(node.GetGeometry());
}

/*! Test that detaching geometry when none is attached returns false.
  Scenario: Detach geometry from node with no geometry. */
NOLINT_TEST_F(SceneNodeGeometryTest, DetachGeometry_NoGeometry_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  EXPECT_FALSE(node.HasGeometry());

  // Act
  const bool detached = node.DetachRenderable();

  // Assert
  EXPECT_FALSE(detached);
}

/*! Test replacing an existing geometry asset with a new one.
  Scenario: Replace geometry and verify new geometry is present. */
NOLINT_TEST_F(SceneNodeGeometryTest, ReplaceGeometry_ReplacesExistingGeometry)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  std::shared_ptr<oxygen::data::Mesh> mesh1
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  std::shared_ptr<oxygen::data::Mesh> mesh2
    = oxygen::data::GenerateMesh("Plane/Mesh1", {});
  auto geometry1 = MakeSingleLodGeometry(mesh1);
  auto geometry2 = MakeSingleLodGeometry(mesh2);
  EXPECT_TRUE(node.AttachGeometry(geometry1));
  EXPECT_TRUE(node.HasGeometry());

  // Act
  const bool replaced = node.ReplaceGeometry(geometry2);

  // Assert
  EXPECT_TRUE(replaced);
  EXPECT_TRUE(node.HasGeometry());
  auto mesh_ref = node.GetGeometry();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, geometry2);
}

/*! Test that replacing geometry when none is attached acts as attach.
  Scenario: Replace geometry on node with no geometry. */
NOLINT_TEST_F(SceneNodeGeometryTest, ReplaceGeometry_NoGeometry_ActsLikeAttach)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  EXPECT_FALSE(node.HasGeometry());

  // Act
  const bool replaced = node.ReplaceGeometry(geometry);

  // Assert
  EXPECT_TRUE(replaced);
  EXPECT_TRUE(node.HasGeometry());
  auto mesh_ref = node.GetGeometry();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, geometry);
}

//! Test that GetGeometry returns nullptr if no geometry asset is attached.
NOLINT_TEST_F(SceneNodeGeometryTest, GetGeometry_ReturnsNullIfNoGeometry)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  EXPECT_FALSE(node.HasGeometry());

  // Act & Assert
  EXPECT_FALSE(node.GetGeometry());
}

//! Test that HasGeometry returns true if a geometry asset is attached.
NOLINT_TEST_F(SceneNodeGeometryTest, HasGeometry_ReturnsTrueIfGeometryAttached)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  EXPECT_FALSE(node.HasGeometry());

  // Act
  node.AttachGeometry(geometry);

  // Assert
  EXPECT_TRUE(node.HasGeometry());
}

//! Test that attaching a nullptr geometry asset returns false.
NOLINT_TEST_F(SceneNodeGeometryTest, AttachGeometry_Nullptr_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  std::shared_ptr<const oxygen::data::GeometryAsset> null_geometry;

  // Act & Assert
  EXPECT_FALSE(node.AttachGeometry(null_geometry));
}

//! Test that replacing with nullptr returns false and keeps existing geometry.
NOLINT_TEST_F(
  SceneNodeGeometryTest, ReplaceGeometry_Nullptr_ReturnsFalse_KeepsExisting)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  ASSERT_TRUE(node.AttachGeometry(geometry));

  // Act
  std::shared_ptr<const oxygen::data::GeometryAsset> null_geometry;
  const bool replaced = node.ReplaceGeometry(null_geometry);

  // Assert
  EXPECT_FALSE(replaced);
  EXPECT_TRUE(node.HasGeometry());
  EXPECT_EQ(node.GetGeometry(), geometry);
}

//! Test that GetActiveMesh returns empty when no geometry is attached.
NOLINT_TEST_F(SceneNodeGeometryTest, GetActiveMesh_NoGeometry_ReturnsEmpty)
{
  auto node = scene_->CreateNode("Node");
  EXPECT_FALSE(node.HasGeometry());
  EXPECT_FALSE(node.GetActiveMesh());
}

//! Test that GetActiveMesh returns LOD 0 mesh for single-LOD geometry.
NOLINT_TEST_F(
  SceneNodeGeometryTest, GetActiveMesh_SingleLodGeometry_ReturnsLod0)
{
  // Arrange
  auto node = scene_->CreateNode("Node");
  std::shared_ptr<oxygen::data::Mesh> mesh
    = oxygen::data::GenerateMesh("Cube/Mesh1", {});
  auto geometry = MakeSingleLodGeometry(mesh);
  ASSERT_TRUE(node.AttachGeometry(geometry));

  // Act
  auto active_opt = node.GetActiveMesh();

  // Assert
  ASSERT_TRUE(active_opt.has_value());
  EXPECT_EQ(active_opt->lod, 0u);
  EXPECT_EQ(active_opt->mesh, geometry->MeshAt(0));
}

//! Test that with two LODs, default policy selects LOD 0.
NOLINT_TEST_F(SceneNodeGeometryTest, GetActiveMesh_TwoLods_DefaultsToLod0)
{
  // Arrange
  auto node = scene_->CreateNode("Node");
  std::shared_ptr<oxygen::data::Mesh> lod0
    = oxygen::data::GenerateMesh("Cube/LOD0", {});
  std::shared_ptr<oxygen::data::Mesh> lod1
    = oxygen::data::GenerateMesh("Cube/LOD1", {});
  auto geometry = MakeTwoLodGeometry(lod0, lod1);
  ASSERT_TRUE(node.AttachGeometry(geometry));

  // Act
  auto active_opt = node.GetActiveMesh();

  // Assert
  ASSERT_TRUE(active_opt.has_value());
  EXPECT_EQ(active_opt->lod, 0u);
  EXPECT_EQ(active_opt->mesh, geometry->MeshAt(0));
}

} // namespace

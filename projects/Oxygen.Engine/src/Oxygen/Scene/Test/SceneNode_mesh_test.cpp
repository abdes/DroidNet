//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./SceneNode_test.h"

#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Scene/SceneNode.h>

using oxygen::scene::testing::SceneNodeTestBase;

namespace {

//------------------------------------------------------------------------------
// Mesh Component Tests
//------------------------------------------------------------------------------

//! Test fixture for SceneNode mesh component scenarios.
class SceneNodeMeshTest : public SceneNodeTestBase { };

/*! Test that attaching a mesh asset works as expected.
    Scenario: Attach a mesh and verify it is present. */
NOLINT_TEST_F(SceneNodeMeshTest, AttachMesh_AttachesMeshAsset)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto mesh = oxygen::data::MakeCubeMeshAsset();
  EXPECT_FALSE(node.HasMesh());

  // Act
  const bool attached = node.AttachMesh(mesh);

  // Assert
  EXPECT_TRUE(attached);
  EXPECT_TRUE(node.HasMesh());
  auto mesh_ref = node.GetMesh();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, mesh);
}

/*! Test that attaching a mesh fails if one already exists.
    Scenario: Attach a second mesh and verify the first remains. */
NOLINT_TEST_F(SceneNodeMeshTest, AttachMesh_FailsIfMeshAlreadyExists)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto mesh1 = oxygen::data::MakeCubeMeshAsset();
  auto mesh2 = oxygen::data::MakePlaneMeshAsset();
  EXPECT_TRUE(node.AttachMesh(mesh1));
  EXPECT_TRUE(node.HasMesh());

  // Act
  const bool attached = node.AttachMesh(mesh2);

  // Assert
  EXPECT_FALSE(attached);
  auto mesh_ref = node.GetMesh();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, mesh1);
}

/*! Test detaching a mesh from a SceneNode.
    Scenario: Remove mesh and verify state. */
NOLINT_TEST_F(SceneNodeMeshTest, DetachMesh_RemovesMeshComponent)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto mesh = oxygen::data::MakeCubeMeshAsset();
  EXPECT_TRUE(node.AttachMesh(mesh));
  EXPECT_TRUE(node.HasMesh());

  // Act
  const bool detached = node.DetachMesh();

  // Assert
  EXPECT_TRUE(detached);
  EXPECT_FALSE(node.HasMesh());
  EXPECT_FALSE(node.GetMesh());
}

/*! Test that detaching a mesh when none is attached returns false.
    Scenario: Detach mesh from node with no mesh. */
NOLINT_TEST_F(SceneNodeMeshTest, DetachMesh_NoMesh_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  EXPECT_FALSE(node.HasMesh());

  // Act
  const bool detached = node.DetachMesh();

  // Assert
  EXPECT_FALSE(detached);
}

/*! Test replacing an existing mesh with a new one.
    Scenario: Replace mesh and verify new mesh is present. */
NOLINT_TEST_F(SceneNodeMeshTest, ReplaceMesh_ReplacesExistingMesh)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto mesh1 = oxygen::data::MakeCubeMeshAsset();
  auto mesh2 = oxygen::data::MakePlaneMeshAsset();
  EXPECT_TRUE(node.AttachMesh(mesh1));
  EXPECT_TRUE(node.HasMesh());

  // Act
  const bool replaced = node.ReplaceMesh(mesh2);

  // Assert
  EXPECT_TRUE(replaced);
  EXPECT_TRUE(node.HasMesh());
  auto mesh_ref = node.GetMesh();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, mesh2);
}

/*! Test that replacing a mesh when none is attached acts as attach.
    Scenario: Replace mesh on node with no mesh. */
NOLINT_TEST_F(SceneNodeMeshTest, ReplaceMesh_NoMesh_ActsLikeAttach)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto mesh = oxygen::data::MakeCubeMeshAsset();
  EXPECT_FALSE(node.HasMesh());

  // Act
  const bool replaced = node.ReplaceMesh(mesh);

  // Assert
  EXPECT_TRUE(replaced);
  EXPECT_TRUE(node.HasMesh());
  auto mesh_ref = node.GetMesh();
  ASSERT_TRUE(mesh_ref);
  EXPECT_EQ(mesh_ref, mesh);
}

//! Test that GetMesh returns nullptr if no mesh is attached.
NOLINT_TEST_F(SceneNodeMeshTest, GetMesh_ReturnsNullIfNoMesh)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  EXPECT_FALSE(node.HasMesh());

  // Act & Assert
  EXPECT_FALSE(node.GetMesh());
}

//! Test that HasMesh returns true if a mesh is attached.
NOLINT_TEST_F(SceneNodeMeshTest, HasMesh_ReturnsTrueIfMeshAttached)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  auto mesh = oxygen::data::MakeCubeMeshAsset();
  EXPECT_FALSE(node.HasMesh());

  // Act
  node.AttachMesh(mesh);

  // Assert
  EXPECT_TRUE(node.HasMesh());
}

//! Test that attaching a nullptr mesh returns false.
NOLINT_TEST_F(SceneNodeMeshTest, AttachMesh_Nullptr_ReturnsFalse)
{
  // Arrange
  auto node = scene_->CreateNode("MeshNode");
  std::shared_ptr<const oxygen::data::MeshAsset> null_mesh;

  // Act & Assert
  EXPECT_FALSE(node.AttachMesh(null_mesh));
}

} // namespace

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/Vertex.h>

using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;
using oxygen::data::MeshView;
using oxygen::data::SubMesh;
using oxygen::data::Vertex;

namespace {

using ::testing::SizeIs;

//=== Test Fixtures ===-------------------------------------------------------//

// MeshAssetBasicTest: immutability, bounding box correctness, shared ownership
class MeshAssetBasicTest : public testing::Test {
protected:
  // Helper to create a simple mesh asset
  static auto MakeSimpleMesh() -> std::shared_ptr<oxygen::data::Mesh>
  {
    std::vector<Vertex> vertices = {
      {
        .position = { 0, 0, 0 },
        .normal = { 0, 1, 0 },
        .texcoord = { 0, 0 },
        .tangent = { 1, 0, 0 },
        .bitangent = {},
        .color = {},
      },
      {
        .position = { 1, 0, 0 },
        .normal = { 0, 1, 0 },
        .texcoord = { 1, 0 },
        .tangent = { 1, 0, 0 },
        .bitangent = {},
        .color = {},
      },
      {
        .position = { 0, 1, 0 },
        .normal = { 0, 1, 0 },
        .texcoord = { 0, 1 },
        .tangent = { 1, 0, 0 },
        .bitangent = {},
        .color = {},
      },
    };
    std::vector<std::uint32_t> indices = { 0, 1, 2 };
    return std::make_shared<oxygen::data::Mesh>("triangle", vertices, indices);
  }
};

//! Checks that Mesh is immutable after construction.
NOLINT_TEST_F(MeshAssetBasicTest, Immutability)
{
  // Arrange
  const auto mesh = MakeSimpleMesh();
  const auto vertices_before = mesh->Vertices();
  const auto indices_before = mesh->Indices();

  // Act
  // Try to mutate the returned spans (should not compile if attempted)
  // mesh->Vertices()[0].position.x = 42.0f; // Uncommenting this should fail to
  // compile

  // Assert
  EXPECT_EQ(vertices_before.size(), 3);
  EXPECT_EQ(indices_before.size(), 3);
}

//! Checks that Mesh computes correct bounding box.
NOLINT_TEST_F(MeshAssetBasicTest, BoundingBoxCorrectness)
{
  // Arrange
  const auto mesh = MakeSimpleMesh();

  // Act
  const auto& min = mesh->BoundingBoxMin();
  const auto& max = mesh->BoundingBoxMax();

  // Assert
  EXPECT_EQ(min, glm::vec3(0, 0, 0));
  EXPECT_EQ(max, glm::vec3(1, 1, 0));
}

//! Checks that Mesh is safely shareable via shared_ptr.
NOLINT_TEST_F(MeshAssetBasicTest, SharedOwnership)
{
  // Arrange
  auto mesh = MakeSimpleMesh();
  const std::shared_ptr<oxygen::data::Mesh> mesh2 = mesh;

  // Act
  mesh.reset();

  // Assert
  EXPECT_NE(mesh2, nullptr);
  EXPECT_EQ(mesh2->VertexCount(), 3);
}

//! Checks that Mesh constructor rejects empty vertex or index arrays
//! (death test).
NOLINT_TEST_F(MeshAssetBasicTest, ConstructorRejectsEmpty)
{
  // Arrange
  std::vector<Vertex> empty_vertices;
  std::vector<std::uint32_t> empty_indices;
  std::vector<Vertex> one_vertex = { { .position = { 0, 0, 0 },
    .normal = { 0, 1, 0 },
    .texcoord = { 0, 0 },
    .tangent = { 1, 0, 0 },
    .bitangent = {},
    .color = {} } };
  std::vector<std::uint32_t> one_index = { 0 };

  // Act & Assert
  EXPECT_DEATH([[maybe_unused]] auto _
    = std::make_shared<oxygen::data::Mesh>("fail1", empty_vertices, one_index),
    "");
  EXPECT_DEATH([[maybe_unused]] auto _
    = std::make_shared<oxygen::data::Mesh>("fail2", one_vertex, empty_indices),
    "");
  EXPECT_DEATH([[maybe_unused]] auto _ = std::make_shared<oxygen::data::Mesh>(
                 "fail3", empty_vertices, empty_indices),
    "");
}

// MeshAssetViewTest: view validity, in-bounds checks
class MeshAssetViewTest : public testing::Test {
protected:
  static auto MakeSimpleMesh() -> std::shared_ptr<oxygen::data::Mesh>
  {
    std::vector<Vertex> vertices = {
      { .position = { 0, 0, 0 },
        .normal = { 0, 1, 0 },
        .texcoord = { 0, 0 },
        .tangent = { 1, 0, 0 },
        .bitangent = {},
        .color = {} },
      { .position = { 1, 0, 0 },
        .normal = { 0, 1, 0 },
        .texcoord = { 1, 0 },
        .tangent = { 1, 0, 0 },
        .bitangent = {},
        .color = {} },
      { .position = { 0, 1, 0 },
        .normal = { 0, 1, 0 },
        .texcoord = { 0, 1 },
        .tangent = { 1, 0, 0 },
        .bitangent = {},
        .color = {} },
    };
    std::vector<std::uint32_t> indices = { 0, 1, 2 };
    return std::make_shared<oxygen::data::Mesh>("triangle", vertices, indices);
  }
};

//! Checks that Mesh can create valid MeshView and that view is in-bounds.
NOLINT_TEST_F(MeshAssetViewTest, ViewValidity)
{
  // Arrange
  const auto mesh = MakeSimpleMesh();

  // Act
  auto mesh_view = mesh->MakeView(0, 3, 0, 3);

  // Assert
  EXPECT_THAT(mesh_view.Vertices(), SizeIs(3));
  EXPECT_THAT(mesh_view.Indices(), SizeIs(3));
  EXPECT_EQ(mesh_view.Vertices().data(), mesh->Vertices().data());
  EXPECT_EQ(mesh_view.Indices().data(), mesh->Indices().data());
}

//! Checks that Mesh rejects out-of-bounds view creation (death test).
NOLINT_TEST_F(MeshAssetViewTest, InBoundsChecks)
{
  // Arrange
  const auto mesh = MakeSimpleMesh();

  // Act & Assert
  EXPECT_DEATH([[maybe_unused]] auto _ = mesh->MakeView(0, 10, 0, 3), "");
  EXPECT_DEATH([[maybe_unused]] auto _ = mesh->MakeView(0, 3, 0, 10), "");
  EXPECT_DEATH([[maybe_unused]] auto _ = mesh->MakeView(5, 1, 0, 3), "");
  EXPECT_DEATH([[maybe_unused]] auto _ = mesh->MakeView(0, 3, 5, 1), "");
}

// MeshAssetSubMeshTest: submesh creation, validity, material association
class MeshAssetSubMeshTest : public testing::Test {
protected:
  static auto MakeSimpleMesh() -> std::shared_ptr<oxygen::data::Mesh>
  {
    std::vector<Vertex> vertices = {
      { .position = { 0, 0, 0 },
        .normal = { 0, 1, 0 },
        .texcoord = { 0, 0 },
        .tangent = { 1, 0, 0 },
        .bitangent = {},
        .color = {} },
      { .position = { 1, 0, 0 },
        .normal = { 0, 1, 0 },
        .texcoord = { 1, 0 },
        .tangent = { 1, 0, 0 },
        .bitangent = {},
        .color = {} },
      { .position = { 0, 1, 0 },
        .normal = { 0, 1, 0 },
        .texcoord = { 0, 1 },
        .tangent = { 1, 0, 0 },
        .bitangent = {},
        .color = {} },
    };
    std::vector<std::uint32_t> indices = { 0, 1, 2 };
    return std::make_shared<oxygen::data::Mesh>("triangle", vertices, indices);
  }
};

//! Checks that Mesh is invalid without submeshes and valid with submeshes.
NOLINT_TEST_F(MeshAssetSubMeshTest, ValidityWithoutSubmeshes)
{
  // Arrange
  const auto mesh = MakeSimpleMesh();

  // Act & Assert - mesh should be invalid without submeshes
  EXPECT_FALSE(mesh->IsValid());
  EXPECT_THAT(mesh->SubMeshes(), SizeIs(0));
}

//! Checks that Mesh becomes valid after adding a submesh.
NOLINT_TEST_F(MeshAssetSubMeshTest, ValidityWithSubmeshes)
{
  // Arrange
  const auto mesh = MakeSimpleMesh();

  // Create a MeshView for the entire mesh
  auto mesh_view
    = mesh->MakeView(0, mesh->VertexCount(), 0, mesh->IndexCount());
  std::vector<MeshView> mesh_views;
  mesh_views.push_back(mesh_view);

  // Act - Add a submesh with a valid material
  oxygen::data::pak::MaterialAssetDesc material_desc {};
  material_desc.material_domain = 1; // Example domain
  material_desc.flags = 2; // Example flags
  material_desc.shader_stages = 0; // No shader stages for test
  auto material = std::make_shared<const MaterialAsset>(
    material_desc, std::vector<oxygen::data::ShaderReference> {});
  mesh->AddSubMesh("main", std::move(mesh_views), material);

  // Assert - mesh should now be valid
  EXPECT_TRUE(mesh->IsValid());
  EXPECT_THAT(mesh->SubMeshes(), SizeIs(1));
  EXPECT_EQ(mesh->SubMeshes()[0].Name(), "main");
  EXPECT_THAT(mesh->SubMeshes()[0].MeshViews(), SizeIs(1));
}

} // namespace

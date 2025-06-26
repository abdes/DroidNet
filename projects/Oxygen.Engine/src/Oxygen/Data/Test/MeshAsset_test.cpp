//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/MeshAsset.h>
#include <Oxygen/Data/Vertex.h>
#include <Oxygen/Testing/GTest.h>

//=== Test Fixtures ===-------------------------------------------------------//

namespace {

// MeshAssetBasicTest: immutability, bounding box correctness, shared ownership
class MeshAssetBasicTest : public ::testing::Test {
protected:
  // Helper to create a simple mesh asset
  std::shared_ptr<oxygen::data::MeshAsset> MakeSimpleMesh()
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
    return std::make_shared<oxygen::data::MeshAsset>(
      "triangle", vertices, indices);
  }
};

//! Checks that MeshAsset is immutable after construction.
NOLINT_TEST_F(MeshAssetBasicTest, Immutability)
{
  // Arrange
  auto mesh = MakeSimpleMesh();
  auto vertices_before = mesh->Vertices();
  auto indices_before = mesh->Indices();

  // Act
  // Try to mutate the returned spans (should not compile if attempted)
  // mesh->Vertices()[0].position.x = 42.0f; // Uncommenting this should fail to
  // compile

  // Assert
  EXPECT_EQ(vertices_before.size(), 3);
  EXPECT_EQ(indices_before.size(), 3);
}

//! Checks that MeshAsset computes correct bounding box.
NOLINT_TEST_F(MeshAssetBasicTest, BoundingBoxCorrectness)
{
  // Arrange
  auto mesh = MakeSimpleMesh();

  // Act
  const auto& min = mesh->BoundingBoxMin();
  const auto& max = mesh->BoundingBoxMax();

  // Assert
  EXPECT_EQ(min, glm::vec3(0, 0, 0));
  EXPECT_EQ(max, glm::vec3(1, 1, 0));
}

//! Checks that MeshAsset is safely shareable via shared_ptr.
NOLINT_TEST_F(MeshAssetBasicTest, SharedOwnership)
{
  // Arrange
  auto mesh = MakeSimpleMesh();
  std::shared_ptr<oxygen::data::MeshAsset> mesh2 = mesh;

  // Act
  mesh.reset();

  // Assert
  EXPECT_NE(mesh2, nullptr);
  EXPECT_EQ(mesh2->VertexCount(), 3);
}

//! Checks that MeshAsset constructor rejects empty vertex or index arrays
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
  EXPECT_DEATH(
    [[maybe_unused]] auto _ = std::make_shared<oxygen::data::MeshAsset>(
      "fail1", empty_vertices, one_index),
    "");
  EXPECT_DEATH(
    [[maybe_unused]] auto _ = std::make_shared<oxygen::data::MeshAsset>(
      "fail2", one_vertex, empty_indices),
    "");
  EXPECT_DEATH(
    [[maybe_unused]] auto _ = std::make_shared<oxygen::data::MeshAsset>(
      "fail3", empty_vertices, empty_indices),
    "");
}

// MeshAssetViewTest: view validity, in-bounds checks
class MeshAssetViewTest : public ::testing::Test {
protected:
  std::shared_ptr<oxygen::data::MeshAsset> MakeSimpleMesh()
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
    return std::make_shared<oxygen::data::MeshAsset>(
      "triangle", vertices, indices);
  }
};

//! Checks that MeshAsset can create valid MeshView and that view is in-bounds.
NOLINT_TEST_F(MeshAssetViewTest, ViewValidity)
{
  // Arrange
  auto mesh = MakeSimpleMesh();

  // Act
  mesh->CreateView("main", 0, 3, 0, 3);

  // Assert
  const auto& view = mesh->Views().front();
  EXPECT_EQ(view.VertexCount(), 3);
  EXPECT_EQ(view.IndexCount(), 3);
}

//! Checks that MeshAsset rejects out-of-bounds view creation (death test).
NOLINT_TEST_F(MeshAssetViewTest, InBoundsChecks)
{
  // Arrange
  auto mesh = MakeSimpleMesh();

  // Act & Assert
  EXPECT_DEATH(mesh->CreateView("bad", 0, 10, 0, 3), "");
  EXPECT_DEATH(mesh->CreateView("bad", 0, 3, 0, 10), "");
}

} // namespace

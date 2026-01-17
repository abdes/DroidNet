//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Standard library
#include <memory>
#include <vector>

// GTest
#include <Oxygen/Testing/GTest.h>

// Project
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/Vertex.h>

using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;
using oxygen::data::MeshBuilder;
using oxygen::data::SubMesh;
using oxygen::data::Vertex;

namespace {

using ::testing::SizeIs;

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
    return MakeMeshFrom(vertices, indices);
  }

  // Helper to create a mesh from arbitrary vertices/indices
  static auto MakeMeshFrom(const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
    -> std::shared_ptr<oxygen::data::Mesh>
  {
    using oxygen::data::MeshType;

    auto material = MaterialAsset::CreateDefault();
    const glm::vec3 bounds_min { 0.0f, 0.0f, 0.0f };
    const glm::vec3 bounds_max { 1.0f, 1.0f, 0.0f };
    oxygen::data::pak::MeshDesc desc {};
    desc.mesh_type
      = static_cast<std::underlying_type_t<MeshType>>(MeshType::kStandard);
    desc.info.standard.bounding_box_min[0] = bounds_min.x;
    desc.info.standard.bounding_box_min[1] = bounds_min.y;
    desc.info.standard.bounding_box_min[2] = bounds_min.z;
    desc.info.standard.bounding_box_max[0] = bounds_max.x;
    desc.info.standard.bounding_box_max[1] = bounds_max.y;
    desc.info.standard.bounding_box_max[2] = bounds_max.z;

    return oxygen::data::MeshBuilder(0, "triangle")
      .WithVertices(vertices)
      .WithIndices(indices)
      .WithDescriptor(desc)
      .BeginSubMesh("main", material)
      .WithMeshView({
        .first_index = 0,
        .index_count = static_cast<uint32_t>(indices.size()),
        .first_vertex = 0,
        .vertex_count = static_cast<uint32_t>(vertices.size()),
      })
      .EndSubMesh()
      .Build();
  }
};

//! Checks that Mesh is immutable after construction.
NOLINT_TEST_F(MeshAssetBasicTest, Immutability)
{
  // Arrange
  const auto mesh = MakeSimpleMesh();
  const auto vertices_before = mesh->Vertices();
  const auto indices_before = mesh->IndexBuffer();

  // Act
  // Try to mutate the returned spans (should not compile if attempted)
  // mesh->Vertices()[0].position.x = 42.0f; // Uncommenting this should fail to
  // compile

  // Assert
  EXPECT_EQ(vertices_before.size(), 3);
  EXPECT_EQ(indices_before.Count(), 3u);
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
  std::vector<Vertex> one_vertex = { {
    .position = { 0, 0, 0 },
    .normal = { 0, 1, 0 },
    .texcoord = { 0, 0 },
    .tangent = { 1, 0, 0 },
    .bitangent = {},
    .color = {},
  } };
  std::vector<std::uint32_t> one_index = { 0 };

  // Act & Assert
  EXPECT_DEATH(
    [[maybe_unused]] auto _ = MakeMeshFrom(empty_vertices, one_index), "");
  EXPECT_DEATH(
    [[maybe_unused]] auto _ = MakeMeshFrom(one_vertex, empty_indices), "");
  EXPECT_DEATH(
    [[maybe_unused]] auto _ = MakeMeshFrom(empty_vertices, empty_indices), "");
}

// MeshAssetSubMeshTest: submesh creation, validity, material association
class MeshAssetSubMeshTest : public testing::Test {
protected:
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
    auto material = MaterialAsset::CreateDefault();
    return oxygen::data::MeshBuilder(0, "triangle")
      .WithVertices(vertices)
      .WithIndices(indices)
      .BeginSubMesh("main", material)
      .WithMeshView({
        .first_index = 0,
        .index_count = 3,
        .first_vertex = 0,
        .vertex_count = 3,
      })
      .EndSubMesh()
      .Build();
  }
};

//! (10) Verifies IsValid() reflects presence of at least one submesh.
NOLINT_TEST(MeshBasicTest, IsValidReflectsSubMeshPresence)
{
  // Arrange
  std::vector<Vertex> vertices = { { .position = { 0, 0, 0 },
                                     .normal = {},
                                     .texcoord = {},
                                     .tangent = {},
                                     .bitangent = {},
                                     .color = {} },
    { .position = { 1, 0, 0 },
      .normal = {},
      .texcoord = {},
      .tangent = {},
      .bitangent = {},
      .color = {} } };
  std::vector<std::uint32_t> indices { 0, 1 };
  auto material = MaterialAsset::CreateDefault();

  // Act
  auto mesh = MeshBuilder(0, "valid")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("sm", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 2,
                  .first_vertex = 0,
                  .vertex_count = 2 })
                .EndSubMesh()
                .Build();

  // Assert
  EXPECT_TRUE(mesh->IsValid());
}

} // namespace

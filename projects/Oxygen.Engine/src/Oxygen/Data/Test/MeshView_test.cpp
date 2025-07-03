//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Testing/GTest.h>
#include <gmock/gmock.h>

using oxygen::data::Mesh;
using oxygen::data::MeshView;
using oxygen::data::Vertex;

// Mock Mesh for MeshView construction
class MockMesh : public Mesh {
public:
  MockMesh(const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
    : Mesh(0, vertices, indices)
    , vertices_(vertices)
    , indices_(indices)
  {
  }

  MOCK_METHOD(
    (std::span<const Vertex>), Vertices, (), (const, noexcept, override));
  MOCK_METHOD(
    (std::span<const std::uint32_t>), Indices, (), (const, noexcept, override));

  std::vector<Vertex> vertices_;
  std::vector<std::uint32_t> indices_;
};

class MeshViewTestFixture : public ::testing::Test {
protected:
  void SetUp() override { }
  void TearDown() override { }

  void SetupMockMesh(const std::vector<Vertex>& vertices,
    const std::vector<std::uint32_t>& indices)
  {
    mesh_ = std::make_unique<MockMesh>(vertices, indices);
    ON_CALL(*mesh_, Vertices())
      .WillByDefault(
        ::testing::Return(std::span<const Vertex>(mesh_->vertices_)));
    ON_CALL(*mesh_, Indices())
      .WillByDefault(
        ::testing::Return(std::span<const std::uint32_t>(mesh_->indices_)));
  }

  std::unique_ptr<MockMesh> mesh_;
};

//=== MeshView Tests ===-----------------------------------------------------//

namespace {

using ::testing::AllOf;
using ::testing::SizeIs;

//! Tests MeshView construction with valid data and accessor methods.
NOLINT_TEST_F(MeshViewTestFixture, ConstructAndAccess)
{
  // Arrange
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
    { .position = { 1, 1, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 1, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {} },
  };
  std::vector<std::uint32_t> indices { 0, 1, 2, 2, 3, 0 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices()).Times(::testing::AnyNumber());
  EXPECT_CALL(*mesh_, Indices()).Times(::testing::AnyNumber());
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));
  EXPECT_CALL(*mesh_, Indices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const std::uint32_t>(indices)));

  // Act
  MeshView view(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 6,
      .first_vertex = 0,
      .vertex_count = 4,
    });

  // Assert
  EXPECT_THAT(view.Vertices(), SizeIs(4));
  EXPECT_THAT(view.Indices(), SizeIs(6));
  EXPECT_EQ(view.Vertices().data(), vertices.data());
  EXPECT_EQ(view.Indices().data(), indices.data());
}

//! Tests MeshView handles empty vertex and index data (should be a death test).
NOLINT_TEST_F(MeshViewTestFixture, Empty)
{
  // Arrange
  std::vector<Vertex> vertices(2);
  std::vector<std::uint32_t> indices;
  SetupMockMesh(vertices, indices);

  // Act & Assert
  EXPECT_DEATH(MeshView mesh_view(*mesh_,
                 oxygen::data::pak::MeshViewDesc {
                   .first_index = 0,
                   .index_count = 0,
                   .first_vertex = 0,
                   .vertex_count = 0,
                 }),
    "");
}

//! Tests MeshView copy and move semantics work correctly.
NOLINT_TEST_F(MeshViewTestFixture, CopyMove)
{
  // Arrange
  std::vector<Vertex> vertices(2);
  std::vector<std::uint32_t> indices { 0, 1 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));
  EXPECT_CALL(*mesh_, Indices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const std::uint32_t>(indices)));

  MeshView mesh_view1(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 2,
      .first_vertex = 0,
      .vertex_count = 2,
    });

  // Act
  MeshView mesh_view2 = mesh_view1;
  MeshView mesh_view3 = std::move(mesh_view1);

  // Assert
  EXPECT_THAT(mesh_view2.Vertices(), SizeIs(2));
  EXPECT_THAT(mesh_view2.Indices(), SizeIs(2));
  EXPECT_THAT(mesh_view3.Vertices(), SizeIs(2));
  EXPECT_THAT(mesh_view3.Indices(), SizeIs(2));
}

} // namespace

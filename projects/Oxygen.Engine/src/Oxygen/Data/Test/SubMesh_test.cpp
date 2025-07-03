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

using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;
using oxygen::data::MeshView;
using oxygen::data::SubMesh;
using oxygen::data::Vertex;

// Mock Mesh for SubMesh construction
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

class SubMeshTestFixture : public ::testing::Test {
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

//=== SubMesh Tests ===------------------------------------------------------//

namespace {

using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::SizeIs;

//! Tests SubMesh construction with valid data and accessor methods.
NOLINT_TEST_F(SubMeshTestFixture, ConstructAndAccess)
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
  };
  std::vector<std::uint32_t> indices { 0, 1 };

  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));
  EXPECT_CALL(*mesh_, Indices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const std::uint32_t>(indices)));

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 2,
      .first_vertex = 0,
      .vertex_count = 2,
    });

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act
  SubMesh submesh(*mesh_, "test_submesh", std::move(mesh_views), material);

  // Assert
  EXPECT_EQ(submesh.GetName(), "test_submesh");
  EXPECT_THAT(submesh.MeshViews(), SizeIs(1));
  EXPECT_THAT(submesh.Material(), NotNull());
  EXPECT_THAT(submesh.MeshViews()[0].Vertices(), SizeIs(2));
  EXPECT_THAT(submesh.MeshViews()[0].Indices(), SizeIs(2));
}

//! Tests SubMesh handles multiple mesh views correctly.
NOLINT_TEST_F(SubMeshTestFixture, MultipleMeshViews)
{
  // Arrange
  std::vector<Vertex> vertices = {
    // clang-format off
    { .position = { 0, 0, 0 }, .normal = { 0, 1, 0 }, .texcoord = { 0, 0 }, .tangent = { 1, 0, 0 }, .bitangent = {}, .color = {} },
    { .position = { 1, 0, 0 }, .normal = { 0, 1, 0 }, .texcoord = { 1, 0 }, .tangent = { 1, 0, 0 }, .bitangent = {}, .color = {} },
    { .position = { 0, 1, 0 }, .normal = { 0, 1, 0 }, .texcoord = { 0, 1 }, .tangent = { 1, 0, 0 }, .bitangent = {}, .color = {} },
    { .position = { 1, 1, 0 }, .normal = { 0, 1, 0 }, .texcoord = { 1, 1 }, .tangent = { 1, 0, 0 }, .bitangent = {}, .color = {} },
    // clang-format on
  };
  std::vector<std::uint32_t> indices = { 0, 1, 2, 0, 2, 3 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));
  EXPECT_CALL(*mesh_, Indices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const std::uint32_t>(indices)));

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 3,
      .first_vertex = 0,
      .vertex_count = 3,
    });
  mesh_views.emplace_back(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 3,
      .index_count = 3,
      .first_vertex = 1,
      .vertex_count = 3,
    });

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act
  SubMesh submesh(
    *mesh_, "multi_view_submesh", std::move(mesh_views), material);

  // Assert
  EXPECT_EQ(submesh.GetName(), "multi_view_submesh");
  EXPECT_THAT(submesh.MeshViews(), SizeIs(2));
  EXPECT_THAT(submesh.MeshViews()[0].Vertices(), SizeIs(3));
  EXPECT_THAT(submesh.MeshViews()[1].Vertices(), SizeIs(3));
}

//! Tests SubMesh rejects empty mesh views collection (violates 1:N constraint).
NOLINT_TEST_F(SubMeshTestFixture, EmptyMeshViews_Throws)
{
  // Arrange
  std::vector<Vertex> vertices = { { .position = { 0, 0, 0 },
    .normal = { 0, 1, 0 },
    .texcoord = { 0, 0 },
    .tangent = { 1, 0, 0 },
    .bitangent = {},
    .color = {} } };
  std::vector<std::uint32_t> indices = { 0 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));
  EXPECT_CALL(*mesh_, Indices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const std::uint32_t>(indices)));

  std::vector<MeshView> mesh_views; // Empty - violates design constraint
  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act & Assert
  EXPECT_DEATH(
    SubMesh submesh(*mesh_, "empty_submesh", std::move(mesh_views), material),
    ".*");
}

//! Tests SubMesh rejects null material pointer (violates 1:1 constraint).
NOLINT_TEST_F(SubMeshTestFixture, NullMaterial_Throws)
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

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 2,
      .first_vertex = 0,
      .vertex_count = 2,
    });

  // Act & Assert
  EXPECT_DEATH(SubMesh submesh(*mesh_, "null_material_submesh",
                 std::move(mesh_views), nullptr),
    ".*");
}

//! Tests SubMesh move semantics work correctly.
NOLINT_TEST_F(SubMeshTestFixture, Move)
{
  // Arrange
  std::vector<Vertex> vertices(3);
  std::vector<std::uint32_t> indices { 0, 1, 2 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));
  EXPECT_CALL(*mesh_, Indices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const std::uint32_t>(indices)));

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 3,
      .first_vertex = 0,
      .vertex_count = 3,
    });

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});
  SubMesh submesh1(*mesh_, "movable_submesh", std::move(mesh_views), material);

  // Act
  SubMesh submesh2 = std::move(submesh1);

  // Assert
  EXPECT_EQ(submesh2.GetName(), "movable_submesh");
  EXPECT_THAT(submesh2.MeshViews(), SizeIs(1));
  EXPECT_THAT(submesh2.Material(), NotNull());
  EXPECT_THAT(submesh2.MeshViews()[0].Vertices(), SizeIs(3));
}

//! Tests SubMesh accepts empty name string.
NOLINT_TEST_F(SubMeshTestFixture, EmptyName)
{
  // Arrange
  std::vector<Vertex> vertices(1);
  std::vector<std::uint32_t> indices { 0 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));
  EXPECT_CALL(*mesh_, Indices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const std::uint32_t>(indices)));

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 1,
      .first_vertex = 0,
      .vertex_count = 1,
    });

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act
  SubMesh submesh(*mesh_, "", std::move(mesh_views), material);

  // Assert
  EXPECT_EQ(submesh.GetName(), "");
  EXPECT_THAT(submesh.MeshViews(), SizeIs(1));
}

//! Tests SubMesh handles very long name strings.
NOLINT_TEST_F(SubMeshTestFixture, LongName)
{
  // Arrange
  std::string long_name(1000, 'a'); // 1000 character name
  std::vector<Vertex> vertices(1);
  std::vector<std::uint32_t> indices { 0 };
  SetupMockMesh(vertices, indices);
  EXPECT_CALL(*mesh_, Vertices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const Vertex>(vertices)));
  EXPECT_CALL(*mesh_, Indices())
    .Times(::testing::AnyNumber())
    .WillRepeatedly(::testing::Return(std::span<const std::uint32_t>(indices)));

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(*mesh_,
    oxygen::data::pak::MeshViewDesc {
      .first_index = 0,
      .index_count = 1,
      .first_vertex = 0,
      .vertex_count = 1,
    });

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act
  SubMesh submesh(*mesh_, long_name, std::move(mesh_views), material);

  // Assert
  EXPECT_EQ(submesh.GetName(), long_name);
  EXPECT_EQ(submesh.GetName().size(), 1000);
}

} // namespace

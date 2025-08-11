//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Data/PakFormat.h>

using oxygen::data::MaterialAsset;
using oxygen::data::MeshBuilder;
using oxygen::data::Vertex;

//! Fixture exercising SubMesh creation & view/material invariants via builder.
class SubMeshBuilderFixture : public ::testing::Test {
protected:
  void SetUp() override { }
  void TearDown() override { }

  std::shared_ptr<const MaterialAsset> MakeMaterial() const
  {
    return std::make_shared<const MaterialAsset>(
      oxygen::data::pak::MaterialAssetDesc {},
      std::vector<oxygen::data::ShaderReference> {});
  }
};

//=== SubMesh Tests ===------------------------------------------------------//

namespace {

using ::testing::AllOf;
using ::testing::HasSubstr;
using ::testing::NotNull;
using ::testing::SizeIs;

//! Tests SubMesh construction with valid data and accessor methods via builder.
NOLINT_TEST_F(SubMeshBuilderFixture, ConstructAndAccess)
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
  auto material = MakeMaterial();

  // Act
  auto mesh = MeshBuilder(0, "test_mesh")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("test_submesh", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 2,
                  .first_vertex = 0,
                  .vertex_count = 2 })
                .EndSubMesh()
                .Build();

  // Assert
  ASSERT_NE(mesh, nullptr);
  ASSERT_THAT(mesh->SubMeshes(), SizeIs(1));
  const auto& submesh = mesh->SubMeshes()[0];
  EXPECT_EQ(submesh.GetName(), "test_submesh");
  EXPECT_THAT(submesh.MeshViews(), SizeIs(1));
  EXPECT_THAT(submesh.Material(), NotNull());
  EXPECT_THAT(submesh.MeshViews()[0].Vertices(), SizeIs(2));
  EXPECT_EQ(submesh.MeshViews()[0].IndexBuffer().Count(), 2u);
}

//! Tests SubMesh handles multiple mesh views correctly via builder.
NOLINT_TEST_F(SubMeshBuilderFixture, MultipleMeshViews)
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
  auto material = MakeMaterial();

  // Act
  auto mesh = MeshBuilder(0, "mv_mesh")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("multi_view_submesh", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = 3 })
                .WithMeshView({ .first_index = 3,
                  .index_count = 3,
                  .first_vertex = 1,
                  .vertex_count = 3 })
                .EndSubMesh()
                .Build();

  // Assert
  ASSERT_NE(mesh, nullptr);
  ASSERT_THAT(mesh->SubMeshes(), SizeIs(1));
  const auto& submesh = mesh->SubMeshes()[0];
  EXPECT_EQ(submesh.GetName(), "multi_view_submesh");
  EXPECT_THAT(submesh.MeshViews(), SizeIs(2));
  EXPECT_THAT(submesh.MeshViews()[0].Vertices(), SizeIs(3));
  EXPECT_THAT(submesh.MeshViews()[1].Vertices(), SizeIs(3));
}

//! Tests EndSubMesh throws when no mesh views were added (1:N constraint).
NOLINT_TEST_F(SubMeshBuilderFixture, EmptyMeshViews_Throws)
{
  // Arrange
  std::vector<Vertex> vertices(1);
  std::vector<std::uint32_t> indices { 0 };
  auto material = MakeMaterial();
  MeshBuilder builder(0, "empty_views");
  builder.WithVertices(vertices).WithIndices(indices);

  // Act
  // (EndSubMesh attempt below.)

  // Assert
  NOLINT_EXPECT_THROW(
    { builder.BeginSubMesh("empty", material).EndSubMesh(); },
    std::logic_error);
}

//! Tests BeginSubMesh throws when material is null.
NOLINT_TEST_F(SubMeshBuilderFixture, NullMaterial_Throws)
{
  // Arrange
  std::vector<Vertex> vertices(2);
  std::vector<std::uint32_t> indices { 0, 1 };
  MeshBuilder builder(0, "null_mat");
  builder.WithVertices(vertices).WithIndices(indices);

  // Act
  // (BeginSubMesh attempt below.)

  // Assert
  NOLINT_EXPECT_THROW(
    { (void)builder.BeginSubMesh("null_material_submesh", nullptr); },
    std::logic_error);
}

//! Tests SubMesh move semantics indirectly (mesh remains valid after build).
NOLINT_TEST_F(SubMeshBuilderFixture, Move)
{
  // Arrange
  std::vector<Vertex> vertices(3);
  std::vector<std::uint32_t> indices { 0, 1, 2 };
  auto material = MakeMaterial();

  // Act
  auto mesh = MeshBuilder(0, "movable")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("movable_submesh", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = 3 })
                .EndSubMesh()
                .Build();
  auto moved = std::move(mesh);

  // Assert
  ASSERT_NE(moved, nullptr);
  ASSERT_THAT(moved->SubMeshes(), SizeIs(1));
  EXPECT_EQ(moved->SubMeshes()[0].GetName(), "movable_submesh");
}

//! Tests SubMesh accepts empty name string via builder.
NOLINT_TEST_F(SubMeshBuilderFixture, EmptyName)
{
  // Arrange
  std::vector<Vertex> vertices(1);
  std::vector<std::uint32_t> indices { 0 };
  auto material = MakeMaterial();

  // Act
  auto mesh = MeshBuilder(0, "empty_name_mesh")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 1,
                  .first_vertex = 0,
                  .vertex_count = 1 })
                .EndSubMesh()
                .Build();

  // Assert
  ASSERT_NE(mesh, nullptr);
  ASSERT_THAT(mesh->SubMeshes(), SizeIs(1));
  EXPECT_EQ(mesh->SubMeshes()[0].GetName(), "");
}

//! Tests SubMesh handles very long name strings via builder.
NOLINT_TEST_F(SubMeshBuilderFixture, LongName)
{
  // Arrange
  std::string long_name(1000, 'a');
  std::vector<Vertex> vertices(1);
  std::vector<std::uint32_t> indices { 0 };
  auto material = MakeMaterial();

  // Act
  auto mesh = MeshBuilder(0, "long_name_mesh")
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh(long_name, material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 1,
                  .first_vertex = 0,
                  .vertex_count = 1 })
                .EndSubMesh()
                .Build();

  // Assert
  ASSERT_NE(mesh, nullptr);
  ASSERT_THAT(mesh->SubMeshes(), SizeIs(1));
  EXPECT_EQ(mesh->SubMeshes()[0].GetName(), long_name);
  EXPECT_EQ(mesh->SubMeshes()[0].GetName().size(), 1000);
}

} // namespace

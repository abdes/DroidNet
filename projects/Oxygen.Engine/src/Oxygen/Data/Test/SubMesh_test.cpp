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
using oxygen::data::MeshView;
using oxygen::data::SubMesh;
using oxygen::data::Vertex;

//=== SubMesh Tests ===------------------------------------------------------//

namespace {

using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::SizeIs;

//! Tests SubMesh construction with valid data and accessor methods.
NOLINT_TEST(SubMeshBasicTest, ConstructAndAccess)
{
  // Arrange
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
  };
  std::vector<std::uint32_t> indices { 0, 1 };

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(
    std::span<const Vertex>(vertices), std::span<const std::uint32_t>(indices));

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act
  SubMesh submesh("test_submesh", std::move(mesh_views), material);

  // Assert
  EXPECT_EQ(submesh.Name(), "test_submesh");
  EXPECT_THAT(submesh.MeshViews(), SizeIs(1));
  EXPECT_THAT(submesh.Material(), NotNull());
  EXPECT_THAT(submesh.MeshViews()[0].Vertices(), SizeIs(2));
  EXPECT_THAT(submesh.MeshViews()[0].Indices(), SizeIs(2));
}

//! Tests SubMesh handles multiple mesh views correctly.
NOLINT_TEST(SubMeshBasicTest, MultipleMeshViews)
{
  // Arrange
  std::vector<Vertex> vertices1(3);
  std::vector<Vertex> vertices2(4);
  std::vector<std::uint32_t> indices1 { 0, 1, 2 };
  std::vector<std::uint32_t> indices2 { 0, 1, 2, 3 };

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(std::span<const Vertex>(vertices1),
    std::span<const std::uint32_t>(indices1));
  mesh_views.emplace_back(std::span<const Vertex>(vertices2),
    std::span<const std::uint32_t>(indices2));

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act
  SubMesh submesh("multi_view_submesh", std::move(mesh_views), material);

  // Assert
  EXPECT_EQ(submesh.Name(), "multi_view_submesh");
  EXPECT_THAT(submesh.MeshViews(), SizeIs(2));
  EXPECT_THAT(submesh.MeshViews()[0].Vertices(), SizeIs(3));
  EXPECT_THAT(submesh.MeshViews()[1].Vertices(), SizeIs(4));
}

//! Tests SubMesh rejects empty mesh views collection (violates 1:N constraint).
NOLINT_TEST(SubMeshDeathTest, EmptyMeshViews_Throws)
{
  // Arrange
  std::vector<MeshView> mesh_views; // Empty - violates design constraint
  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act & Assert
  EXPECT_DEATH(
    SubMesh submesh("empty_submesh", std::move(mesh_views), material), ".*");
}

//! Tests SubMesh rejects null material pointer (violates 1:1 constraint).
NOLINT_TEST(SubMeshDeathTest, NullMaterial_Throws)
{
  // Arrange
  std::vector<Vertex> vertices(2);
  std::vector<std::uint32_t> indices { 0, 1 };

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(
    std::span<const Vertex>(vertices), std::span<const std::uint32_t>(indices));

  // Act & Assert
  EXPECT_DEATH(
    SubMesh submesh("null_material_submesh", std::move(mesh_views), nullptr),
    ".*");
}

//! Tests SubMesh move semantics work correctly.
NOLINT_TEST(SubMeshBasicTest, Move)
{
  // Arrange
  std::vector<Vertex> vertices(3);
  std::vector<std::uint32_t> indices { 0, 1, 2 };

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(
    std::span<const Vertex>(vertices), std::span<const std::uint32_t>(indices));

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});
  SubMesh submesh1("movable_submesh", std::move(mesh_views), material);

  // Act
  SubMesh submesh2 = std::move(submesh1);

  // Assert
  EXPECT_EQ(submesh2.Name(), "movable_submesh");
  EXPECT_THAT(submesh2.MeshViews(), SizeIs(1));
  EXPECT_THAT(submesh2.Material(), NotNull());
  EXPECT_THAT(submesh2.MeshViews()[0].Vertices(), SizeIs(3));
}

//! Tests SubMesh accepts empty name string.
NOLINT_TEST(SubMeshEdgeTest, EmptyName)
{
  // Arrange
  std::vector<Vertex> vertices(1);
  std::vector<std::uint32_t> indices { 0 };

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(
    std::span<const Vertex>(vertices), std::span<const std::uint32_t>(indices));

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act
  SubMesh submesh("", std::move(mesh_views), material);

  // Assert
  EXPECT_EQ(submesh.Name(), "");
  EXPECT_THAT(submesh.MeshViews(), SizeIs(1));
}

//! Tests SubMesh handles very long name strings.
NOLINT_TEST(SubMeshEdgeTest, LongName)
{
  // Arrange
  std::string long_name(1000, 'a'); // 1000 character name
  std::vector<Vertex> vertices(1);
  std::vector<std::uint32_t> indices { 0 };

  std::vector<MeshView> mesh_views;
  mesh_views.emplace_back(
    std::span<const Vertex>(vertices), std::span<const std::uint32_t>(indices));

  auto material = std::make_shared<const MaterialAsset>(
    oxygen::data::pak::MaterialAssetDesc {},
    std::vector<oxygen::data::ShaderReference> {});

  // Act
  SubMesh submesh(long_name, std::move(mesh_views), material);

  // Assert
  EXPECT_EQ(submesh.Name(), long_name);
  EXPECT_EQ(submesh.Name().size(), 1000);
}

} // namespace

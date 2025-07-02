//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>

using oxygen::data::Mesh;
using oxygen::data::MeshView;
using oxygen::data::Vertex;

//=== MeshView Tests ===-----------------------------------------------------//

namespace {

using ::testing::AllOf;
using ::testing::SizeIs;

//! Tests MeshView construction with valid data and accessor methods.
NOLINT_TEST(MeshViewBasicTest, ConstructAndAccess)
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
    {
      .position = { 0, 1, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 0, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {},
    },
    {
      .position = { 1, 1, 0 },
      .normal = { 0, 1, 0 },
      .texcoord = { 1, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = {},
      .color = {},
    },
  };
  std::vector<std::uint32_t> indices { 0, 1, 2, 2, 3, 0 };

  // Act
  MeshView view { std::span<const Vertex>(vertices),
    std::span<const std::uint32_t>(indices) };

  // Assert
  EXPECT_THAT(view.Vertices(), SizeIs(4));
  EXPECT_THAT(view.Indices(), SizeIs(6));
  EXPECT_EQ(view.Vertices().data(), vertices.data());
  EXPECT_EQ(view.Indices().data(), indices.data());
}

//! Tests MeshView handles empty vertex and index data.
NOLINT_TEST(MeshViewEdgeTest, Empty)
{
  // Arrange
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;

  // Act
  MeshView mesh_view { std::span<const Vertex>(vertices),
    std::span<const std::uint32_t>(indices) };

  // Assert
  EXPECT_THAT(mesh_view.Vertices(), SizeIs(0));
  EXPECT_THAT(mesh_view.Indices(), SizeIs(0));
}

//! Tests MeshView copy and move semantics work correctly.
NOLINT_TEST(MeshViewBasicTest, CopyMove)
{
  // Arrange
  std::vector<Vertex> vertices(2);
  std::vector<std::uint32_t> indices { 0, 1 };

  MeshView mesh_view1 { std::span<const Vertex>(vertices),
    std::span<const std::uint32_t>(indices) };

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

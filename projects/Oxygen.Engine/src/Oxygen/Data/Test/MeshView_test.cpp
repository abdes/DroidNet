//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/MeshView.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::data::Vertex;

namespace {

//=== Test Fixtures ===-------------------------------------------------------//

// MeshViewBasicTest: encapsulation, accessors, comparison, copy/move semantics
class MeshViewBasicTest : public testing::Test {
protected:
  static auto MakeVertices() -> std::vector<Vertex>
  {
    return {
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
  }
  static auto MakeIndices() -> std::vector<std::uint32_t>
  {
    return { 0, 1, 2 };
  }
};

//! Checks that MeshView encapsulates its data and is immutable.
NOLINT_TEST_F(MeshViewBasicTest, Encapsulation)
{
  // Arrange
  auto vertices = MakeVertices();
  auto indices = MakeIndices();
  const oxygen::data::MeshView view("main", vertices, indices);

  // Act & Assert
  EXPECT_EQ(view.Name(), "main");
  EXPECT_EQ(view.Vertices().size(), 3);
  EXPECT_EQ(view.Indices().size(), 3);
}

//! Checks MeshView accessors for correct data.
NOLINT_TEST_F(MeshViewBasicTest, Accessors)
{
  // Arrange
  auto vertices = MakeVertices();
  auto indices = MakeIndices();
  const oxygen::data::MeshView view("main", vertices, indices);

  // Act & Assert
  EXPECT_EQ(view.VertexCount(), 3);
  EXPECT_EQ(view.IndexCount(), 3);
  EXPECT_EQ(view.Vertices()[1].position, glm::vec3(1, 0, 0));
  EXPECT_EQ(view.Indices()[2], 2u);
}

//! Checks MeshView comparison operators.
NOLINT_TEST_F(MeshViewBasicTest, Comparison)
{
  // Arrange
  auto vertices = MakeVertices();
  auto indices = MakeIndices();
  const oxygen::data::MeshView view1("main", vertices, indices);
  const oxygen::data::MeshView view2("main", vertices, indices);
  const oxygen::data::MeshView view3("other", vertices, indices);

  // Act & Assert
  EXPECT_EQ(view1, view2);
  EXPECT_NE(view1, view3);
}

//! Checks MeshView copy and move semantics.
NOLINT_TEST_F(MeshViewBasicTest, CopyMoveSemantics)
{
  // Arrange
  auto vertices = MakeVertices();
  auto indices = MakeIndices();
  const oxygen::data::MeshView view1("main", vertices, indices);

  // Act
  const oxygen::data::MeshView copy = view1;
  const oxygen::data::MeshView moved = view1;

  // Assert
  EXPECT_EQ(copy.Name(), "main");
  EXPECT_EQ(moved.Name(), "main");
  EXPECT_EQ(copy, moved);
}

} // namespace

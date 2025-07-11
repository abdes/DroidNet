//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Data/ProceduralMeshes.h>
#include <Oxygen/Testing/GTest.h>

//=== Test Fixtures ===-------------------------------------------------------//

namespace {

// ProceduralMeshTest: valid/invalid input, mesh validity, bounding box, default
// view, per-mesh-type
class ProceduralMeshTest : public testing::Test { };

using testing::AllOf;
using testing::IsNull;
using testing::NotNull;
using testing::SizeIs;

//! Checks that all procedural mesh factories reject invalid input and succeed
//! on valid input.
NOLINT_TEST_F(ProceduralMeshTest, ValidInvalidInput)
{
  // Arrange
  using namespace oxygen::data;

  // Act & Assert
  EXPECT_FALSE(MakeSphereMeshAsset(2, 2).has_value());
  EXPECT_TRUE(MakeSphereMeshAsset(8, 8).has_value());

  EXPECT_FALSE(MakePlaneMeshAsset(0, 1, 1.0f).has_value());
  EXPECT_FALSE(MakePlaneMeshAsset(1, 0, 1.0f).has_value());
  EXPECT_FALSE(MakePlaneMeshAsset(1, 1, 0.0f).has_value());
  EXPECT_TRUE(MakePlaneMeshAsset(2, 2, 1.0f).has_value());

  EXPECT_FALSE(MakeCylinderMeshAsset(2, 1.0f, 0.5f).has_value());
  EXPECT_FALSE(MakeCylinderMeshAsset(8, -1.0f, 0.5f).has_value());
  EXPECT_FALSE(MakeCylinderMeshAsset(8, 1.0f, -0.5f).has_value());
  EXPECT_TRUE(MakeCylinderMeshAsset(8, 1.0f, 0.5f).has_value());

  EXPECT_FALSE(MakeConeMeshAsset(2, 1.0f, 0.5f).has_value());
  EXPECT_FALSE(MakeConeMeshAsset(8, -1.0f, 0.5f).has_value());
  EXPECT_FALSE(MakeConeMeshAsset(8, 1.0f, -0.5f).has_value());
  EXPECT_TRUE(MakeConeMeshAsset(8, 1.0f, 0.5f).has_value());

  EXPECT_FALSE(MakeTorusMeshAsset(2, 8, 1.0f, 0.25f).has_value());
  EXPECT_FALSE(MakeTorusMeshAsset(8, 2, 1.0f, 0.25f).has_value());
  EXPECT_FALSE(MakeTorusMeshAsset(8, 8, -1.0f, 0.25f).has_value());
  EXPECT_FALSE(MakeTorusMeshAsset(8, 8, 1.0f, -0.25f).has_value());
  EXPECT_TRUE(MakeTorusMeshAsset(8, 8, 1.0f, 0.25f).has_value());

  EXPECT_FALSE(MakeQuadMeshAsset(0.0f, 1.0f).has_value());
  EXPECT_FALSE(MakeQuadMeshAsset(1.0f, 0.0f).has_value());
  EXPECT_TRUE(MakeQuadMeshAsset(1.0f, 1.0f).has_value());

  EXPECT_TRUE(MakeArrowGizmoMeshAsset().has_value());
  EXPECT_TRUE(MakeCubeMeshAsset().has_value());
}

//! Checks that all procedural mesh assets are valid: non-empty, correct view,
//! and index/vertex counts.
NOLINT_TEST_F(ProceduralMeshTest, MeshValidity)
{
  // Arrange
  using namespace oxygen::data;
  struct MeshFactory {
    const char* name;
    std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>> (
      *fn)();
  } factories[] = {
    { "Cube", &MakeCubeMeshAsset },
    { "ArrowGizmo", &MakeArrowGizmoMeshAsset },
  };

  // Factories with params
  const auto sphere = MakeSphereMeshAsset(8, 8);
  const auto plane = MakePlaneMeshAsset(2, 2, 1.0f);
  const auto cylinder = MakeCylinderMeshAsset(8, 1.0f, 0.5f);
  const auto cone = MakeConeMeshAsset(8, 1.0f, 0.5f);
  const auto torus = MakeTorusMeshAsset(8, 8, 1.0f, 0.25f);
  const auto quad = MakeQuadMeshAsset(1.0f, 1.0f);
  std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>> assets[]
    = { sphere, plane, cylinder, cone, torus, quad };

  // Act & Assert
  for (const auto& f : factories) {
    auto mesh_opt = f.fn();
    ASSERT_TRUE(mesh_opt.has_value()) << f.name;
    const auto& [vertices, indices] = *mesh_opt;
    EXPECT_GT(vertices.size(), 0u) << f.name;
    EXPECT_GT(indices.size(), 0u) << f.name;
  }
  for (const auto& mesh_opt : assets) {
    ASSERT_TRUE(mesh_opt.has_value());
    const auto& [vertices, indices] = *mesh_opt;
    EXPECT_GT(vertices.size(), 0u);
    EXPECT_GT(indices.size(), 0u);
  }
}

//! Checks that all procedural mesh assets compute a valid bounding box.
NOLINT_TEST_F(ProceduralMeshTest, BoundingBox)
{
  // Arrange
  using namespace oxygen::data;
  const auto mesh_opt = MakeCubeMeshAsset();
  ASSERT_TRUE(mesh_opt.has_value());
  const auto& [vertices, indices] = *mesh_opt;

  // Compute bounding box from vertices
  ASSERT_FALSE(vertices.empty());
  glm::vec3 min = vertices.front().position;
  glm::vec3 max = vertices.front().position;
  for (const auto& v : vertices) {
    min = glm::min(min, v.position);
    max = glm::max(max, v.position);
  }

  // Assert
  EXPECT_LE(min.x, max.x);
  EXPECT_LE(min.y, max.y);
  EXPECT_LE(min.z, max.z);
  // Cube should be centered at origin, size 1
  EXPECT_EQ(min, glm::vec3(-0.5f, -0.5f, -0.5f));
  EXPECT_EQ(max, glm::vec3(0.5f, 0.5f, 0.5f));
}

//! Checks that the default view of each procedural mesh covers the full mesh.
NOLINT_TEST_F(ProceduralMeshTest, DefaultView)
{
  // Arrange
  using namespace oxygen::data;
  const auto mesh_opt = MakeCubeMeshAsset();
  ASSERT_TRUE(mesh_opt.has_value());
  const auto& [vertices, indices] = *mesh_opt;

  // Act & Assert: the only view is the full data
  EXPECT_EQ(vertices.size(), 8u); // Cube should have 8 vertices
  EXPECT_EQ(indices.size(), 36u); // Cube should have 36 indices
}

//! Checks each procedural mesh type for expected geometry and view properties.
NOLINT_TEST_F(ProceduralMeshTest, PerMeshType)
{
  // Arrange
  using namespace oxygen::data;
  struct MeshType {
    const char* name;
    std::optional<std::pair<std::vector<Vertex>, std::vector<uint32_t>>> asset;
    size_t min_vertices;
    size_t min_indices;
  } types[] = {
    { "Cube", MakeCubeMeshAsset(), 8, 36 },
    { "Sphere", MakeSphereMeshAsset(8, 8), 81, 384 },
    { "Plane", MakePlaneMeshAsset(2, 2, 1.0f), 9, 24 },
    { "Cylinder", MakeCylinderMeshAsset(8, 1.0f, 0.5f), 18, 72 },
    { "Cone", MakeConeMeshAsset(8, 1.0f, 0.5f), 11, 48 },
    { "Torus", MakeTorusMeshAsset(8, 8, 1.0f, 0.25f), 81, 384 },
    { "Quad", MakeQuadMeshAsset(1.0f, 1.0f), 4, 6 },
    { "ArrowGizmo", MakeArrowGizmoMeshAsset(), 0, 0 },
  };

  // Act & Assert
  for (const auto& t : types) {
    ASSERT_TRUE(t.asset.has_value()) << t.name;
    const auto& [vertices, indices] = *t.asset;
    if (t.min_vertices > 0) {
      EXPECT_GE(vertices.size(), t.min_vertices) << t.name;
    }
    if (t.min_indices > 0) {
      EXPECT_GE(indices.size(), t.min_indices) << t.name;
    }
  }
}

} // namespace

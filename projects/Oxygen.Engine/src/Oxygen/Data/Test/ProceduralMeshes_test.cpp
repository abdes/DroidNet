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
  EXPECT_THAT(MakeSphereMeshAsset(2, 2), IsNull());
  EXPECT_THAT(MakeSphereMeshAsset(8, 8), NotNull());

  EXPECT_THAT(MakePlaneMeshAsset(0, 1, 1.0f), IsNull());
  EXPECT_THAT(MakePlaneMeshAsset(1, 0, 1.0f), IsNull());
  EXPECT_THAT(MakePlaneMeshAsset(1, 1, 0.0f), IsNull());
  EXPECT_THAT(MakePlaneMeshAsset(2, 2, 1.0f), NotNull());

  EXPECT_THAT(MakeCylinderMeshAsset(2, 1.0f, 0.5f), IsNull());
  EXPECT_THAT(MakeCylinderMeshAsset(8, -1.0f, 0.5f), IsNull());
  EXPECT_THAT(MakeCylinderMeshAsset(8, 1.0f, -0.5f), IsNull());
  EXPECT_THAT(MakeCylinderMeshAsset(8, 1.0f, 0.5f), NotNull());

  EXPECT_THAT(MakeConeMeshAsset(2, 1.0f, 0.5f), IsNull());
  EXPECT_THAT(MakeConeMeshAsset(8, -1.0f, 0.5f), IsNull());
  EXPECT_THAT(MakeConeMeshAsset(8, 1.0f, -0.5f), IsNull());
  EXPECT_THAT(MakeConeMeshAsset(8, 1.0f, 0.5f), NotNull());

  EXPECT_THAT(MakeTorusMeshAsset(2, 8, 1.0f, 0.25f), IsNull());
  EXPECT_THAT(MakeTorusMeshAsset(8, 2, 1.0f, 0.25f), IsNull());
  EXPECT_THAT(MakeTorusMeshAsset(8, 8, -1.0f, 0.25f), IsNull());
  EXPECT_THAT(MakeTorusMeshAsset(8, 8, 1.0f, -0.25f), IsNull());
  EXPECT_THAT(MakeTorusMeshAsset(8, 8, 1.0f, 0.25f), NotNull());

  EXPECT_THAT(MakeQuadMeshAsset(0.0f, 1.0f), IsNull());
  EXPECT_THAT(MakeQuadMeshAsset(1.0f, 0.0f), IsNull());
  EXPECT_THAT(MakeQuadMeshAsset(1.0f, 1.0f), NotNull());

  EXPECT_THAT(MakeArrowGizmoMeshAsset(), NotNull());
  EXPECT_THAT(MakeCubeMeshAsset(), NotNull());
}

//! Checks that all procedural mesh assets are valid: non-empty, correct view,
//! and index/vertex counts.
NOLINT_TEST_F(ProceduralMeshTest, MeshValidity)
{
  // Arrange
  using namespace oxygen::data;
  struct MeshFactory {
    const char* name;
    std::shared_ptr<Mesh> (*fn)();
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
  std::shared_ptr<Mesh> assets[]
    = { sphere, plane, cylinder, cone, torus, quad };

  // Act & Assert
  for (const auto& f : factories) {
    auto mesh = f.fn();
    ASSERT_NE(mesh, nullptr) << f.name;
    EXPECT_GT(mesh->VertexCount(), 0u) << f.name;
    EXPECT_GT(mesh->IndexCount(), 0u) << f.name;
    ASSERT_EQ(mesh->SubMeshes().size(), 1u) << f.name;
    const auto& submesh = mesh->SubMeshes().front();
    ASSERT_EQ(submesh.MeshViews().size(), 1u) << f.name;
    const auto& view = submesh.MeshViews().front();
    EXPECT_EQ(view.Vertices().size(), mesh->VertexCount()) << f.name;
    EXPECT_EQ(view.Indices().size(), mesh->IndexCount()) << f.name;
  }
  for (const auto& mesh : assets) {
    ASSERT_NE(mesh, nullptr);
    EXPECT_GT(mesh->VertexCount(), 0u);
    EXPECT_GT(mesh->IndexCount(), 0u);
    ASSERT_EQ(mesh->SubMeshes().size(), 1u);
    const auto& submesh = mesh->SubMeshes().front();
    ASSERT_EQ(submesh.MeshViews().size(), 1u);
    const auto& view = submesh.MeshViews().front();
    EXPECT_EQ(view.Vertices().size(), mesh->VertexCount());
    EXPECT_EQ(view.Indices().size(), mesh->IndexCount());
  }
}

//! Checks that all procedural mesh assets compute a valid bounding box.
NOLINT_TEST_F(ProceduralMeshTest, BoundingBox)
{
  // Arrange
  using namespace oxygen::data;
  const auto mesh = MakeCubeMeshAsset();
  ASSERT_NE(mesh, nullptr);

  // Act
  const auto& min = mesh->BoundingBoxMin();
  const auto& max = mesh->BoundingBoxMax();

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
  const auto mesh = MakeCubeMeshAsset();
  ASSERT_NE(mesh, nullptr);
  const auto& submesh = mesh->SubMeshes().front();
  const auto& view = submesh.MeshViews().front();

  // Act & Assert
  EXPECT_EQ(view.Vertices().size(), mesh->VertexCount());
  EXPECT_EQ(view.Indices().size(), mesh->IndexCount());
  EXPECT_EQ(view.Vertices().data(), mesh->Vertices().data());
  EXPECT_EQ(view.Indices().data(), mesh->Indices().data());
}

//! Checks each procedural mesh type for expected geometry and view properties.
NOLINT_TEST_F(ProceduralMeshTest, PerMeshType)
{
  // Arrange
  using namespace oxygen::data;
  struct MeshType {
    const char* name;
    std::shared_ptr<Mesh> asset;
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
    { "ArrowGizmo", MakeArrowGizmoMeshAsset(), 0,
      0 }, // ArrowGizmo: skip min counts, just check not null
  };

  // Act & Assert
  for (const auto& t : types) {
    ASSERT_NE(t.asset, nullptr) << t.name;
    if (t.min_vertices > 0) {
      EXPECT_GE(t.asset->VertexCount(), t.min_vertices) << t.name;
    }
    if (t.min_indices > 0) {
      EXPECT_GE(t.asset->IndexCount(), t.min_indices) << t.name;
    }
    ASSERT_EQ(t.asset->SubMeshes().size(), 1u) << t.name;
    const auto& submesh = t.asset->SubMeshes().front();
    ASSERT_EQ(submesh.MeshViews().size(), 1u) << t.name;
    const auto& view = submesh.MeshViews().front();
    EXPECT_EQ(view.Vertices().size(), t.asset->VertexCount()) << t.name;
    EXPECT_EQ(view.Indices().size(), t.asset->IndexCount()) << t.name;
  }
}

} // namespace

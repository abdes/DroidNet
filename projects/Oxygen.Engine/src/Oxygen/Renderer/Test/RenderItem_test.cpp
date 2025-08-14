//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RenderItem.h>

using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;
using oxygen::engine::RenderItem;

namespace {

// --- Helpers -----------------------------------------------------------------

constexpr const float kEpsilon = 1e-5F;

//! Build a simple triangle mesh with known bounds (unit triangle in XY plane).
[[nodiscard]] auto MakeUnitTriangleMesh() -> std::shared_ptr<const Mesh>
{
  std::vector<oxygen::data::Vertex> vertices = {
    {
      .position = { 0.0F, 0.0F, 0.0F },
      .normal = { 0, 0, 1 },
      .texcoord = { 0, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 },
    },
    {
      .position = { 1.0F, 0.0F, 0.0F },
      .normal = { 0, 0, 1 },
      .texcoord = { 1, 0 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 },
    },
    {
      .position = { 0.0F, 1.0F, 0.0F },
      .normal = { 0, 0, 1 },
      .texcoord = { 0, 1 },
      .tangent = { 1, 0, 0 },
      .bitangent = { 0, 1, 0 },
      .color = { 1, 1, 1, 1 },
    },
  };
  std::vector<std::uint32_t> indices = { 0, 1, 2 };

  auto material = MaterialAsset::CreateDefault();

  auto mesh = oxygen::data::MeshBuilder()
                .WithVertices(vertices)
                .WithIndices(indices)
                .BeginSubMesh("DefaultSubMesh", material)
                .WithMeshView({ .first_index = 0,
                  .index_count = 3,
                  .first_vertex = 0,
                  .vertex_count = 3 })
                .EndSubMesh()
                .Build();
  return mesh;
}

//! Create a RenderItem with given mesh and world.
[[nodiscard]] auto MakeItem(
  std::shared_ptr<const Mesh> mesh, const glm::mat4& world) -> RenderItem
{
  RenderItem item;
  item.mesh = std::move(mesh);
  item.material = MaterialAsset::CreateDefault();
  item.world_transform = world;
  return item;
}

// --- Tests -------------------------------------------------------------------

//! No mesh: defaults for bounds, normal from world3x3 only.
NOLINT_TEST(RenderItem_BasicTest, NoMesh_DefaultBoundsAndNormal)
{
  // Arrange
  RenderItem item;
  item.world_transform
    = glm::translate(glm::mat4(1.0F), glm::vec3(3.0F, -2.0F, 5.0F));

  // Act
  item.UpdatedTransformedProperties();

  // Assert
  EXPECT_FLOAT_EQ(item.bounding_sphere.x, RenderItem::kDefaultBoundingSphere.x);
  EXPECT_FLOAT_EQ(item.bounding_sphere.y, RenderItem::kDefaultBoundingSphere.y);
  EXPECT_FLOAT_EQ(item.bounding_sphere.z, RenderItem::kDefaultBoundingSphere.z);
  EXPECT_FLOAT_EQ(item.bounding_sphere.w, RenderItem::kDefaultBoundingSphere.w);

  EXPECT_FLOAT_EQ(item.bounding_box_min.x, 0.0F);
  EXPECT_FLOAT_EQ(item.bounding_box_min.y, 0.0F);
  EXPECT_FLOAT_EQ(item.bounding_box_min.z, 0.0F);
  EXPECT_FLOAT_EQ(item.bounding_box_max.x, 0.0F);
  EXPECT_FLOAT_EQ(item.bounding_box_max.y, 0.0F);
  EXPECT_FLOAT_EQ(item.bounding_box_max.z, 0.0F);

  // Normal transform ignores translation (3x3 only)
  const glm::mat3 world3(item.world_transform);
  const glm::mat3 expected_normal = glm::transpose(glm::inverse(world3));
  EXPECT_NEAR(item.normal_transform[0][0], expected_normal[0][0], kEpsilon);
  EXPECT_NEAR(item.normal_transform[1][1], expected_normal[1][1], kEpsilon);
  EXPECT_NEAR(item.normal_transform[2][2], expected_normal[2][2], kEpsilon);
}

//! Identity world: WS bounds equal mesh local bounds; normal is identity.
NOLINT_TEST(RenderItem_BasicTest, IdentityWorld_UsesMeshLocalBounds)
{
  // Arrange
  auto mesh = MakeUnitTriangleMesh();
  auto item = MakeItem(mesh, glm::mat4(1.0F));

  // Act
  item.UpdatedTransformedProperties();

  // Assert: sphere and AABB equal mesh local data
  const auto ms = mesh->BoundingSphere();
  EXPECT_FLOAT_EQ(item.bounding_sphere.x, ms.x);
  EXPECT_FLOAT_EQ(item.bounding_sphere.y, ms.y);
  EXPECT_FLOAT_EQ(item.bounding_sphere.z, ms.z);
  EXPECT_FLOAT_EQ(item.bounding_sphere.w, ms.w);

  EXPECT_FLOAT_EQ(item.bounding_box_min.x, mesh->BoundingBoxMin().x);
  EXPECT_FLOAT_EQ(item.bounding_box_min.y, mesh->BoundingBoxMin().y);
  EXPECT_FLOAT_EQ(item.bounding_box_min.z, mesh->BoundingBoxMin().z);
  EXPECT_FLOAT_EQ(item.bounding_box_max.x, mesh->BoundingBoxMax().x);
  EXPECT_FLOAT_EQ(item.bounding_box_max.y, mesh->BoundingBoxMax().y);
  EXPECT_FLOAT_EQ(item.bounding_box_max.z, mesh->BoundingBoxMax().z);

  // Normal should be identity
  EXPECT_FLOAT_EQ(item.normal_transform[0][0], 1.0F);
  EXPECT_FLOAT_EQ(item.normal_transform[1][1], 1.0F);
  EXPECT_FLOAT_EQ(item.normal_transform[2][2], 1.0F);
}

//! Non-uniform scale and translate: sphere uses max-scale, AABB via corners.
NOLINT_TEST(RenderItem_TransformTest, NonUniformScaleAndTranslate)
{
  // Arrange
  auto mesh = MakeUnitTriangleMesh();
  const auto S = glm::scale(glm::mat4(1.0F), glm::vec3(2.0F, 3.0F, 4.0F));
  const auto T = glm::translate(glm::mat4(1.0F), glm::vec3(1.0F, 2.0F, 3.0F));
  const auto world = T * S;
  auto item = MakeItem(mesh, world);

  // Act
  item.UpdatedTransformedProperties();

  // Assert: sphere
  const auto local_sphere = mesh->BoundingSphere();
  const glm::vec3 expected_center_ws = glm::vec3(
    world * glm::vec4(local_sphere.x, local_sphere.y, local_sphere.z, 1.0F));
  const float max_scale = (std::max)({ glm::length(glm::vec3(world[0])),
    glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
  const float expected_radius = local_sphere.w * max_scale;

  EXPECT_NEAR(item.bounding_sphere.x, expected_center_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.y, expected_center_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.z, expected_center_ws.z, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.w, expected_radius, kEpsilon);

  // Assert: AABB via transformed corners
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  const std::array<glm::vec3, 8> local_corners = {
    glm::vec3 { bb_min.x, bb_min.y, bb_min.z },
    glm::vec3 { bb_max.x, bb_min.y, bb_min.z },
    glm::vec3 { bb_min.x, bb_max.y, bb_min.z },
    glm::vec3 { bb_max.x, bb_max.y, bb_min.z },
    glm::vec3 { bb_min.x, bb_min.y, bb_max.z },
    glm::vec3 { bb_max.x, bb_min.y, bb_max.z },
    glm::vec3 { bb_min.x, bb_max.y, bb_max.z },
    glm::vec3 { bb_max.x, bb_max.y, bb_max.z },
  };
  glm::vec3 exp_min_ws((std::numeric_limits<float>::max)());
  glm::vec3 exp_max_ws(std::numeric_limits<float>::lowest());
  for (auto c : local_corners) {
    const auto ws = glm::vec3(world * glm::vec4(c, 1.0F));
    exp_min_ws = glm::min(exp_min_ws, ws);
    exp_max_ws = glm::max(exp_max_ws, ws);
  }
  EXPECT_NEAR(item.bounding_box_min.x, exp_min_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.y, exp_min_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.z, exp_min_ws.z, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.x, exp_max_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.y, exp_max_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.z, exp_max_ws.z, kEpsilon);

  // Assert: normal transform
  const glm::mat3 world3(world);
  const glm::mat3 expected_normal = glm::transpose(glm::inverse(world3));
  EXPECT_NEAR(item.normal_transform[0][0], expected_normal[0][0], kEpsilon);
  EXPECT_NEAR(item.normal_transform[0][1], expected_normal[0][1], kEpsilon);
  EXPECT_NEAR(item.normal_transform[0][2], expected_normal[0][2], kEpsilon);
  EXPECT_NEAR(item.normal_transform[1][0], expected_normal[1][0], kEpsilon);
  EXPECT_NEAR(item.normal_transform[1][1], expected_normal[1][1], kEpsilon);
  EXPECT_NEAR(item.normal_transform[1][2], expected_normal[1][2], kEpsilon);
  EXPECT_NEAR(item.normal_transform[2][0], expected_normal[2][0], kEpsilon);
  EXPECT_NEAR(item.normal_transform[2][1], expected_normal[2][1], kEpsilon);
  EXPECT_NEAR(item.normal_transform[2][2], expected_normal[2][2], kEpsilon);
}

//! UpdateComputedProperties should be equivalent to
//! UpdatedTransformedProperties.
NOLINT_TEST(RenderItem_ComputeTest, UpdateComputedProperties_Delegates)
{
  // Arrange
  auto mesh = MakeUnitTriangleMesh();
  const auto world
    = glm::translate(glm::mat4(1.0F), glm::vec3(3.0F, 4.0F, 5.0F));

  auto a = MakeItem(mesh, world);
  auto b = a; // copy with same state

  // Act
  a.UpdatedTransformedProperties();
  b.UpdateComputedProperties();

  // Assert: equality of computed properties
  EXPECT_FLOAT_EQ(a.bounding_sphere.x, b.bounding_sphere.x);
  EXPECT_FLOAT_EQ(a.bounding_sphere.y, b.bounding_sphere.y);
  EXPECT_FLOAT_EQ(a.bounding_sphere.z, b.bounding_sphere.z);
  EXPECT_FLOAT_EQ(a.bounding_sphere.w, b.bounding_sphere.w);

  EXPECT_FLOAT_EQ(a.bounding_box_min.x, b.bounding_box_min.x);
  EXPECT_FLOAT_EQ(a.bounding_box_min.y, b.bounding_box_min.y);
  EXPECT_FLOAT_EQ(a.bounding_box_min.z, b.bounding_box_min.z);
  EXPECT_FLOAT_EQ(a.bounding_box_max.x, b.bounding_box_max.x);
  EXPECT_FLOAT_EQ(a.bounding_box_max.y, b.bounding_box_max.y);
  EXPECT_FLOAT_EQ(a.bounding_box_max.z, b.bounding_box_max.z);

  EXPECT_FLOAT_EQ(a.normal_transform[0][0], b.normal_transform[0][0]);
  EXPECT_FLOAT_EQ(a.normal_transform[1][1], b.normal_transform[1][1]);
  EXPECT_FLOAT_EQ(a.normal_transform[2][2], b.normal_transform[2][2]);
}

//! Pure rotation: sphere radius unchanged; AABB via rotated corners; normal ok.
NOLINT_TEST(RenderItem_TransformTest, RotationOnly_AffectsAabbNotSphereRadius)
{
  // Arrange
  auto mesh = MakeUnitTriangleMesh();
  const auto R = glm::rotate(
    glm::mat4(1.0F), glm::radians(90.0F), glm::vec3(0.0F, 0.0F, 1.0F));
  auto item = MakeItem(mesh, R);

  // Act
  item.UpdatedTransformedProperties();

  // Assert: sphere radius equal to local; center rotated
  const auto local_sphere = mesh->BoundingSphere();
  const glm::vec3 expected_center_ws = glm::vec3(
    R * glm::vec4(local_sphere.x, local_sphere.y, local_sphere.z, 1.0F));
  EXPECT_NEAR(item.bounding_sphere.x, expected_center_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.y, expected_center_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.z, expected_center_ws.z, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.w, local_sphere.w, kEpsilon);

  // AABB via rotated corners
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  const std::array<glm::vec3, 8> local_corners = {
    glm::vec3 { bb_min.x, bb_min.y, bb_min.z },
    glm::vec3 { bb_max.x, bb_min.y, bb_min.z },
    glm::vec3 { bb_min.x, bb_max.y, bb_min.z },
    glm::vec3 { bb_max.x, bb_max.y, bb_min.z },
    glm::vec3 { bb_min.x, bb_min.y, bb_max.z },
    glm::vec3 { bb_max.x, bb_min.y, bb_max.z },
    glm::vec3 { bb_min.x, bb_max.y, bb_max.z },
    glm::vec3 { bb_max.x, bb_max.y, bb_max.z },
  };
  glm::vec3 exp_min_ws((std::numeric_limits<float>::max)());
  glm::vec3 exp_max_ws(std::numeric_limits<float>::lowest());
  for (auto c : local_corners) {
    const auto ws = glm::vec3(R * glm::vec4(c, 1.0F));
    exp_min_ws = glm::min(exp_min_ws, ws);
    exp_max_ws = glm::max(exp_max_ws, ws);
  }
  EXPECT_NEAR(item.bounding_box_min.x, exp_min_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.y, exp_min_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.z, exp_min_ws.z, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.x, exp_max_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.y, exp_max_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.z, exp_max_ws.z, kEpsilon);

  // Normal should equal rotation's inverse transpose = rotation itself
  const glm::mat3 world3(R);
  const glm::mat3 expected_normal = glm::transpose(glm::inverse(world3));
  EXPECT_NEAR(item.normal_transform[0][0], expected_normal[0][0], kEpsilon);
  EXPECT_NEAR(item.normal_transform[0][1], expected_normal[0][1], kEpsilon);
  EXPECT_NEAR(item.normal_transform[1][0], expected_normal[1][0], kEpsilon);
  EXPECT_NEAR(item.normal_transform[1][1], expected_normal[1][1], kEpsilon);
}

//! Negative scale (reflection): sphere uses |scale|, AABB via corners.
NOLINT_TEST(RenderItem_TransformTest, NegativeScale_UsesAbsScaleForSphere)
{
  // Arrange
  auto mesh = MakeUnitTriangleMesh();
  const auto Sneg = glm::scale(glm::mat4(1.0F), glm::vec3(-2.0F, 1.5F, -1.0F));
  const auto T = glm::translate(glm::mat4(1.0F), glm::vec3(0.5F, -1.0F, 2.0F));
  const auto world = T * Sneg;
  auto item = MakeItem(mesh, world);

  // Act
  item.UpdatedTransformedProperties();

  // Assert: sphere radius uses max column length (absolute scale)
  const auto local_sphere = mesh->BoundingSphere();
  const glm::vec3 expected_center_ws = glm::vec3(
    world * glm::vec4(local_sphere.x, local_sphere.y, local_sphere.z, 1.0F));
  const float max_scale = (std::max)({ glm::length(glm::vec3(world[0])),
    glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
  EXPECT_NEAR(item.bounding_sphere.x, expected_center_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.y, expected_center_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.z, expected_center_ws.z, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.w, local_sphere.w * max_scale, kEpsilon);

  // AABB via transformed corners
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  const std::array<glm::vec3, 8> local_corners = {
    glm::vec3 { bb_min.x, bb_min.y, bb_min.z },
    glm::vec3 { bb_max.x, bb_min.y, bb_min.z },
    glm::vec3 { bb_min.x, bb_max.y, bb_min.z },
    glm::vec3 { bb_max.x, bb_max.y, bb_min.z },
    glm::vec3 { bb_min.x, bb_min.y, bb_max.z },
    glm::vec3 { bb_max.x, bb_min.y, bb_max.z },
    glm::vec3 { bb_min.x, bb_max.y, bb_max.z },
    glm::vec3 { bb_max.x, bb_max.y, bb_max.z },
  };
  glm::vec3 exp_min_ws((std::numeric_limits<float>::max)());
  glm::vec3 exp_max_ws(std::numeric_limits<float>::lowest());
  for (auto c : local_corners) {
    const auto ws = glm::vec3(world * glm::vec4(c, 1.0F));
    exp_min_ws = glm::min(exp_min_ws, ws);
    exp_max_ws = glm::max(exp_max_ws, ws);
  }
  EXPECT_NEAR(item.bounding_box_min.x, exp_min_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.y, exp_min_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.z, exp_min_ws.z, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.x, exp_max_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.y, exp_max_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.z, exp_max_ws.z, kEpsilon);
}

//! Zero scale collapses AABB at translation; sphere radius becomes zero.
NOLINT_TEST(RenderItem_TransformTest, ZeroScale_CollapsesAabbAndSphere)
{
  // Arrange: scale(0) then translate
  auto mesh = MakeUnitTriangleMesh();
  const auto S0 = glm::scale(glm::mat4(1.0F), glm::vec3(0.0F));
  const glm::vec3 t(4.0F, -3.0F, 2.0F);
  const auto T = glm::translate(glm::mat4(1.0F), t);
  const auto world = T * S0;
  auto item = MakeItem(mesh, world);

  // Act
  item.UpdatedTransformedProperties();

  // Assert: sphere center at translation, radius 0
  EXPECT_NEAR(item.bounding_sphere.x, t.x, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.y, t.y, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.z, t.z, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.w, 0.0F, kEpsilon);

  // AABB collapsed at translation
  EXPECT_NEAR(item.bounding_box_min.x, t.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.y, t.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.z, t.z, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.x, t.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.y, t.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.z, t.z, kEpsilon);

  // Note: normal_transform is undefined (inverse(0)); intentionally not
  // checked.
}

//! Recompute after changing world: properties update accordingly.
NOLINT_TEST(RenderItem_RecomputeTest, TranslationOnly_RecomputeUpdates)
{
  // Arrange
  auto mesh = MakeUnitTriangleMesh();
  RenderItem item;
  item.mesh = mesh;
  item.material = MaterialAsset::CreateDefault();
  item.world_transform = glm::mat4(1.0F);
  item.UpdatedTransformedProperties();

  // Act: move by (+5, -2, +1)
  const glm::vec3 dt(5.0F, -2.0F, 1.0F);
  item.world_transform = glm::translate(glm::mat4(1.0F), dt);
  item.UpdatedTransformedProperties();

  // Assert: sphere center translated by dt; radius unchanged
  const auto local_sphere = mesh->BoundingSphere();
  const glm::vec3 expected_center_ws = glm::vec3(item.world_transform
    * glm::vec4(local_sphere.x, local_sphere.y, local_sphere.z, 1.0F));
  EXPECT_NEAR(item.bounding_sphere.x, expected_center_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.y, expected_center_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.z, expected_center_ws.z, kEpsilon);
  EXPECT_NEAR(item.bounding_sphere.w, local_sphere.w, kEpsilon);

  // AABB: min/max translated by dt
  const auto bb_min = mesh->BoundingBoxMin();
  const auto bb_max = mesh->BoundingBoxMax();
  const std::array<glm::vec3, 8> local_corners = {
    glm::vec3 { bb_min.x, bb_min.y, bb_min.z },
    glm::vec3 { bb_max.x, bb_min.y, bb_min.z },
    glm::vec3 { bb_min.x, bb_max.y, bb_min.z },
    glm::vec3 { bb_max.x, bb_max.y, bb_min.z },
    glm::vec3 { bb_min.x, bb_min.y, bb_max.z },
    glm::vec3 { bb_max.x, bb_min.y, bb_max.z },
    glm::vec3 { bb_min.x, bb_max.y, bb_max.z },
    glm::vec3 { bb_max.x, bb_max.y, bb_max.z },
  };
  glm::vec3 exp_min_ws((std::numeric_limits<float>::max)());
  glm::vec3 exp_max_ws(std::numeric_limits<float>::lowest());
  for (auto c : local_corners) {
    const auto ws = glm::vec3(item.world_transform * glm::vec4(c, 1.0F));
    exp_min_ws = glm::min(exp_min_ws, ws);
    exp_max_ws = glm::max(exp_max_ws, ws);
  }
  EXPECT_NEAR(item.bounding_box_min.x, exp_min_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.y, exp_min_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_min.z, exp_min_ws.z, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.x, exp_max_ws.x, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.y, exp_max_ws.y, kEpsilon);
  EXPECT_NEAR(item.bounding_box_max.z, exp_max_ws.z, kEpsilon);

  // Normal should remain identity under pure translation
  EXPECT_FLOAT_EQ(item.normal_transform[0][0], 1.0F);
  EXPECT_FLOAT_EQ(item.normal_transform[1][1], 1.0F);
  EXPECT_FLOAT_EQ(item.normal_transform[2][2], 1.0F);
}

} // namespace

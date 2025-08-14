//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Data/GeometryAsset.h>
#include <Oxygen/Data/MaterialAsset.h>
#include <Oxygen/Renderer/RenderItem.h>
#include <Oxygen/Renderer/RenderItemsList.h>

using oxygen::data::MaterialAsset;
using oxygen::data::Mesh;
using oxygen::engine::RenderItem;
using oxygen::engine::RenderItemsList;

namespace {

// --- Helpers -----------------------------------------------------------------

constexpr const float kEpsilon = 1e-5F;
constexpr const float kTwo = 2.0F;

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

//! Create a base RenderItem with provided world matrix and unit triangle mesh.
[[nodiscard]] auto MakeItem(const glm::mat4& world) -> RenderItem
{
  RenderItem item;
  item.mesh = MakeUnitTriangleMesh();
  item.material = MaterialAsset::CreateDefault();
  item.world_transform = world;
  // Intentionally do not call UpdateComputedProperties here; the container
  // should do it on Add/Update.
  return item;
}

// --- Tests -------------------------------------------------------------------

//! List operations: add/remove/update preserve order and enforce bounds.
NOLINT_TEST(RenderItemsList_BasicTest, ListOperations_OrderAndBounds)
{
  RenderItemsList list;

  // Arrange: three items with different translations
  const auto w0 = glm::mat4(1.0F);
  const auto w1 = glm::translate(glm::mat4(1.0F), glm::vec3(10.0F, 0.0F, 0.0F));
  const auto w2 = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 5.0F, 0.0F));

  const auto i0 = list.Add(MakeItem(w0));
  const auto i1 = list.Add(MakeItem(w1));
  const auto i2 = list.Add(MakeItem(w2));

  // Assert: indices are sequential and size is 3
  EXPECT_EQ(i0, 0U);
  EXPECT_EQ(i1, 1U);
  EXPECT_EQ(i2, 2U);
  EXPECT_EQ(list.Size(), 3U);

  // Act: remove middle element
  list.RemoveAt(1);

  // Assert: size reduced, order preserved (former index 2 becomes 1)
  EXPECT_EQ(list.Size(), 2U);
  const auto items_after_remove = list.Items();
  ASSERT_EQ(items_after_remove.size(), 2U);
  // Item at index 0 remained first
  EXPECT_FLOAT_EQ(items_after_remove[0].world_transform[3][0], 0.0F);
  // Item that was at index 2 moved to 1 (x stays 0, y is 5)
  EXPECT_FLOAT_EQ(items_after_remove[1].world_transform[3][1], 5.0F);

  // Bounds check: RemoveAt out of range
  EXPECT_THROW(list.RemoveAt(2), std::out_of_range);

  // Act: Update item at index 1
  const auto w2b = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 7.0F, 0.0F));
  auto updated = MakeItem(w2b);
  list.Update(1, updated);

  // Assert: Update out of range throws
  EXPECT_THROW(list.Update(5, updated), std::out_of_range);

  // Assert: Order preserved and transform updated
  const auto items_after_update = list.Items();
  ASSERT_EQ(items_after_update.size(), 2U);
  EXPECT_FLOAT_EQ(items_after_update[0].world_transform[3][0], 0.0F);
  EXPECT_FLOAT_EQ(items_after_update[1].world_transform[3][1], 7.0F);
}

//! Validation: negative sphere radius and invalid AABB throw and log.
NOLINT_TEST(RenderItemsList_ValidationTest, NegativeSphereAndInvalidAabb)
{
  RenderItemsList list;

  // Negative sphere radius: craft item with manual invalid sphere
  auto bad_sphere = MakeItem(glm::mat4(1.0F));
  bad_sphere.bounding_sphere = glm::vec4(0.0F, 0.0F, 0.0F, -1.0F);
  // Expect invalid_argument and error log
  testing::internal::CaptureStderr();
  EXPECT_THROW(list.Add(bad_sphere), std::invalid_argument);
  {
    const auto out = testing::internal::GetCapturedStderr();
    EXPECT_NE(out.find("negative bounding sphere radius"), std::string::npos);
  }

  // Invalid AABB: min > max on one component
  auto bad_aabb = MakeItem(glm::mat4(1.0F));
  bad_aabb.bounding_box_min = glm::vec3(1.0F, 0.0F, 0.0F);
  bad_aabb.bounding_box_max = glm::vec3(0.0F, 1.0F, 1.0F);
  testing::internal::CaptureStderr();
  EXPECT_THROW(list.Add(bad_aabb), std::invalid_argument);
  {
    const auto out = testing::internal::GetCapturedStderr();
    EXPECT_NE(out.find("invalid AABB min/max ordering"), std::string::npos);
  }

  // For Update path as well
  auto good = MakeItem(glm::mat4(1.0F));
  const auto idx = list.Add(good);
  auto bad_update = good;
  bad_update.bounding_box_min = glm::vec3(2.0F);
  bad_update.bounding_box_max = glm::vec3(1.0F);
  testing::internal::CaptureStderr();
  EXPECT_THROW(list.Update(idx, bad_update), std::invalid_argument);
  {
    const auto out = testing::internal::GetCapturedStderr();
    EXPECT_NE(out.find("invalid AABB min/max ordering"), std::string::npos);
  }
}

//! Recompute: Add/Update invoke UpdateComputedProperties reflecting transforms.
NOLINT_TEST(RenderItemsList_RecomputeTest, ComputedPropertiesReflectTransform)
{
  RenderItemsList list;

  // Arrange: identity -> bounding volumes should match mesh local in WS
  const auto idx = list.Add(MakeItem(glm::mat4(1.0F)));
  const auto items0 = list.Items();
  ASSERT_EQ(items0.size(), 1U);

  // Expect bounding sphere center equals mesh sphere center, radius > 0
  const auto bs0 = items0[0].bounding_sphere;
  EXPECT_GE(bs0.w, 0.0F);

  // Expect AABB equals transformed of unit triangle: min(0,0,0), max(1,1,0)
  EXPECT_FLOAT_EQ(items0[0].bounding_box_min.x, 0.0F);
  EXPECT_FLOAT_EQ(items0[0].bounding_box_min.y, 0.0F);
  EXPECT_FLOAT_EQ(items0[0].bounding_box_max.x, 1.0F);
  EXPECT_FLOAT_EQ(items0[0].bounding_box_max.y, 1.0F);

  // Act: scale by 2 then translate by +3 on X (effective: scale then translate)
  const auto S = glm::scale(glm::mat4(1.0F), glm::vec3(kTwo));
  const auto T = glm::translate(glm::mat4(1.0F), glm::vec3(3.0F, 0.0F, 0.0F));
  const auto world = T * S;
  auto updated = MakeItem(world);
  list.Update(idx, updated);

  // Assert: recomputed bounds reflect scale and translation
  const auto items1 = list.Items();
  ASSERT_EQ(items1.size(), 1U);

  // Compute expected from world and mesh local data
  const auto local_sphere = items0[0].mesh->BoundingSphere();
  const auto expected_center_ws = glm::vec3(
    world * glm::vec4(local_sphere.x, local_sphere.y, local_sphere.z, 1.0F));
  const float max_scale = (std::max)({ glm::length(glm::vec3(world[0])),
    glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
  const float expected_radius = local_sphere.w * max_scale;

  const auto bs1 = items1[0].bounding_sphere;
  EXPECT_NEAR(bs1.x, expected_center_ws.x, kEpsilon);
  EXPECT_NEAR(bs1.y, expected_center_ws.y, kEpsilon);
  EXPECT_NEAR(bs1.z, expected_center_ws.z, kEpsilon);
  EXPECT_NEAR(bs1.w, expected_radius, kEpsilon);

  // Compute expected AABB by transforming 8 corners
  const auto bb_min = items0[0].mesh->BoundingBoxMin();
  const auto bb_max = items0[0].mesh->BoundingBoxMax();
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
  EXPECT_NEAR(items1[0].bounding_box_min.x, exp_min_ws.x, kEpsilon);
  EXPECT_NEAR(items1[0].bounding_box_min.y, exp_min_ws.y, kEpsilon);
  EXPECT_NEAR(items1[0].bounding_box_min.z, exp_min_ws.z, kEpsilon);
  EXPECT_NEAR(items1[0].bounding_box_max.x, exp_max_ws.x, kEpsilon);
  EXPECT_NEAR(items1[0].bounding_box_max.y, exp_max_ws.y, kEpsilon);
  EXPECT_NEAR(items1[0].bounding_box_max.z, exp_max_ws.z, kEpsilon);

  // Normal matrix should equal transpose(inverse(world_3x3))
  const glm::mat3 world3(world);
  const glm::mat3 expected_normal = glm::transpose(glm::inverse(world3));
  EXPECT_NEAR(
    items1[0].normal_transform[0][0], expected_normal[0][0], kEpsilon);
  EXPECT_NEAR(
    items1[0].normal_transform[1][1], expected_normal[1][1], kEpsilon);
  EXPECT_NEAR(
    items1[0].normal_transform[2][2], expected_normal[2][2], kEpsilon);
}

} // namespace

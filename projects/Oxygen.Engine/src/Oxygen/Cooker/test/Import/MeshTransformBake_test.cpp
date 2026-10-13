//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/MeshTransformBake.h

#include <array>
#include <cstdint>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float3.hpp>

#include <Oxygen/Cooker/Import/Internal/MeshTransformBake.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using namespace oxygen::content::import;

NOLINT_TEST(MeshTransformBakePlanTest,
  RetainsUnsafeTransformsAndSeparatesMaterialBindings)
{
  const auto transform = glm::scale(glm::mat4(1), glm::vec3(-2, 3, 4));
  std::array<internal::MeshBakeNode, 5> nodes;
  for (auto& node : nodes) {
    node.mesh_index = 0;
    node.local_transform = transform;
  }
  nodes.at(1).material_binding = 1;
  nodes.at(2).retain_reason = "skinning";
  nodes.at(3).retain_reason = "animation";
  nodes.at(4).local_transform = glm::scale(glm::mat4(1), glm::vec3(0, 1, 1));
  const std::array<uint8_t, 1> enabled { 1 };
  const auto plan = internal::BuildMeshBakePlan(nodes, enabled, true);
  ASSERT_EQ(plan.variants.size(), 3U);
  EXPECT_NE(plan.node_variant.at(0), plan.node_variant.at(1));
  EXPECT_EQ(plan.node_variant.at(2), plan.node_variant.at(3));
  EXPECT_EQ(plan.node_variant.at(3), plan.node_variant.at(4));
  EXPECT_FALSE(plan.variants.at(plan.node_variant.at(2)).transform.has_value());
  EXPECT_EQ(plan.retained_reasons.at(2), "skinning");
  EXPECT_EQ(plan.retained_reasons.at(3), "animation");
  EXPECT_EQ(plan.retained_reasons.at(4), "non-invertible transform");
}
} // namespace

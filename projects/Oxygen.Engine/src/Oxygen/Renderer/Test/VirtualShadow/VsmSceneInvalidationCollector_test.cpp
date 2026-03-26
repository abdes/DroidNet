//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmSceneInvalidationCollector.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmLightCacheKind;
using oxygen::renderer::vsm::VsmPrimitiveIdentity;
using oxygen::renderer::vsm::VsmSceneInvalidationCollector;
using oxygen::renderer::vsm::VsmSceneLightRemapBinding;
using oxygen::renderer::vsm::VsmScenePrimitiveHistoryRecord;
using oxygen::renderer::vsm::testing::VirtualShadowTest;
using oxygen::scene::NodeHandle;

class VsmSceneInvalidationCollectorTest : public VirtualShadowTest { };

NOLINT_TEST_F(VsmSceneInvalidationCollectorTest,
  DrainPrimitiveInvalidationRecordsMergesTransformAndDestroyIntoSortedOutput)
{
  auto collector = VsmSceneInvalidationCollector {};

  const auto static_node = NodeHandle { 10, 2 };
  const auto removed_node = NodeHandle { 10, 3 };

  collector.PublishScenePrimitiveHistory(std::array {
    VsmScenePrimitiveHistoryRecord {
      .node_handle = removed_node,
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 9U,
        .transform_generation = 4U,
        .submesh_index = 1U,
      },
      .world_bounding_sphere = glm::vec4 { 4.0F, 5.0F, 6.0F, 2.0F },
      .static_shadow_caster = false,
    },
    VsmScenePrimitiveHistoryRecord {
      .node_handle = static_node,
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 3U,
        .transform_generation = 1U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { 1.0F, 2.0F, 3.0F, 4.0F },
      .static_shadow_caster = true,
    },
  });

  collector.OnTransformChanged(static_node);
  collector.OnTransformChanged(removed_node);
  collector.OnNodeDestroyed(removed_node);

  const auto drained = collector.DrainPrimitiveInvalidationRecords();
  ASSERT_EQ(drained.size(), 2U);

  EXPECT_EQ(drained[0].primitive.transform_index, 3U);
  EXPECT_EQ(drained[0].scope, VsmCacheInvalidationScope::kStaticAndDynamic);
  EXPECT_FALSE(static_cast<bool>(drained[0].is_removed));
  EXPECT_EQ(
    drained[0].world_bounding_sphere, (glm::vec4 { 1.0F, 2.0F, 3.0F, 4.0F }));

  EXPECT_EQ(drained[1].primitive.transform_index, 9U);
  EXPECT_EQ(drained[1].scope, VsmCacheInvalidationScope::kDynamicOnly);
  EXPECT_TRUE(static_cast<bool>(drained[1].is_removed));
  EXPECT_EQ(
    drained[1].world_bounding_sphere, (glm::vec4 { 4.0F, 5.0F, 6.0F, 2.0F }));

  EXPECT_TRUE(collector.DrainPrimitiveInvalidationRecords().empty());
}

NOLINT_TEST_F(VsmSceneInvalidationCollectorTest,
  DrainLightInvalidationRequestsAggregatesAndDeduplicatesRemapKeysByLightKind)
{
  auto collector = VsmSceneInvalidationCollector {};

  const auto local_light = NodeHandle { 20, 4 };
  const auto directional_light = NodeHandle { 20, 5 };

  collector.PublishSceneLightRemapBindings(std::array {
    VsmSceneLightRemapBinding {
      .node_handle = local_light,
      .kind = VsmLightCacheKind::kLocal,
      .remap_keys = { "local-b", "local-a" },
    },
    VsmSceneLightRemapBinding {
      .node_handle = local_light,
      .kind = VsmLightCacheKind::kLocal,
      .remap_keys = { "local-a", "local-c" },
    },
    VsmSceneLightRemapBinding {
      .node_handle = directional_light,
      .kind = VsmLightCacheKind::kDirectional,
      .remap_keys = { "sun-main" },
    },
  });

  collector.OnLightChanged(local_light);
  collector.OnLightChanged(directional_light);

  const auto drained = collector.DrainLightInvalidationRequests();
  ASSERT_EQ(drained.size(), 2U);

  const auto local_it = std::ranges::find_if(drained, [](const auto& request) {
    return request.kind == VsmLightCacheKind::kLocal;
  });
  ASSERT_NE(local_it, drained.end());
  EXPECT_EQ(local_it->remap_keys,
    (std::vector<std::string> { "local-a", "local-b", "local-c" }));

  const auto directional_it
    = std::ranges::find_if(drained, [](const auto& request) {
        return request.kind == VsmLightCacheKind::kDirectional;
      });
  ASSERT_NE(directional_it, drained.end());
  EXPECT_EQ(
    directional_it->remap_keys, (std::vector<std::string> { "sun-main" }));

  EXPECT_TRUE(collector.DrainLightInvalidationRequests().empty());
}

NOLINT_TEST_F(VsmSceneInvalidationCollectorTest,
  ResetClearsPublishedStateAndPendingSceneMutations)
{
  auto collector = VsmSceneInvalidationCollector {};
  const auto node = NodeHandle { 30, 6 };

  collector.PublishScenePrimitiveHistory(std::array {
    VsmScenePrimitiveHistoryRecord {
      .node_handle = node,
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 7U,
        .transform_generation = 1U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { 0.0F, 0.0F, 0.0F, 1.0F },
      .static_shadow_caster = false,
    },
  });
  collector.PublishSceneLightRemapBindings(std::array {
    VsmSceneLightRemapBinding {
      .node_handle = node,
      .kind = VsmLightCacheKind::kLocal,
      .remap_keys = { "local-0" },
    },
  });

  collector.OnTransformChanged(node);
  collector.OnLightChanged(node);
  collector.Reset();

  EXPECT_TRUE(collector.DrainPrimitiveInvalidationRecords().empty());
  EXPECT_TRUE(collector.DrainLightInvalidationRequests().empty());
}

} // namespace

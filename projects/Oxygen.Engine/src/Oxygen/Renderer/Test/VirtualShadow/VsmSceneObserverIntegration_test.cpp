//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <memory>
#include <string>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmSceneInvalidationCoordinator.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::observer_ptr;
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmLightCacheKind;
using oxygen::renderer::vsm::VsmPrimitiveIdentity;
using oxygen::renderer::vsm::VsmSceneInvalidationCoordinator;
using oxygen::renderer::vsm::VsmSceneLightRemapBinding;
using oxygen::renderer::vsm::VsmScenePrimitiveHistoryRecord;
using oxygen::renderer::vsm::testing::VirtualShadowTest;
using oxygen::scene::DirectionalLight;
using oxygen::scene::Scene;

class VsmSceneObserverIntegrationTest : public VirtualShadowTest { };

NOLINT_TEST_F(VsmSceneObserverIntegrationTest,
  DirectSceneObserverRegistrationDeliversSceneMutationsIntoCollectorDrains)
{
  auto scene = std::make_shared<Scene>("vsm-scene-observer-integration", 64);
  auto primitive_node = scene->CreateNode("primitive");
  auto light_node = scene->CreateNode("light");
  ASSERT_TRUE(primitive_node.IsValid());
  ASSERT_TRUE(light_node.IsValid());
  ASSERT_TRUE(light_node.AttachLight(std::make_unique<DirectionalLight>()));

  auto coordinator = VsmSceneInvalidationCoordinator {};
  coordinator.SyncObservedScene(observer_ptr<Scene> { scene.get() });

  coordinator.PublishScenePrimitiveHistory(std::array {
    VsmScenePrimitiveHistoryRecord {
      .node_handle = primitive_node.GetHandle(),
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 5U,
        .transform_generation = 2U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { 1.0F, 2.0F, 3.0F, 4.0F },
      .static_shadow_caster = true,
    },
  });
  coordinator.PublishSceneLightRemapBindings(std::array {
    VsmSceneLightRemapBinding {
      .node_handle = light_node.GetHandle(),
      .kind = VsmLightCacheKind::kDirectional,
      .remap_keys = { "sun-main" },
    },
  });

  ASSERT_TRUE(
    primitive_node.GetTransform().SetLocalPosition({ 7.0F, 8.0F, 9.0F }));
  ASSERT_TRUE(light_node.ReplaceLight(std::make_unique<DirectionalLight>()));

  scene->SyncObservers();

  const auto frame_inputs = coordinator.DrainFrameInputs();
  const auto& primitive_invalidations = frame_inputs.primitive_invalidations;
  ASSERT_EQ(primitive_invalidations.size(), 1U);
  EXPECT_EQ(primitive_invalidations[0].primitive.transform_index, 5U);
  EXPECT_EQ(primitive_invalidations[0].scope,
    VsmCacheInvalidationScope::kStaticAndDynamic);
  EXPECT_FALSE(static_cast<bool>(primitive_invalidations[0].is_removed));

  const auto& light_invalidations = frame_inputs.light_invalidation_requests;
  ASSERT_EQ(light_invalidations.size(), 1U);
  EXPECT_EQ(light_invalidations[0].kind, VsmLightCacheKind::kDirectional);
  EXPECT_EQ(light_invalidations[0].remap_keys,
    (std::vector<std::string> { "sun-main" }));
}

NOLINT_TEST_F(VsmSceneObserverIntegrationTest,
  DirectSceneObserverRebindingStopsDeliveringOldSceneMutations)
{
  auto scene_a = std::make_shared<Scene>("vsm-scene-a", 32);
  auto scene_b = std::make_shared<Scene>("vsm-scene-b", 32);
  auto node_a = scene_a->CreateNode("node-a");
  auto node_b = scene_b->CreateNode("node-b");
  ASSERT_TRUE(node_a.IsValid());
  ASSERT_TRUE(node_b.IsValid());

  auto coordinator = VsmSceneInvalidationCoordinator {};
  coordinator.SyncObservedScene(observer_ptr<Scene> { scene_a.get() });

  coordinator.PublishScenePrimitiveHistory(std::array {
    VsmScenePrimitiveHistoryRecord {
      .node_handle = node_a.GetHandle(),
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 11U,
        .transform_generation = 1U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { 0.0F, 0.0F, 0.0F, 2.0F },
      .static_shadow_caster = false,
    },
  });
  coordinator.SyncObservedScene(observer_ptr<Scene> { scene_b.get() });
  coordinator.PublishScenePrimitiveHistory(std::array {
    VsmScenePrimitiveHistoryRecord {
      .node_handle = node_b.GetHandle(),
      .primitive = VsmPrimitiveIdentity {
        .transform_index = 12U,
        .transform_generation = 1U,
        .submesh_index = 0U,
      },
      .world_bounding_sphere = glm::vec4 { 1.0F, 1.0F, 1.0F, 2.0F },
      .static_shadow_caster = false,
    },
  });

  ASSERT_TRUE(node_a.GetTransform().SetLocalPosition({ 1.0F, 0.0F, 0.0F }));
  ASSERT_TRUE(node_b.GetTransform().SetLocalPosition({ 0.0F, 1.0F, 0.0F }));

  scene_a->SyncObservers();
  scene_b->SyncObservers();

  const auto primitive_invalidations
    = coordinator.DrainFrameInputs().primitive_invalidations;
  ASSERT_EQ(primitive_invalidations.size(), 1U);
  EXPECT_EQ(primitive_invalidations[0].primitive.transform_index, 12U);
}

} // namespace

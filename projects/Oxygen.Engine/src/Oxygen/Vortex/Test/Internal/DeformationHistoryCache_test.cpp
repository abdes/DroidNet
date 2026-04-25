//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Vortex/Internal/DeformationHistoryCache.h>

namespace {

using oxygen::data::AssetKey;
using oxygen::scene::NodeHandle;
using oxygen::vortex::MaterialWpoPublication;
using oxygen::vortex::MotionVectorStatusPublication;
using oxygen::vortex::VelocityProducerFamily;
using oxygen::vortex::internal::DeformationHistoryCache;
using oxygen::vortex::internal::RenderMotionIdentityKey;

auto MakeIdentity(const NodeHandle node_handle, const AssetKey geometry_key,
  const VelocityProducerFamily family, const std::uint64_t contract_hash)
  -> RenderMotionIdentityKey
{
  return RenderMotionIdentityKey {
    .node_handle = node_handle,
    .geometry_asset_key = geometry_key,
    .lod_index = 0U,
    .submesh_index = 0U,
    .producer_family = family,
    .contract_hash = contract_hash,
  };
}

} // namespace

namespace oxygen::vortex::test {

NOLINT_TEST(DeformationHistoryCacheTest,
  SeedsPreviousFromCurrentThenRollsForwardOnNextFrame)
{
  auto scene = std::make_shared<scene::Scene>("DeformationHistoryCache", 16U);
  auto node = scene->CreateNode("NodeA");
  const auto geometry_key = AssetKey::FromVirtualPath("/Tests/Vortex/GeoA.ogeo");
  auto cache = DeformationHistoryCache {};

  const auto current_a = MaterialWpoPublication {
    .contract_hash = 11U,
    .capability_flags = 1U,
  };
  cache.BeginFrame(1U, observer_ptr<const scene::Scene> { scene.get() });
  const auto first = cache.TouchCurrentMaterialWpo(
    MakeIdentity(node.GetHandle(), geometry_key,
      VelocityProducerFamily::kMaterialWpo, current_a.contract_hash),
    current_a);
  EXPECT_FALSE(first.previous_valid);
  EXPECT_EQ(first.previous.contract_hash, current_a.contract_hash);
  cache.EndFrame();

  const auto current_b = MaterialWpoPublication {
    .contract_hash = 11U,
    .capability_flags = 3U,
  };
  cache.BeginFrame(2U, observer_ptr<const scene::Scene> { scene.get() });
  const auto second = cache.TouchCurrentMaterialWpo(
    MakeIdentity(node.GetHandle(), geometry_key,
      VelocityProducerFamily::kMaterialWpo, current_b.contract_hash),
    current_b);
  EXPECT_TRUE(second.previous_valid);
  EXPECT_EQ(second.previous.contract_hash, current_a.contract_hash);
  EXPECT_EQ(second.current.contract_hash, current_b.contract_hash);
}

NOLINT_TEST(DeformationHistoryCacheTest, ContractChangeInvalidatesHistory)
{
  auto scene = std::make_shared<scene::Scene>("ContractChange", 16U);
  auto node = scene->CreateNode("NodeA");
  const auto geometry_key = AssetKey::FromVirtualPath("/Tests/Vortex/GeoC.ogeo");
  auto cache = DeformationHistoryCache {};

  cache.BeginFrame(1U, observer_ptr<const scene::Scene> { scene.get() });
  static_cast<void>(cache.TouchCurrentMaterialWpo(
    MakeIdentity(node.GetHandle(), geometry_key,
      VelocityProducerFamily::kMaterialWpo, 11U),
    MaterialWpoPublication {
      .contract_hash = 11U,
      .capability_flags = 1U,
    }));
  cache.EndFrame();

  cache.BeginFrame(2U, observer_ptr<const scene::Scene> { scene.get() });
  const auto changed = cache.TouchCurrentMaterialWpo(
    MakeIdentity(node.GetHandle(), geometry_key,
      VelocityProducerFamily::kMaterialWpo, 22U),
    MaterialWpoPublication {
      .contract_hash = 22U,
      .capability_flags = 1U,
    });
  EXPECT_FALSE(changed.previous_valid);
  EXPECT_EQ(changed.previous.contract_hash, 22U);
}

NOLINT_TEST(DeformationHistoryCacheTest, SceneSwitchInvalidatesPriorHistory)
{
  auto scene_a = std::make_shared<scene::Scene>("SceneA", 16U);
  auto scene_b = std::make_shared<scene::Scene>("SceneB", 16U);
  auto node_a = scene_a->CreateNode("NodeA");
  auto cache = DeformationHistoryCache {};
  const auto geometry_key = AssetKey::FromVirtualPath("/Tests/Vortex/GeoB.ogeo");

  const auto status_a = MotionVectorStatusPublication {
    .contract_hash = 33U,
    .capability_flags = 5U,
  };
  cache.BeginFrame(1U, observer_ptr<const scene::Scene> { scene_a.get() });
  static_cast<void>(cache.TouchCurrentMotionVectorStatus(
    MakeIdentity(node_a.GetHandle(), geometry_key,
      VelocityProducerFamily::kMotionVectorStatus, status_a.contract_hash),
    status_a));
  cache.EndFrame();

  const auto status_b = MotionVectorStatusPublication {
    .contract_hash = 44U,
    .capability_flags = 7U,
  };
  cache.BeginFrame(2U, observer_ptr<const scene::Scene> { scene_b.get() });
  const auto second = cache.TouchCurrentMotionVectorStatus(
    MakeIdentity(node_a.GetHandle(), geometry_key,
      VelocityProducerFamily::kMotionVectorStatus, status_b.contract_hash),
    status_b);
  EXPECT_FALSE(second.previous_valid);
  EXPECT_EQ(second.previous.contract_hash, status_b.contract_hash);
}

} // namespace oxygen::vortex::test

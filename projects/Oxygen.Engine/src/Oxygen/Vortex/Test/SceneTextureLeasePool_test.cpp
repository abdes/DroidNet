//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Vortex/SceneRenderer/SceneTextureLeasePool.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {

using oxygen::vortex::SceneTextureLeaseKey;
using oxygen::vortex::SceneTextureLeasePool;
using oxygen::vortex::SceneTextureQueueAffinity;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::testing::FakeGraphics;

auto MakeConfig(const glm::uvec2 extent = { 160U, 90U })
  -> SceneTexturesConfig
{
  return SceneTexturesConfig {
    .extent = extent,
    .enable_velocity = true,
    .enable_custom_depth = false,
    .gbuffer_count = 4U,
    .msaa_sample_count = 1U,
  };
}

TEST(SceneTextureLeasePoolTest, ReusesReleasedLeaseForTheSameKey)
{
  FakeGraphics graphics;
  SceneTextureLeasePool pool(graphics, MakeConfig());
  const auto key = SceneTextureLeaseKey::FromConfig(MakeConfig());

  const auto* first_family = [&] {
    auto lease = pool.Acquire(key);
    EXPECT_EQ(pool.GetLiveLeaseCount(), 1U);
    return &lease.GetSceneTextures();
  }();
  EXPECT_EQ(pool.GetLiveLeaseCount(), 0U);

  auto second_lease = pool.Acquire(key);
  EXPECT_EQ(&second_lease.GetSceneTextures(), first_family);
  EXPECT_EQ(pool.GetAllocationCount(), 1U);
  EXPECT_EQ(pool.GetLeaseCountForKey(key), 1U);
}

TEST(SceneTextureLeasePoolTest, KeepsSimultaneousSameKeyLeasesDistinct)
{
  FakeGraphics graphics;
  SceneTextureLeasePool pool(graphics, MakeConfig(), 2U);
  const auto key = SceneTextureLeaseKey::FromConfig(MakeConfig());

  auto first_lease = pool.Acquire(key);
  auto second_lease = pool.Acquire(key);

  EXPECT_NE(&first_lease.GetSceneTextures(), &second_lease.GetSceneTextures());
  EXPECT_EQ(pool.GetLiveLeaseCount(), 2U);
  EXPECT_EQ(pool.GetAllocationCount(), 2U);
  EXPECT_EQ(pool.GetLeaseCountForKey(key), 2U);
}

TEST(SceneTextureLeasePoolTest, ExhaustionIsExplicitPerKey)
{
  FakeGraphics graphics;
  SceneTextureLeasePool pool(graphics, MakeConfig(), 1U);
  const auto key = SceneTextureLeaseKey::FromConfig(MakeConfig());

  auto lease = pool.Acquire(key);

  EXPECT_THROW(static_cast<void>(pool.Acquire(key)), std::runtime_error);
}

TEST(SceneTextureLeasePoolTest, QueueAffinityParticipatesInTheKey)
{
  FakeGraphics graphics;
  SceneTextureLeasePool pool(graphics, MakeConfig());
  auto graphics_key = SceneTextureLeaseKey::FromConfig(MakeConfig());
  auto future_queue_key = graphics_key;
  future_queue_key.queue_affinity = SceneTextureQueueAffinity::kAsyncCompute;

  {
    auto graphics_lease = pool.Acquire(graphics_key);
    auto future_queue_lease = pool.Acquire(future_queue_key);
    EXPECT_NE(
      &graphics_lease.GetSceneTextures(), &future_queue_lease.GetSceneTextures());
  }

  EXPECT_EQ(pool.GetLeaseCountForKey(graphics_key), 1U);
  EXPECT_EQ(pool.GetLeaseCountForKey(future_queue_key), 1U);
  EXPECT_EQ(pool.GetAllocationCount(), 2U);
}

TEST(SceneTextureLeasePoolTest, WarmupHarnessDoesNotAllocateAfterWarmup)
{
  FakeGraphics graphics;
  SceneTextureLeasePool pool(graphics, MakeConfig());
  const auto key_a = SceneTextureLeaseKey::FromConfig(MakeConfig({ 160U, 90U }));
  const auto key_b = SceneTextureLeaseKey::FromConfig(MakeConfig({ 320U, 180U }));
  constexpr auto kFrameCount = 10U;
  constexpr auto kWarmupFrames = 2U;
  auto allocations_after_warmup = std::size_t { 0U };

  for (auto frame = 0U; frame < kFrameCount; ++frame) {
    const auto allocations_before = pool.GetAllocationCount();
    {
      auto lease_a = pool.Acquire(key_a);
      auto lease_b = pool.Acquire(key_b);
      EXPECT_NE(&lease_a.GetSceneTextures(), &lease_b.GetSceneTextures());
    }
    if (frame >= kWarmupFrames) {
      allocations_after_warmup
        += pool.GetAllocationCount() - allocations_before;
    }
  }

  EXPECT_EQ(pool.GetAllocationCount(), 2U);
  EXPECT_EQ(pool.GetLeaseCountForKey(key_a), 1U);
  EXPECT_EQ(pool.GetLeaseCountForKey(key_b), 1U);
  EXPECT_EQ(allocations_after_warmup, 0U);
}

} // namespace

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <tuple>

#include <glm/ext/vector_uint2.hpp>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Internal/RetainedTexturePool.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextureLeasePool.h>
#include <Oxygen/Vortex/SceneRenderer/SceneTextures.h>
#include <Oxygen/Vortex/Test/Fakes/Graphics.h>

namespace {

using oxygen::vortex::SceneTextureLeaseKey;
using oxygen::vortex::SceneTextureLeasePool;
using oxygen::vortex::SceneTextureQueueAffinity;
using oxygen::vortex::SceneTexturesConfig;
using oxygen::vortex::testing::FakeGraphics;

auto MakeConfig(const glm::uvec2 extent = {
                  160U,
                  90U,
                }) -> SceneTexturesConfig
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

  const auto* first_family = [&] -> oxygen::vortex::SceneTextures* {
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

  EXPECT_THROW(std::ignore = pool.Acquire(key), std::runtime_error);
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
    EXPECT_NE(&graphics_lease.GetSceneTextures(),
      &future_queue_lease.GetSceneTextures());
  }

  EXPECT_EQ(pool.GetLeaseCountForKey(graphics_key), 1U);
  EXPECT_EQ(pool.GetLeaseCountForKey(future_queue_key), 1U);
  EXPECT_EQ(pool.GetAllocationCount(), 2U);
}

TEST(SceneTextureLeasePoolTest, WarmupHarnessDoesNotAllocateAfterWarmup)
{
  FakeGraphics graphics;
  SceneTextureLeasePool pool(graphics, MakeConfig());
  const auto key_a = SceneTextureLeaseKey::FromConfig(MakeConfig({
    160U,
    90U,
  }));
  const auto key_b = SceneTextureLeaseKey::FromConfig(MakeConfig({
    320U,
    180U,
  }));
  constexpr auto kFrameCount = 10U;
  constexpr auto kWarmupFrames = 2U;
  auto allocations_after_warmup = std::size_t {
    0U,
  };

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

TEST(
  SceneTextureLeasePoolTest, HdrFormatsAllocateAndReuseDistinctPhysicalFamilies)
{
  FakeGraphics graphics;
  SceneTextureLeasePool pool(graphics, MakeConfig());
  const auto half_key = SceneTextureLeaseKey::FromConfig(MakeConfig());
  auto full_config = MakeConfig();
  full_config.scene_color_format = oxygen::Format::kRGBA32Float;
  const auto full_key = SceneTextureLeaseKey::FromConfig(full_config);
  EXPECT_NE(half_key, full_key);
  auto half = pool.Acquire(half_key);
  auto full = pool.Acquire(full_key);
  auto* half_texture = &half.GetSceneTextures().GetSceneColor();
  auto* full_texture = &full.GetSceneTextures().GetSceneColor();
  EXPECT_NE(half_texture, full_texture);
  EXPECT_EQ(half_texture->GetDescriptor().format, oxygen::Format::kRGBA16Float);
  EXPECT_EQ(full_texture->GetDescriptor().format, oxygen::Format::kRGBA32Float);
  full.Release();
  auto again = pool.Acquire(full_key);
  EXPECT_EQ(&again.GetSceneTextures().GetSceneColor(), full_texture);
  EXPECT_EQ(half_texture->GetDescriptor().format, oxygen::Format::kRGBA16Float);
  EXPECT_EQ(pool.GetAllocationCount(), 2U);
}

TEST(SceneTextureLeasePoolTest, RetainedLeaseOutlivesPoolWithoutDanglingRelease)
{
  FakeGraphics graphics;
  auto retained = oxygen::vortex::SceneTextureLease {};
  const auto key = SceneTextureLeaseKey::FromConfig(MakeConfig());
  {
    auto pool = std::make_unique<SceneTextureLeasePool>(graphics, MakeConfig());
    retained = pool->Acquire(key);
    EXPECT_TRUE(retained.IsValid());
    EXPECT_EQ(pool->GetLiveLeaseCount(), 1U);
  }
  EXPECT_EQ(retained.GetKey(), key);
  EXPECT_EQ(retained.GetSceneTextures().GetExtent(), key.extent);
  retained.Release();
  EXPECT_FALSE(retained.IsValid());
}

TEST(SceneTextureLeasePoolTest, ColorReadersDoNotRetainAttachmentFamily)
{
  auto graphics = std::make_shared<FakeGraphics>();
  graphics->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy {});
  auto colors = oxygen::vortex::internal::RetainedTexturePool(graphics);
  const auto config = MakeConfig();
  const auto key = SceneTextureLeaseKey::FromConfig(config);
  SceneTextureLeasePool families(*graphics, config);
  const auto view = oxygen::ViewId {
    1U,
  };
  const auto desc = oxygen::vortex::SceneTextures::SceneColorDescriptor(config);
  auto reader = colors.Acquire(view, desc, true);
  auto first = families.Acquire(key, reader);
  auto* family = &first.GetSceneTextures();
  const auto* depth = &family->GetSceneDepth();
  const auto* color = reader.get();
  first.Retire(*graphics);
  first.Release();
  EXPECT_EQ(families.GetLiveLeaseCount(), 1U);
  {
    auto pending = families.Acquire(key, colors.Acquire(view, desc, true));
    EXPECT_NE(&pending.GetSceneTextures(), family);
    EXPECT_NE(&pending.GetSceneTextures().GetSceneColor(), color);
  }
  graphics->GetDeferredReclaimer().ProcessAllDeferredReleases();
  EXPECT_EQ(families.GetLiveLeaseCount(), 0U);
  auto next = families.Acquire(key, colors.Acquire(view, desc, true));
  EXPECT_EQ(&next.GetSceneTextures(), family);
  EXPECT_EQ(&next.GetSceneTextures().GetSceneDepth(), depth);
  EXPECT_NE(&next.GetSceneTextures().GetSceneColor(), color);
  EXPECT_TRUE(graphics->GetResourceRegistry().Contains(*reader));
  reader.reset();
  next.Retire(*graphics);
  next.Release();
  graphics->GetDeferredReclaimer().ProcessAllDeferredReleases();
}

} // namespace

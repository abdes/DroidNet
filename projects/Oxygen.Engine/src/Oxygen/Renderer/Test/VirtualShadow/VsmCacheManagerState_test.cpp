//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheInvalidationReason;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerConfig;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::testing::VsmCacheManagerTestBase;

class VsmCacheManagerStateTest : public VsmCacheManagerTestBase { };

NOLINT_TEST_F(
  VsmCacheManagerStateTest, CacheManagerLifecycleStartsUnavailableAndIdle)
{
  const auto manager = VsmCacheManager(
    nullptr, VsmCacheManagerConfig { .debug_name = "phase2-manager" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

NOLINT_TEST_F(VsmCacheManagerStateTest,
  CacheManagerBeginFrameTransitionsToFrameOpenOnColdStart)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase2-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase2-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 1ULL, 10U, "phase2-begin");
  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "phase2-open" });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

NOLINT_TEST_F(VsmCacheManagerStateTest,
  CacheManagerInvalidateAllTransitionsAvailableDataToInvalidated)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase2-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase2-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(manager, MakeSeam(pool_manager, 1ULL, 10U, "phase2-prev"));

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  manager.InvalidateAll(VsmCacheInvalidationReason::kExplicitInvalidateAll);

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame()->frame_generation, 1ULL);
}

NOLINT_TEST_F(
  VsmCacheManagerStateTest, CacheManagerResetClearsStateMachinesAndStoredFrames)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase2-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase2-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(manager, MakeSeam(pool_manager, 1ULL, 10U, "phase2-prev"));
  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "phase2-current"));
  manager.Reset();

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

NOLINT_TEST_F(VsmCacheManagerStateTest,
  CacheManagerAbortFrameRestoresIdleStateAndPreservesPreviousExtraction)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase2-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase2-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  const auto previous_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase2-previous");
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, previous_frame), "phase2-previous");

  const auto current_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "phase2-current");
  const auto request = oxygen::renderer::vsm::VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
  };
  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase2-current" });
  manager.SetPageRequests({ &request, 1U });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());

  manager.AbortFrame();

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame()->frame_generation, 1ULL);

  manager.BeginFrame(MakeSeam(pool_manager, 3ULL, 30U, "phase2-reopen"),
    VsmCacheManagerFrameConfig { .debug_name = "phase2-reopen" });
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
}

} // namespace

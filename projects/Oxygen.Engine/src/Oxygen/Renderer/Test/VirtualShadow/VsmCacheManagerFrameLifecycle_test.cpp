//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>

namespace {

using oxygen::Format;
using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::testing::VsmCacheManagerTestBase;

class VsmCacheManagerFrameLifecycleTest : public VsmCacheManagerTestBase { };

NOLINT_TEST_F(VsmCacheManagerFrameLifecycleTest,
  CacheManagerCommitAndExtractPublishCanonicalSnapshotShape)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase2-frame-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase2-frame-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 7ULL, 32U, "phase2-frame", 2U);
  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "phase2-commit" });

  const auto& plan = manager.BuildPageAllocationPlan();
  EXPECT_TRUE(plan.decisions.empty());
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kPlanned);

  const auto& frame = manager.CommitPageAllocationFrame();
  const auto committed_snapshot = frame.snapshot;
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kReady);
  EXPECT_TRUE(frame.is_ready);
  EXPECT_EQ(
    frame.snapshot.frame_generation, seam.current_frame.frame_generation);
  EXPECT_EQ(frame.snapshot.pool_identity, seam.physical_pool.pool_identity);
  EXPECT_EQ(frame.snapshot.virtual_frame, seam.current_frame);
  EXPECT_EQ(frame.snapshot.page_table.size(),
    seam.current_frame.total_page_table_entry_count);
  EXPECT_EQ(
    frame.snapshot.physical_pages.size(), seam.physical_pool.tile_capacity);
  EXPECT_EQ(frame.snapshot.light_cache_entries.size(), 2U);
  ASSERT_NE(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetCurrentFrame()->snapshot, committed_snapshot);

  manager.ExtractFrameData();
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_TRUE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(*manager.GetPreviousFrame(), committed_snapshot);
}

NOLINT_TEST_F(VsmCacheManagerFrameLifecycleTest,
  CacheManagerInvalidatesPreviousDataWhenPoolCompatibilityChanges)
{
  auto previous_pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(previous_pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase2-frame-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(previous_pool_manager.EnsureHzbPool(
              MakeHzbPoolConfig("phase2-frame-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    MakeSeam(previous_pool_manager, 1ULL, 10U, "phase2-frame", 1U));
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  auto incompatible_pool_manager = VsmPhysicalPagePoolManager(nullptr);
  auto different_pool = MakeShadowPoolConfig("phase2-frame-shadow-pool");
  different_pool.depth_format = Format::kDepth32Stencil8;
  ASSERT_EQ(incompatible_pool_manager.EnsureShadowPool(different_pool),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(incompatible_pool_manager.EnsureHzbPool(
              MakeHzbPoolConfig("phase2-frame-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  manager.BeginFrame(
    MakeSeam(incompatible_pool_manager, 2ULL, 20U, "phase2-frame", 1U));
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

NOLINT_TEST_F(VsmCacheManagerFrameLifecycleTest,
  CacheManagerKeepsPreviousDataAvailableAcrossOrdinaryFrameShapeChanges)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase2-frame-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase2-frame-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, 1ULL, 10U, "phase2-frame", 1U));
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "phase2-frame", 2U));
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_TRUE(manager.IsHzbDataAvailable());
}

NOLINT_TEST_F(VsmCacheManagerFrameLifecycleTest,
  CacheManagerHzbAvailabilityIsCompatibilityGated)
{
  auto previous_pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(previous_pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase2-frame-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(previous_pool_manager.EnsureHzbPool(
              MakeHzbPoolConfig("phase2-frame-hzb-pool")),
    oxygen::renderer::vsm::VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    MakeSeam(previous_pool_manager, 1ULL, 10U, "phase2-frame", 1U));
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  auto no_hzb_pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(no_hzb_pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase2-frame-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);

  manager.BeginFrame(
    MakeSeam(no_hzb_pool_manager, 2ULL, 20U, "phase2-frame", 1U));
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

NOLINT_TEST_F(VsmCacheManagerFrameLifecycleTest,
  CacheManagerIllegalApiOrderingFailsDeterministically)
{
  auto manager = VsmCacheManager(nullptr);

  NOLINT_EXPECT_DEATH((void)manager.BuildPageAllocationPlan(), ".*");
  NOLINT_EXPECT_DEATH((void)manager.CommitPageAllocationFrame(), ".*");
  NOLINT_EXPECT_DEATH((void)manager.ExtractFrameData(), ".*");
}

} // namespace

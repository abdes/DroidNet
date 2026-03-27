//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Stage 1 — Pool Compatibility
//
// Verifies that BeginFrame evaluates pool-snapshot compatibility correctly:
// a compatible pool retains kAvailable, each changed field independently
// triggers kInvalidated.

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::Format;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::testing::VsmCacheManagerTestBase;

class VsmBeginFramePoolCompatibilityTest : public VsmCacheManagerTestBase { };

// ── Baseline: unchanged pool preserves available state ──────────────────────

// A warm start where no pool field changes must leave the cache in kAvailable.
NOLINT_TEST_F(
  VsmBeginFramePoolCompatibilityTest, CompatiblePoolPreservesAvailableState)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("compat.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("compat.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(manager, MakeSeam(pool_manager, 1ULL, 10U, "compat.prev"));

  // Second BeginFrame — same pool manager, different virtual-address range.
  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "compat.curr"),
    VsmCacheManagerFrameConfig { .debug_name = "compat.curr" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

// ── Per-field invalidation ───────────────────────────────────────────────────

// Changing pool_depth_format is the canonical incompatibility signal.
NOLINT_TEST_F(
  VsmBeginFramePoolCompatibilityTest, DepthFormatChangeCausesInvalidation)
{
  auto previous_pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(previous_pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("depth-fmt.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    previous_pool_manager.EnsureHzbPool(MakeHzbPoolConfig("depth-fmt.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(
    manager, MakeSeam(previous_pool_manager, 1ULL, 10U, "depth-fmt.prev"));

  auto incompatible_pool_manager = VsmPhysicalPagePoolManager(nullptr);
  auto different_pool = MakeShadowPoolConfig("depth-fmt.shadow");
  different_pool.depth_format = Format::kDepth32Stencil8;
  ASSERT_EQ(incompatible_pool_manager.EnsureShadowPool(different_pool),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(
    incompatible_pool_manager.EnsureHzbPool(MakeHzbPoolConfig("depth-fmt.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  manager.BeginFrame(
    MakeSeam(incompatible_pool_manager, 2ULL, 20U, "depth-fmt.curr"),
    VsmCacheManagerFrameConfig { .debug_name = "depth-fmt.curr" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

NOLINT_TEST_F(
  VsmBeginFramePoolCompatibilityTest, PoolIdentityChangeCausesInvalidation)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("identity.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("identity.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, 1ULL, 10U, "identity.prev"));

  auto seam = MakeSeam(pool_manager, 2ULL, 20U, "identity.curr");
  seam.physical_pool.pool_identity += 100U;
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "identity.curr" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
}

NOLINT_TEST_F(
  VsmBeginFramePoolCompatibilityTest, PageSizeTexelsChangeCausesInvalidation)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("pagesize.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("pagesize.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, 1ULL, 10U, "pagesize.prev"));

  auto seam = MakeSeam(pool_manager, 2ULL, 20U, "pagesize.curr");
  seam.physical_pool.page_size_texels = 64U; // default is 128
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "pagesize.curr" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
}

NOLINT_TEST_F(
  VsmBeginFramePoolCompatibilityTest, TileCapacityChangeCausesInvalidation)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("capacity.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("capacity.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, 1ULL, 10U, "capacity.prev"));

  auto seam = MakeSeam(pool_manager, 2ULL, 20U, "capacity.curr");
  seam.physical_pool.tile_capacity = 128U; // default is 512
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "capacity.curr" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
}

NOLINT_TEST_F(
  VsmBeginFramePoolCompatibilityTest, SliceCountChangeCausesInvalidation)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("slicecount.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("slicecount.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, 1ULL, 10U, "slicecount.prev"));

  auto seam = MakeSeam(pool_manager, 2ULL, 20U, "slicecount.curr");
  seam.physical_pool.slice_count = 1U; // default is 2
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "slicecount.curr" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
}

NOLINT_TEST_F(
  VsmBeginFramePoolCompatibilityTest, SliceRolesChangeCausesInvalidation)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("sliceroles.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("sliceroles.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, 1ULL, 10U, "sliceroles.prev"));

  auto seam = MakeSeam(pool_manager, 2ULL, 20U, "sliceroles.curr");
  // Default is {kDynamicDepth, kStaticDepth}; drop to dynamic-only.
  seam.physical_pool.slice_roles = { VsmPhysicalPoolSliceRole::kDynamicDepth };
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "sliceroles.curr" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
}

// ── GAP-8: pool not available in the seam ───────────────────────────────────

// When the current seam carries a shadow pool snapshot with is_available =
// false (i.e., no active shadow pool this frame), the pool_identity in the
// snapshot will be 0 (default), which differs from the previous extracted
// frame's pool_identity. BeginFrame must detect this as a pool identity change
// and produce kInvalidated.
NOLINT_TEST_F(VsmBeginFramePoolCompatibilityTest,
  PoolNotAvailableInSeamOnWarmStartProducesInvalidated)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("unavail.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("unavail.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(manager, MakeSeam(pool_manager, 1ULL, 10U, "unavail.prev"));
  ASSERT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);

  // Build a seam whose shadow pool reports is_available = false (no pool this
  // frame). pool_identity will be 0 (default), triggering the identity check.
  auto seam = MakeSeam(pool_manager, 2ULL, 20U, "unavail.curr");
  seam.physical_pool.is_available = false;
  seam.physical_pool.pool_identity = 0U;
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "unavail.curr" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

} // namespace

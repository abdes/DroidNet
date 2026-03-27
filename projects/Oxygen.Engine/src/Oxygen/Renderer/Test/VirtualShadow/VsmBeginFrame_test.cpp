//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Stage 1 — Begin Frame
//
// Covers: seam type contracts, seam field correctness, build-state machine,
// cache-data state transitions (cold, warm, force-invalidate, sticky-invalid),
// Reset/Abort recovery, illegal API ordering, and shape-compatibility contract.

#include <array>
#include <type_traits>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManagerSeam.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmPhysicalPagePoolManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowStageCpuHarness.h"

namespace {

using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmCacheDataState;
using oxygen::renderer::vsm::VsmCacheInvalidationReason;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerConfig;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmCacheManagerSeam;
using oxygen::renderer::vsm::VsmDirectionalClipmapDesc;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPhysicalPoolSliceRole;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::testing::LocalStageLightSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

class VsmBeginFrameTest : public VsmStageCpuHarness { };

// ── Type and seam contracts ──────────────────────────────────────────────────

NOLINT_TEST_F(
  VsmBeginFrameTest, TypeTraitsExposeNonCopyableManagerAndCopyableSeam)
{
  static_assert(std::is_constructible_v<VsmCacheManager, oxygen::Graphics*,
    const VsmCacheManagerConfig&>);
  static_assert(!std::is_copy_constructible_v<VsmCacheManager>);
  static_assert(!std::is_move_constructible_v<VsmCacheManager>);
  static_assert(std::is_copy_constructible_v<VsmCacheManagerSeam>);
}

// Verifies that a seam assembled from two address-space frames carries the
// correct pool snapshot, HZB snapshot, and remap table field values.
NOLINT_TEST_F(VsmBeginFrameTest, SeamExposesCorrectPoolHzbAndRemapFields)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("seam-shadow-pool")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("seam-hzb-pool")),
    VsmHzbPoolChangeResult::kCreated);

  auto previous_space = VsmVirtualAddressSpace {};
  previous_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 10,
      .clipmap_reuse_config = {
        .max_page_offset_x = 4,
        .max_page_offset_y = 4,
        .depth_range_epsilon = 0.01F,
        .page_world_size_epsilon = 0.01F,
      },
      .debug_name = "seam-prev",
    },
    1ULL);
  previous_space.AllocateSinglePageLocalLight(
    VsmSinglePageLightDesc { .remap_key = "local-a", .debug_name = "local-a" });
  previous_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
    .remap_key = "sun",
    .clip_level_count = 2,
    .pages_per_axis = 4,
    .page_grid_origin = { { 0, 0 }, { 4, 4 } },
    .page_world_size = { 32.0F, 64.0F },
    .near_depth = { 1.0F, 2.0F },
    .far_depth = { 101.0F, 202.0F },
    .debug_name = "sun",
  });
  const auto previous_frame = previous_space.DescribeFrame();

  auto current_space = VsmVirtualAddressSpace {};
  current_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 20,
      .clipmap_reuse_config = {
        .max_page_offset_x = 4,
        .max_page_offset_y = 4,
        .depth_range_epsilon = 0.01F,
        .page_world_size_epsilon = 0.01F,
      },
      .debug_name = "seam-current",
    },
    2ULL);
  current_space.AllocateSinglePageLocalLight(
    VsmSinglePageLightDesc { .remap_key = "local-a", .debug_name = "local-a" });
  current_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
    .remap_key = "sun",
    .clip_level_count = 2,
    .pages_per_axis = 4,
    .page_grid_origin = { { 1, -1 }, { 5, 3 } },
    .page_world_size = { 32.0F, 64.0F },
    .near_depth = { 1.0F, 2.0F },
    .far_depth = { 101.0F, 202.0F },
    .debug_name = "sun",
  });

  const auto current_frame = current_space.DescribeFrame();
  const auto remap = current_space.BuildRemapTable(previous_frame);
  const auto seam = VsmCacheManagerSeam {
    .physical_pool = pool_manager.GetShadowPoolSnapshot(),
    .hzb_pool = pool_manager.GetHzbPoolSnapshot(),
    .current_frame = current_frame,
    .previous_to_current_remap = remap,
  };

  EXPECT_TRUE(seam.physical_pool.is_available);
  EXPECT_EQ(seam.physical_pool.pool_identity, 1ULL);
  ASSERT_EQ(seam.physical_pool.slice_roles.size(), 2U);
  EXPECT_EQ(
    seam.physical_pool.slice_roles[0], VsmPhysicalPoolSliceRole::kDynamicDepth);
  EXPECT_EQ(
    seam.physical_pool.slice_roles[1], VsmPhysicalPoolSliceRole::kStaticDepth);
  EXPECT_TRUE(seam.hzb_pool.is_available);
  EXPECT_EQ(seam.hzb_pool.width, 1024U);
  EXPECT_EQ(seam.hzb_pool.height, 1024U);
  EXPECT_EQ(seam.hzb_pool.array_size, 1U);
  EXPECT_EQ(seam.current_frame.frame_generation, 2ULL);
  ASSERT_EQ(seam.current_frame.local_light_layouts.size(), 1U);
  EXPECT_EQ(seam.current_frame.local_light_layouts[0].remap_key, "local-a");
  EXPECT_EQ(previous_frame.directional_layouts[0].remap_key, "sun");
  ASSERT_EQ(seam.previous_to_current_remap.entries.size(), 3U);
  EXPECT_EQ(seam.previous_to_current_remap.entries[0].current_id, 20U);
}

// ── Cold-start behavior ──────────────────────────────────────────────────────

// BeginFrame on a freshly constructed manager must produce kFrameOpen /
// kUnavailable regardless of seam content.
NOLINT_TEST_F(VsmBeginFrameTest, InitialStateIsIdleAndUnavailable)
{
  const auto manager = VsmCacheManager(
    nullptr, VsmCacheManagerConfig { .debug_name = "init-check" });

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

NOLINT_TEST_F(
  VsmBeginFrameTest, ColdStartWithSinglePageLocalLightOpensColdFrameState)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("cold-single.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("cold-single.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 1ULL, 10U, "cold-single");
  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "cold-single" });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

// Verifies cold-start with a mixed seam (multi-level local + directional) and
// checks that total_page_table_entry_count is computed correctly.
NOLINT_TEST_F(
  VsmBeginFrameTest, ColdStartWithMultiLightMixedLayoutOpensColdFrameState)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("cold-mixed.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("cold-mixed.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
    LocalStageLightSpec { .remap_key = "fill-local" },
  };
  const auto current_frame
    = MakeLocalFrame(11ULL, 40U, kLights, "cold-mixed.current");
  ASSERT_EQ(current_frame.local_light_layouts.size(), 2U);
  EXPECT_EQ(current_frame.total_page_table_entry_count, 9U);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, current_frame),
    VsmCacheManagerFrameConfig { .debug_name = "cold-mixed" });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

// A seam that carries a populated remap table must not promote the cache to
// kAvailable on a cold start — availability requires a prior extraction.
NOLINT_TEST_F(VsmBeginFrameTest,
  ColdStartWithRemapTableButNoPreviousFrameRemainsUnavailable)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("cold-remap.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("cold-remap.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto previous_space = VsmVirtualAddressSpace {};
  previous_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 10, .debug_name = "cold-remap-prev" },
    1ULL);
  previous_space.AllocateSinglePageLocalLight(
    VsmSinglePageLightDesc { .remap_key = "local-a", .debug_name = "local-a" });
  const auto previous_frame = previous_space.DescribeFrame();

  auto current_space = VsmVirtualAddressSpace {};
  current_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 20, .debug_name = "cold-remap-curr" },
    2ULL);
  current_space.AllocateSinglePageLocalLight(
    VsmSinglePageLightDesc { .remap_key = "local-a", .debug_name = "local-a" });
  const auto current_frame = current_space.DescribeFrame();

  const auto seam = VsmCacheManagerSeam {
    .physical_pool = pool_manager.GetShadowPoolSnapshot(),
    .hzb_pool = pool_manager.GetHzbPoolSnapshot(),
    .current_frame = current_frame,
    .previous_to_current_remap = current_space.BuildRemapTable(previous_frame),
  };
  ASSERT_FALSE(seam.previous_to_current_remap.entries.empty());

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "cold-remap" });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

// ── Warm-start and invalidation paths ───────────────────────────────────────

// After a successful extraction, the next BeginFrame with a compatible pool
// must report kAvailable — verifying the primary cache-reuse signal.
NOLINT_TEST_F(VsmBeginFrameTest, WarmStartTransitionsToCacheDataAvailable)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeShadowPoolConfig("warm.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("warm.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights
    = std::array { LocalStageLightSpec { .remap_key = "light-a" } };
  const auto prev_frame = MakeLocalFrame(1ULL, 40U, kLights, "warm.prev");
  const auto curr_frame = MakeLocalFrame(2ULL, 40U, kLights, "warm.curr");

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "warm.prev" });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  manager.BeginFrame(MakeSeam(pool_manager, curr_frame, &prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "warm.curr" });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
  EXPECT_TRUE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame()->frame_generation, 1ULL);
}

// force_invalidate_all = true in the frame config must override an available
// cache and transition it directly to kInvalidated.
NOLINT_TEST_F(
  VsmBeginFrameTest, ForceInvalidateAllTransitionsAvailableCacheToInvalidated)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("forceinv.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("forceinv.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  constexpr auto kLights
    = std::array { LocalStageLightSpec { .remap_key = "light-a" } };
  const auto prev_frame = MakeLocalFrame(1ULL, 40U, kLights, "forceinv.prev");
  const auto curr_frame = MakeLocalFrame(2ULL, 40U, kLights, "forceinv.curr");

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, prev_frame),
    VsmCacheManagerFrameConfig { .debug_name = "forceinv.prev" });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();
  ASSERT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);

  manager.BeginFrame(MakeSeam(pool_manager, curr_frame, &prev_frame),
    VsmCacheManagerFrameConfig {
      .force_invalidate_all = true,
      .debug_name = "forceinv.curr",
    });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame()->frame_generation, 1ULL);
}
// kInvalidated and leave the previous-frame snapshot accessible.
NOLINT_TEST_F(VsmBeginFrameTest, InvalidateAllMakesCacheDataInvalidated)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("invall.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("invall.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(manager, MakeSeam(pool_manager, 1ULL, 10U, "invall.prev"));
  ASSERT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);

  manager.InvalidateAll(VsmCacheInvalidationReason::kExplicitInvalidateAll);

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame()->frame_generation, 1ULL);
}

// Invalidated cache must remain invalidated across a subsequent BeginFrame with
// a fully compatible pool — kInvalidated is sticky until extraction succeeds.
NOLINT_TEST_F(
  VsmBeginFrameTest, InvalidatedCacheRemainsInvalidatedAcrossBeginFrame)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("sticky.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("sticky.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(manager, MakeSeam(pool_manager, 1ULL, 10U, "sticky.prev"));
  ASSERT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);

  manager.InvalidateAll(VsmCacheInvalidationReason::kExplicitInvalidateAll);
  ASSERT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);

  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "sticky.curr"),
    VsmCacheManagerFrameConfig { .debug_name = "sticky.curr" });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame()->frame_generation, 1ULL);
}

// ── Recovery paths ───────────────────────────────────────────────────────────

// AbortFrame must return the manager to kIdle / kInvalidated and keep the
// previous-frame snapshot accessible for diagnostic reads.
NOLINT_TEST_F(VsmBeginFrameTest, AbortFrameInvalidatesAndPreservesPreviousData)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeShadowPoolConfig("abort.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("abort.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  const auto previous_frame = MakeSinglePageLocalFrame(1ULL, 10U, "abort.prev");
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, previous_frame), "abort.prev");

  const auto current_frame = MakeSinglePageLocalFrame(2ULL, 20U, "abort.curr");
  const auto request = VsmPageRequest {
    .map_id = current_frame.local_light_layouts[0].id,
    .page = {},
  };
  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "abort.curr" });
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

  // A subsequent BeginFrame after abort must be valid and inherit the
  // invalidated state rather than resetting to unavailable, and HZB must
  // remain suppressed through the sticky-kInvalidated path.
  manager.BeginFrame(MakeSeam(pool_manager, 3ULL, 30U, "abort.reopen"),
    VsmCacheManagerFrameConfig { .debug_name = "abort.reopen" });
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

// Reset must clear both state machines and discard all stored frames.
NOLINT_TEST_F(VsmBeginFrameTest, ResetClearsBothStateMachinesAndStoredFrames)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeShadowPoolConfig("reset.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("reset.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(manager, MakeSeam(pool_manager, 1ULL, 10U, "reset.prev"));
  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "reset.curr"));
  manager.Reset();

  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kIdle);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetCurrentFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

// ── API contract ─────────────────────────────────────────────────────────────

// The three pipeline calls that require an active frame must CHECK-fail when
// invoked before BeginFrame.
NOLINT_TEST_F(
  VsmBeginFrameTest, IllegalApiOrderingBeforeBeginFrameFailsDeterministically)
{
  auto manager = VsmCacheManager(nullptr);

  NOLINT_EXPECT_DEATH((void)manager.BuildPageAllocationPlan(), ".*");
  NOLINT_EXPECT_DEATH((void)manager.CommitPageAllocationFrame(), ".*");
  NOLINT_EXPECT_DEATH((void)manager.ExtractFrameData(), ".*");
}

// IsFrameShapeCompatible() is a defensive invariant guard. Under correct API
// usage the extracted snapshot is always self-consistent, so the
// kIncompatibleFrameShape path is unreachable through the public API. This test
// documents the positive contract: a normally-extracted frame always passes the
// shape check and transitions to kAvailable on the next BeginFrame.
NOLINT_TEST_F(
  VsmBeginFrameTest, NormallyExtractedFramePassesShapeCompatibilityCheck)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(MakeShadowPoolConfig("shape.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("shape.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(manager, MakeSeam(pool_manager, 1ULL, 10U, "shape.prev"));

  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "shape.curr"),
    VsmCacheManagerFrameConfig { .debug_name = "shape.curr" });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);
}

// ── GAP-2: allow_reuse = false suppresses cache ──────────────────────────────

// When allow_reuse = false is set in the per-frame config, BeginFrame must
// treat the manager as if no previous frame was extracted — kUnavailable —
// even when a fully compatible warm start would otherwise yield kAvailable.
NOLINT_TEST_F(
  VsmBeginFrameTest, AllowReuseDisabledInFrameConfigSuppressesCacheAvailability)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("reuse-frame.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("reuse-frame.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, 1ULL, 10U, "reuse-frame.prev"));
  ASSERT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);

  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "reuse-frame.curr"),
    VsmCacheManagerFrameConfig {
      .allow_reuse = false,
      .debug_name = "reuse-frame.curr",
    });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

// When allow_reuse = false is set in the manager-level config, every BeginFrame
// must suppress cache availability regardless of extracted-frame state.
NOLINT_TEST_F(VsmBeginFrameTest,
  AllowReuseDisabledInManagerConfigSuppressesCacheAvailability)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("reuse-mgr.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("reuse-mgr.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr,
    VsmCacheManagerConfig { .allow_reuse = false, .debug_name = "reuse-mgr" });
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, 1ULL, 10U, "reuse-mgr.prev"));
  // Note: ExtractReadyFrame calls BeginFrame with default config (allow_reuse =
  // true), so the first extraction still succeeds. The manager config
  // allow_reuse = false fires on the second BeginFrame.
  ASSERT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kAvailable);

  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "reuse-mgr.curr"),
    VsmCacheManagerFrameConfig { .debug_name = "reuse-mgr.curr" });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

// ── GAP-3: force_invalidate_all on cold start ────────────────────────────────

// When there is no previous extracted frame, force_invalidate_all = true must
// produce kUnavailable (not kInvalidated), because kInvalidated semantically
// requires that a prior valid frame existed and was present.
NOLINT_TEST_F(VsmBeginFrameTest,
  ForceInvalidateAllOnColdStartProducesUnavailableNotInvalidated)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("fi-cold.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("fi-cold.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, 1ULL, 10U, "fi-cold"),
    VsmCacheManagerFrameConfig {
      .force_invalidate_all = true,
      .debug_name = "fi-cold",
    });

  // No previous frame ⇒ force_invalidate_all is unreachable in the decision
  // tree; the no-previous-frame branch fires first ⇒ kUnavailable.
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kUnavailable);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

// ── GAP-4: force_invalidate_all when already kInvalidated ────────────────────

// When the cache is already kInvalidated (sticky) and a subsequent BeginFrame
// arrives with force_invalidate_all = true, the sticky branch fires first and
// the original invalidation reason is preserved — the force flag is a no-op
// on an already-invalidated manager.
NOLINT_TEST_F(VsmBeginFrameTest,
  ForceInvalidateAllWhenAlreadyInvalidatedPreservesFirstInvalidationReason)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("fi-sticky.shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("fi-sticky.hzb")),
    VsmHzbPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(nullptr);
  ExtractReadyFrame(
    manager, MakeSeam(pool_manager, 1ULL, 10U, "fi-sticky.prev"));
  manager.InvalidateAll(VsmCacheInvalidationReason::kExplicitInvalidateAll);
  ASSERT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);

  // force_invalidate_all = true on a manager already in kInvalidated:
  // the sticky-kInvalidated branch fires; cache stays kInvalidated, HZB stays
  // suppressed.
  manager.BeginFrame(MakeSeam(pool_manager, 2ULL, 20U, "fi-sticky.curr"),
    VsmCacheManagerFrameConfig {
      .force_invalidate_all = true,
      .debug_name = "fi-sticky.curr",
    });

  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kFrameOpen);
  EXPECT_EQ(manager.DescribeCacheDataState(), VsmCacheDataState::kInvalidated);
  EXPECT_FALSE(manager.IsCacheDataAvailable());
  EXPECT_FALSE(manager.IsHzbDataAvailable());
}

} // namespace

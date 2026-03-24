//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>
#include <Oxygen/Testing/ScopedLogCapture.h>

namespace {

using oxygen::renderer::vsm::TryGetPageTableEntryIndex;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheInvalidationReason;
using oxygen::renderer::vsm::VsmCacheInvalidationScope;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmDirectionalClipmapDesc;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmPageInitializationAction;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::VsmCacheManagerTestBase;
using oxygen::testing::ScopedLogCapture;

class VsmCacheManagerInvalidationTest : public VsmCacheManagerTestBase {
protected:
  static auto MakeSinglePageRequest(const VsmVirtualMapLayout& layout)
    -> VsmPageRequest
  {
    return VsmPageRequest {
      .map_id = layout.id,
      .page = {},
      .flags = VsmPageRequestFlags::kRequired,
    };
  }

  static auto MakeSingleClipmapFrame(const std::uint64_t frame_generation,
    const std::uint32_t first_virtual_id, const char* debug_name,
    const char* remap_key) -> VsmVirtualAddressSpaceFrame
  {
    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .debug_name = debug_name,
      },
      frame_generation);
    address_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
      .remap_key = remap_key,
      .clip_level_count = 1,
      .pages_per_axis = 1,
      .page_grid_origin = { { 0, 0 } },
      .page_world_size = { 32.0F },
      .near_depth = { 1.0F },
      .far_depth = { 100.0F },
      .debug_name = remap_key,
    });
    return address_space.DescribeFrame();
  }

  static auto MakeSingleClipmapRequest(const VsmVirtualAddressSpaceFrame& frame)
    -> VsmPageRequest
  {
    const auto& layout = frame.directional_layouts.front();
    return VsmPageRequest {
      .map_id = layout.first_id,
      .page = { .level = 0, .page_x = 0, .page_y = 0 },
      .flags = VsmPageRequestFlags::kRequired,
    };
  }

  static auto FindInitializationAction(
    const oxygen::renderer::vsm::VsmPageAllocationPlan& plan,
    const std::uint32_t physical_page)
    -> std::optional<VsmPageInitializationAction>
  {
    for (const auto& item : plan.initialization_work) {
      if (item.physical_page.value == physical_page) {
        return item.action;
      }
    }

    return std::nullopt;
  }
};

NOLINT_TEST_F(VsmCacheManagerInvalidationTest,
  CacheManagerLocalLightInvalidationAffectsOnlyMatchingKeysAndDefersMutationUntilPlanning)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase7-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase7-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto previous_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase7-prev", 2U);
  const auto previous_requests = std::array {
    MakeSinglePageRequest(previous_frame.local_light_layouts[0]),
    MakeSinglePageRequest(previous_frame.local_light_layouts[1]),
  };

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase7-prev" });
  manager.SetPageRequests(previous_requests);
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  const auto extracted_before_invalidation = *manager.GetPreviousFrame();
  manager.InvalidateLocalLights({ "local-0" },
    VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(*manager.GetPreviousFrame(), extracted_before_invalidation);

  const auto current_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "phase7-curr", 2U);
  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase7-curr" });
  manager.SetPageRequests(std::array {
    MakeSinglePageRequest(current_frame.local_light_layouts[0]),
    MakeSinglePageRequest(current_frame.local_light_layouts[1]),
  });

  const auto& plan = manager.BuildPageAllocationPlan();
  ASSERT_EQ(plan.decisions.size(), 3U);
  EXPECT_EQ(plan.decisions[0].action, VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(plan.decisions[0].current_physical_page.value, 0U);
  EXPECT_EQ(plan.decisions[1].action, VsmAllocationAction::kInitializeOnly);
  EXPECT_EQ(plan.decisions[1].current_physical_page.value, 1U);
  EXPECT_EQ(plan.decisions[2].action, VsmAllocationAction::kEvict);
  EXPECT_EQ(plan.decisions[2].previous_physical_page.value, 0U);
  EXPECT_EQ(plan.initialized_page_count, 2U);
  EXPECT_EQ(plan.allocated_page_count, 1U);
  EXPECT_EQ(plan.reused_page_count, 1U);
  ASSERT_EQ(plan.initialization_work.size(), 2U);
  EXPECT_EQ(FindInitializationAction(plan, 0U),
    VsmPageInitializationAction::kCopyStaticSlice);
  EXPECT_EQ(FindInitializationAction(plan, 1U),
    VsmPageInitializationAction::kClearDepth);
}

NOLINT_TEST_F(VsmCacheManagerInvalidationTest,
  CacheManagerDirectionalInvalidationAffectsOnlyMatchingDirectionalKeys)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase7-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase7-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto previous_frame
    = MakeSingleClipmapFrame(1ULL, 10U, "phase7-prev", "sun-main");
  const auto previous_request = MakeSingleClipmapRequest(previous_frame);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase7-prev" });
  manager.SetPageRequests(std::array { previous_request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  manager.InvalidateDirectionalClipmaps({ "sun-main" },
    VsmCacheInvalidationScope::kStaticAndDynamic,
    VsmCacheInvalidationReason::kTargetedInvalidate);

  const auto current_frame
    = MakeSingleClipmapFrame(2ULL, 20U, "phase7-curr", "sun-main");
  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase7-curr" });
  manager.SetPageRequests(
    std::array { MakeSingleClipmapRequest(current_frame) });

  const auto& plan = manager.BuildPageAllocationPlan();
  ASSERT_EQ(plan.decisions.size(), 2U);
  EXPECT_EQ(plan.decisions[0].action, VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(plan.decisions[1].action, VsmAllocationAction::kEvict);
  EXPECT_EQ(plan.initialized_page_count, 1U);
  ASSERT_EQ(plan.initialization_work.size(), 1U);
  EXPECT_EQ(plan.initialization_work[0].action,
    VsmPageInitializationAction::kClearDepth);
}

NOLINT_TEST_F(VsmCacheManagerInvalidationTest,
  CacheManagerInvalidateAllClearsReuseEligibilityForTheNextFrame)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase7-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase7-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto previous_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase7-prev", 1U);
  const auto previous_request
    = MakeSinglePageRequest(previous_frame.local_light_layouts[0]);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase7-prev" });
  manager.SetPageRequests(std::array { previous_request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  manager.InvalidateAll(VsmCacheInvalidationReason::kExplicitInvalidateAll);

  const auto current_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "phase7-curr", 1U);
  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase7-curr" });
  manager.SetPageRequests(std::array {
    MakeSinglePageRequest(current_frame.local_light_layouts[0]),
  });

  const auto& plan = manager.BuildPageAllocationPlan();
  ASSERT_EQ(plan.decisions.size(), 2U);
  EXPECT_EQ(plan.decisions[0].action, VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(plan.decisions[1].action, VsmAllocationAction::kEvict);
  EXPECT_EQ(plan.reused_page_count, 0U);
  EXPECT_EQ(plan.allocated_page_count, 1U);
  EXPECT_EQ(plan.initialized_page_count, 1U);
}

NOLINT_TEST_F(VsmCacheManagerInvalidationTest,
  CacheManagerTargetedInvalidationLogsAggregateQueueAndApplyEvents)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase7-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase7-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto previous_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase7-prev", 1U);
  const auto previous_request
    = MakeSinglePageRequest(previous_frame.local_light_layouts[0]);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase7-prev" });
  manager.SetPageRequests(std::array { previous_request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  const auto saved_verbosity = loguru::g_global_verbosity;
  loguru::g_global_verbosity = 2;
  auto capture
    = ScopedLogCapture("phase7-targeted-invalidation-log", loguru::Verbosity_2);

  manager.InvalidateLocalLights({ "local-0" },
    VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);

  const auto current_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "phase7-curr", 1U);
  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase7-curr" });
  manager.SetPageRequests(std::array {
    MakeSinglePageRequest(current_frame.local_light_layouts[0]),
  });
  static_cast<void>(manager.BuildPageAllocationPlan());

  loguru::g_global_verbosity = saved_verbosity;

  EXPECT_EQ(capture.Count("queued targeted invalidation"), 1);
  EXPECT_EQ(capture.Count("applied targeted invalidations"), 1);
}

NOLINT_TEST_F(VsmCacheManagerInvalidationTest,
  CacheManagerRejectsMalformedTargetedInvalidationListsWithoutMutatingReuseState)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase7-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase7-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto previous_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase7-prev", 1U);
  const auto previous_request
    = MakeSinglePageRequest(previous_frame.local_light_layouts[0]);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase7-prev" });
  manager.SetPageRequests(std::array { previous_request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  auto capture
    = ScopedLogCapture("phase8-invalid-remap-list", loguru::Verbosity_WARNING);
  manager.InvalidateLocalLights({ "local-0", "local-0" },
    VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);
  EXPECT_EQ(capture.Count("rejecting targeted invalidation"), 1);

  const auto current_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "phase8-curr", 1U);
  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase8-curr" });
  manager.SetPageRequests(std::array {
    MakeSinglePageRequest(current_frame.local_light_layouts[0]),
  });

  const auto& plan = manager.BuildPageAllocationPlan();
  ASSERT_EQ(plan.decisions.size(), 1U);
  EXPECT_EQ(plan.decisions[0].action, VsmAllocationAction::kInitializeOnly);
}

NOLINT_TEST_F(VsmCacheManagerInvalidationTest,
  CacheManagerIgnoresTargetedInvalidationWithoutAnExtractedFrameAndLogsWarning)
{
  auto manager = VsmCacheManager(nullptr);
  auto capture
    = ScopedLogCapture("phase8-no-previous-frame", loguru::Verbosity_WARNING);

  manager.InvalidateLocalLights({ "local-0" },
    VsmCacheInvalidationScope::kDynamicOnly,
    VsmCacheInvalidationReason::kTargetedInvalidate);

  EXPECT_EQ(capture.Count("ignoring targeted invalidation"), 1);
}

} // namespace

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

namespace {

using oxygen::renderer::vsm::TryGetPageTableEntryIndex;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheBuildState;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmPageInitializationAction;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmPrimitiveIdentity;
using oxygen::renderer::vsm::VsmStaticPrimitivePageFeedbackRecord;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame;
using oxygen::renderer::vsm::testing::VsmCacheManagerTestBase;

class VsmCacheManagerOrchestrationTest : public VsmCacheManagerTestBase {
protected:
  static auto MakeSinglePageRequest(const VsmVirtualAddressSpaceFrame& frame,
    const std::size_t light_index = 0U) -> VsmPageRequest
  {
    return VsmPageRequest {
      .map_id = frame.local_light_layouts.at(light_index).id,
      .page = {},
      .flags = VsmPageRequestFlags::kRequired,
    };
  }

  static auto ResolveLocalPageTableEntryIndex(
    const VsmVirtualAddressSpaceFrame& frame,
    const std::size_t light_index = 0U) -> std::uint32_t
  {
    const auto entry_index = TryGetPageTableEntryIndex(
      frame.local_light_layouts.at(light_index), {});
    EXPECT_TRUE(entry_index.has_value());
    return entry_index.value_or(0U);
  }
};

NOLINT_TEST_F(VsmCacheManagerOrchestrationTest,
  CacheManagerBuildsPlannerBackedColdStartFrameFromCapturedRequests)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase5-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase5-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 3ULL, 100U, "phase5-frame", 2U);
  const auto requests = std::array {
    MakeSinglePageRequest(seam.current_frame, 0U),
    MakeSinglePageRequest(seam.current_frame, 1U),
  };

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "phase5-frame" });
  manager.SetPageRequests(requests);

  const auto& plan = manager.BuildPageAllocationPlan();
  EXPECT_EQ(manager.DescribeBuildState(), VsmCacheBuildState::kPlanned);
  ASSERT_EQ(plan.decisions.size(), requests.size());
  EXPECT_EQ(plan.allocated_page_count, 2U);
  EXPECT_EQ(plan.initialized_page_count, 2U);
  EXPECT_EQ(plan.reused_page_count, 0U);
  EXPECT_EQ(plan.evicted_page_count, 0U);
  EXPECT_EQ(plan.rejected_page_count, 0U);
  ASSERT_EQ(plan.initialization_work.size(), 2U);
  EXPECT_EQ(plan.initialization_work[0].action,
    VsmPageInitializationAction::kClearDepth);
  EXPECT_EQ(plan.initialization_work[1].action,
    VsmPageInitializationAction::kClearDepth);
  EXPECT_EQ(plan.decisions[0].action, VsmAllocationAction::kAllocateNew);
  EXPECT_EQ(plan.decisions[1].action, VsmAllocationAction::kAllocateNew);

  const auto& frame = manager.CommitPageAllocationFrame();
  ASSERT_TRUE(frame.is_ready);
  EXPECT_EQ(frame.plan, plan);
  EXPECT_EQ(frame.snapshot.virtual_frame, seam.current_frame);
  EXPECT_EQ(frame.snapshot.pool_identity, seam.physical_pool.pool_identity);
  EXPECT_EQ(frame.snapshot.page_table.size(),
    seam.current_frame.total_page_table_entry_count);
  EXPECT_EQ(
    frame.snapshot.physical_pages.size(), seam.physical_pool.tile_capacity);

  const auto first_entry
    = ResolveLocalPageTableEntryIndex(frame.snapshot.virtual_frame, 0U);
  const auto second_entry
    = ResolveLocalPageTableEntryIndex(frame.snapshot.virtual_frame, 1U);
  ASSERT_TRUE(frame.snapshot.page_table[first_entry].is_mapped);
  ASSERT_TRUE(frame.snapshot.page_table[second_entry].is_mapped);
  EXPECT_EQ(frame.snapshot.page_table[first_entry].physical_page.value, 0U);
  EXPECT_EQ(frame.snapshot.page_table[second_entry].physical_page.value, 1U);
  EXPECT_EQ(frame.snapshot.physical_pages[0].owner_id, requests[0].map_id);
  EXPECT_EQ(frame.snapshot.physical_pages[1].owner_id, requests[1].map_id);
  EXPECT_EQ(manager.GetCurrentFrame(), &frame);
  EXPECT_EQ(manager.GetPreviousFrame(), nullptr);
}

NOLINT_TEST_F(VsmCacheManagerOrchestrationTest,
  CacheManagerPreservesPreviousExtractionAndReusesCompatibleMappingsOnNextFrame)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase5-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase5-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto previous_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase5-prev", 1U);
  const auto previous_request = MakeSinglePageRequest(previous_frame);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase5-prev" });
  manager.SetPageRequests(std::array { previous_request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  const auto preserved_previous = *manager.GetPreviousFrame();

  const auto current_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "phase5-curr", 1U);
  const auto current_request = MakeSinglePageRequest(current_frame);
  manager.BeginFrame(MakeSeam(pool_manager, current_frame, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase5-curr" });
  manager.SetPageRequests(std::array { current_request });

  const auto& plan = manager.BuildPageAllocationPlan();
  ASSERT_EQ(plan.decisions.size(), 1U);
  EXPECT_EQ(plan.decisions[0].action, VsmAllocationAction::kInitializeOnly);
  EXPECT_EQ(plan.decisions[0].current_physical_page.value, 0U);
  EXPECT_EQ(plan.allocated_page_count, 0U);
  EXPECT_EQ(plan.initialized_page_count, 1U);
  EXPECT_EQ(plan.reused_page_count, 1U);
  EXPECT_EQ(plan.evicted_page_count, 0U);
  EXPECT_EQ(plan.rejected_page_count, 0U);
  ASSERT_EQ(plan.initialization_work.size(), 1U);
  EXPECT_EQ(plan.initialization_work[0].physical_page.value, 0U);
  EXPECT_EQ(plan.initialization_work[0].action,
    VsmPageInitializationAction::kClearDepth);

  const auto& frame = manager.CommitPageAllocationFrame();
  const auto current_entry
    = ResolveLocalPageTableEntryIndex(frame.snapshot.virtual_frame);
  ASSERT_TRUE(frame.snapshot.page_table[current_entry].is_mapped);
  EXPECT_EQ(frame.snapshot.page_table[current_entry].physical_page.value, 0U);
  EXPECT_EQ(frame.snapshot.physical_pages[0].owner_id, current_request.map_id);
  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(*manager.GetPreviousFrame(), preserved_previous);
}

NOLINT_TEST_F(VsmCacheManagerOrchestrationTest,
  CacheManagerPublishesVisiblePrimitiveAndStaticFeedbackIntoExtractedFrame)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("phase5-publish-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase5-publish-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto seam = MakeSeam(pool_manager, 5ULL, 100U, "phase5-publish", 1U);
  const auto request = MakeSinglePageRequest(seam.current_frame);

  auto manager = VsmCacheManager(nullptr);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "phase5-publish" });
  manager.SetPageRequests(std::array { request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  const auto& frame = manager.CommitPageAllocationFrame();

  const auto visible_primitives = std::array {
    VsmPrimitiveIdentity {
      .transform_index = 7U,
      .transform_generation = 9U,
      .submesh_index = 3U,
      .primitive_flags = 5U,
    },
  };
  const auto static_feedback = std::array {
    VsmStaticPrimitivePageFeedbackRecord {
      .primitive = visible_primitives[0],
      .page_table_index
      = ResolveLocalPageTableEntryIndex(frame.snapshot.virtual_frame),
      .physical_page = frame.plan.decisions[0].current_physical_page,
      .map_id = request.map_id,
      .virtual_page = request.page,
      .valid = 1U,
    },
  };

  manager.PublishVisibleShadowPrimitives(visible_primitives);
  manager.PublishStaticPrimitivePageFeedback(static_feedback);
  manager.ExtractFrameData();

  ASSERT_NE(manager.GetPreviousFrame(), nullptr);
  EXPECT_EQ(manager.GetPreviousFrame()->visible_shadow_primitives,
    std::vector<VsmPrimitiveIdentity>(
      visible_primitives.begin(), visible_primitives.end()));
  EXPECT_EQ(manager.GetPreviousFrame()->static_primitive_page_feedback,
    std::vector<VsmStaticPrimitivePageFeedbackRecord>(
      static_feedback.begin(), static_feedback.end()));
}

} // namespace

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerConfig;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmHzbPoolChangeResult;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPageRequestFlags;
using oxygen::renderer::vsm::VsmPhysicalPagePoolManager;
using oxygen::renderer::vsm::VsmPhysicalPoolChangeResult;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame;
using oxygen::renderer::vsm::testing::VsmCacheManagerTestBase;

class VsmCacheManagerRetentionTest : public VsmCacheManagerTestBase {
protected:
  static auto MakeSinglePageRequest(const VsmVirtualAddressSpaceFrame& frame)
    -> VsmPageRequest
  {
    return VsmPageRequest {
      .map_id = frame.local_light_layouts.front().id,
      .page = {},
      .flags = VsmPageRequestFlags::kRequired,
    };
  }
};

NOLINT_TEST_F(VsmCacheManagerRetentionTest,
  CacheManagerPublishesRetainedUnreferencedEntryWithFreshCurrentId)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase6-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase6-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto previous_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase6-prev", 1U);
  const auto previous_request = MakeSinglePageRequest(previous_frame);
  const auto previous_virtual_id = previous_request.map_id;

  auto manager = VsmCacheManager(nullptr,
    VsmCacheManagerConfig {
      .retained_unreferenced_frame_count = 1,
      .debug_name = "phase6-manager",
    });
  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase6-prev" });
  manager.SetPageRequests(std::array { previous_request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  const auto current_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "phase6-curr", 0U);
  const auto seam = MakeSeam(pool_manager, current_frame, &previous_frame);
  manager.BeginFrame(
    seam, VsmCacheManagerFrameConfig { .debug_name = "phase6-curr" });

  const auto& plan = manager.BuildPageAllocationPlan();
  EXPECT_EQ(plan.evicted_page_count, 0U);
  EXPECT_TRUE(plan.decisions.empty());

  const auto& frame = manager.CommitPageAllocationFrame();
  EXPECT_TRUE(frame.snapshot.virtual_frame.local_light_layouts.empty());
  EXPECT_EQ(frame.snapshot.virtual_frame, seam.current_frame);
  ASSERT_EQ(frame.snapshot.retained_local_light_layouts.size(), 1U);
  ASSERT_EQ(frame.snapshot.light_cache_entries.size(), 1U);
  EXPECT_EQ(frame.snapshot.page_table.size(), 1U);
  ASSERT_TRUE(frame.snapshot.page_table.front().is_mapped);
  EXPECT_EQ(frame.snapshot.page_table.front().physical_page.value, 0U);

  const auto& retained_layout
    = frame.snapshot.retained_local_light_layouts.front();
  const auto& retained_entry = frame.snapshot.light_cache_entries.front();
  EXPECT_EQ(retained_entry.remap_key, "local-0");
  EXPECT_TRUE(retained_entry.current_frame_state.is_retained_unreferenced);
  EXPECT_EQ(
    retained_entry.previous_frame_state.virtual_map_id, previous_virtual_id);
  EXPECT_EQ(
    retained_entry.current_frame_state.virtual_map_id, retained_layout.id);
  EXPECT_NE(
    retained_entry.current_frame_state.virtual_map_id, previous_virtual_id);
  EXPECT_EQ(retained_entry.current_frame_state.first_page_table_entry,
    retained_layout.first_page_table_entry);
  EXPECT_EQ(retained_entry.current_frame_state.page_table_entry_count, 1U);
  EXPECT_EQ(frame.snapshot.physical_pages[0].owner_id, retained_layout.id);
}

NOLINT_TEST_F(VsmCacheManagerRetentionTest,
  CacheManagerEvictsRetainedEntriesAfterConfiguredRetentionWindowExpires)
{
  auto pool_manager = VsmPhysicalPagePoolManager(nullptr);
  ASSERT_EQ(
    pool_manager.EnsureShadowPool(MakeShadowPoolConfig("phase6-shadow")),
    VsmPhysicalPoolChangeResult::kCreated);
  ASSERT_EQ(pool_manager.EnsureHzbPool(MakeHzbPoolConfig("phase6-hzb")),
    VsmHzbPoolChangeResult::kCreated);

  const auto previous_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "phase6-prev", 1U);
  const auto previous_request = MakeSinglePageRequest(previous_frame);

  auto manager = VsmCacheManager(nullptr,
    VsmCacheManagerConfig {
      .retained_unreferenced_frame_count = 1,
      .debug_name = "phase6-manager",
    });
  manager.BeginFrame(MakeSeam(pool_manager, previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase6-prev" });
  manager.SetPageRequests(std::array { previous_request });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  const auto empty_frame_a
    = MakeSinglePageLocalFrame(2ULL, 20U, "phase6-empty-a", 0U);
  manager.BeginFrame(MakeSeam(pool_manager, empty_frame_a, &previous_frame),
    VsmCacheManagerFrameConfig { .debug_name = "phase6-empty-a" });
  static_cast<void>(manager.BuildPageAllocationPlan());
  static_cast<void>(manager.CommitPageAllocationFrame());
  manager.ExtractFrameData();

  const auto extracted_retained = *manager.GetPreviousFrame();
  ASSERT_EQ(extracted_retained.retained_local_light_layouts.size(), 1U);

  const auto empty_frame_b
    = MakeSinglePageLocalFrame(3ULL, 30U, "phase6-empty-b", 0U);
  manager.BeginFrame(MakeSeam(pool_manager, empty_frame_b),
    VsmCacheManagerFrameConfig { .debug_name = "phase6-empty-b" });

  const auto& plan = manager.BuildPageAllocationPlan();
  EXPECT_EQ(plan.evicted_page_count, 1U);
  ASSERT_EQ(plan.decisions.size(), 1U);
  EXPECT_EQ(plan.decisions.front().action,
    oxygen::renderer::vsm::VsmAllocationAction::kEvict);

  const auto& frame = manager.CommitPageAllocationFrame();
  EXPECT_TRUE(frame.snapshot.retained_local_light_layouts.empty());
  EXPECT_TRUE(frame.snapshot.light_cache_entries.empty());
  EXPECT_TRUE(frame.snapshot.page_table.empty());
  EXPECT_FALSE(frame.snapshot.physical_pages[0].is_allocated);
}

} // namespace

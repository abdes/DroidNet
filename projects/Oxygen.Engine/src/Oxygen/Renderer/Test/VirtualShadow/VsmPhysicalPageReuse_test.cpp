//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>

#include "VirtualShadowStageGpuHarness.h"

namespace {

using oxygen::engine::VsmPageManagementFinalStage;
using oxygen::renderer::vsm::IsMapped;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::testing::VsmStageGpuHarness;

[[nodiscard]] auto IsReusePathAction(const VsmAllocationAction action) noexcept
  -> bool
{
  return action == VsmAllocationAction::kReuseExisting
    || action == VsmAllocationAction::kInitializeOnly;
}

class VsmPhysicalPageReuseTest : public VsmStageGpuHarness { };

NOLINT_TEST_F(VsmPhysicalPageReuseTest,
  AppliesOnlyReusableMappingsBeforePackingAndFreshAllocation)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("vsm-physical-page-reuse.shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto previous_virtual_frame = MakeSinglePageLocalFrame(1ULL, 10U);
  const auto previous_request = VsmPageRequest {
    .map_id = previous_virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, previous_virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-physical-page-reuse.previous" });
  manager.SetPageRequests({ &previous_request, 1U });
  static_cast<void>(CommitFrame(manager));
  static_cast<void>(manager.ExtractFrameData());

  const auto current_virtual_frame = MakeCustomLocalFrame(
    2ULL, 20U, { "local-0", "fresh-1" }, "vsm-physical-page-reuse.current");
  const auto current_requests = std::array {
    VsmPageRequest {
      .map_id = current_virtual_frame.local_light_layouts[0].id, .page = {} },
    VsmPageRequest {
      .map_id = current_virtual_frame.local_light_layouts[1].id, .page = {} },
  };

  manager.BeginFrame(
    MakeSeam(pool_manager, current_virtual_frame, &previous_virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-physical-page-reuse.current" });
  manager.SetPageRequests(current_requests);
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.decisions.size(), 2U);
  const auto reuse_it
    = std::find_if(frame.plan.decisions.begin(), frame.plan.decisions.end(),
      [](const auto& decision) { return IsReusePathAction(decision.action); });
  const auto allocate_it = std::find_if(frame.plan.decisions.begin(),
    frame.plan.decisions.end(), [](const auto& decision) {
      return decision.action == VsmAllocationAction::kAllocateNew;
    });
  ASSERT_NE(reuse_it, frame.plan.decisions.end());
  ASSERT_NE(allocate_it, frame.plan.decisions.end());

  const auto available_count_buffer = ExecutePageManagementPass(frame,
    VsmPageManagementFinalStage::kReuse, "vsm-physical-page-reuse.execute");
  ASSERT_NE(available_count_buffer, nullptr);

  const auto available_count = ReadBufferAs<std::uint32_t>(
    available_count_buffer, 1U, "vsm-physical-page-reuse.available-count");
  ASSERT_EQ(available_count.size(), 1U);
  EXPECT_EQ(available_count[0], 0U);

  const auto page_table
    = ReadBufferAs<VsmShaderPageTableEntry>(frame.page_table_buffer,
      frame.snapshot.page_table.size(), "vsm-physical-page-reuse.page-table");
  ASSERT_EQ(page_table.size(), 2U);
  EXPECT_TRUE(IsMapped(page_table[0]));
  EXPECT_FALSE(IsMapped(page_table[1]));

  const auto metadata = ReadBufferAs<VsmPhysicalPageMeta>(
    frame.physical_page_meta_buffer, frame.snapshot.physical_pages.size(),
    "vsm-physical-page-reuse.physical-meta");
  const auto& reused = metadata[reuse_it->current_physical_page.value];
  const auto& deferred = metadata[allocate_it->current_physical_page.value];
  EXPECT_TRUE(static_cast<bool>(reused.is_allocated));
  EXPECT_EQ(reused.owner_id, current_requests[0].map_id);
  EXPECT_FALSE(static_cast<bool>(deferred.is_allocated));
}

} // namespace

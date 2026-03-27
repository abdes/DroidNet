//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

#include "VirtualShadowStageGpuHarness.h"

namespace {

using oxygen::engine::VsmPageManagementFinalStage;
using oxygen::renderer::vsm::DecodePhysicalPageIndex;
using oxygen::renderer::vsm::IsMapped;
using oxygen::renderer::vsm::VsmAllocationAction;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmPhysicalPageMeta;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::VsmShaderPageTableEntry;
using oxygen::renderer::vsm::testing::VsmStageGpuHarness;

class VsmNewPageMappingTest : public VsmStageGpuHarness { };

NOLINT_TEST_F(VsmNewPageMappingTest,
  PublishesMixedReuseAndFreshMappingsForMultiLightRequests)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("vsm-new-page-mapping.shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto previous_virtual_frame
    = MakeSinglePageLocalFrame(1ULL, 10U, "previous", 1U);
  const auto previous_request = VsmPageRequest {
    .map_id = previous_virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, previous_virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-new-page-mapping.previous" });
  manager.SetPageRequests({ &previous_request, 1U });
  static_cast<void>(CommitFrame(manager));
  manager.ExtractFrameData();

  const auto current_virtual_frame
    = MakeSinglePageLocalFrame(2ULL, 20U, "current", 2U);
  const auto current_requests = std::array {
    VsmPageRequest {
      .map_id = current_virtual_frame.local_light_layouts[0].id, .page = {} },
    VsmPageRequest {
      .map_id = current_virtual_frame.local_light_layouts[1].id, .page = {} },
  };

  manager.BeginFrame(
    MakeSeam(pool_manager, current_virtual_frame, &previous_virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-new-page-mapping.current" });
  manager.SetPageRequests(current_requests);
  const auto& frame = CommitFrame(manager);

  ASSERT_EQ(frame.plan.decisions.size(), 2U);
  EXPECT_EQ(
    frame.plan.decisions[0].action, VsmAllocationAction::kInitializeOnly);
  EXPECT_EQ(frame.plan.decisions[1].action, VsmAllocationAction::kAllocateNew);

  static_cast<void>(ExecutePageManagementPass(frame,
    VsmPageManagementFinalStage::kAllocateNewPages,
    "vsm-new-page-mapping.execute"));

  const auto page_table
    = ReadBufferAs<VsmShaderPageTableEntry>(frame.page_table_buffer,
      frame.snapshot.page_table.size(), "vsm-new-page-mapping.page-table");
  ASSERT_EQ(page_table.size(), 2U);
  EXPECT_TRUE(IsMapped(page_table[0]));
  EXPECT_TRUE(IsMapped(page_table[1]));
  EXPECT_EQ(DecodePhysicalPageIndex(page_table[0]).value,
    frame.plan.decisions[0].current_physical_page.value);
  EXPECT_EQ(DecodePhysicalPageIndex(page_table[1]).value,
    frame.plan.decisions[1].current_physical_page.value);

  const auto page_flags
    = ReadBufferAs<VsmShaderPageFlags>(frame.page_flags_buffer,
      frame.snapshot.page_table.size(), "vsm-new-page-mapping.page-flags");
  const auto allocated_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated);
  const auto dynamic_uncached_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kDynamicUncached);
  const auto static_uncached_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached);
  EXPECT_NE(page_flags[0].bits & allocated_bit, 0U);
  EXPECT_NE(page_flags[1].bits & allocated_bit, 0U);
  EXPECT_NE(page_flags[1].bits & dynamic_uncached_bit, 0U);
  EXPECT_NE(page_flags[1].bits & static_uncached_bit, 0U);

  const auto metadata = ReadBufferAs<VsmPhysicalPageMeta>(
    frame.physical_page_meta_buffer, frame.snapshot.physical_pages.size(),
    "vsm-new-page-mapping.physical-meta");
  const auto& allocated
    = metadata[frame.plan.decisions[1].current_physical_page.value];
  EXPECT_TRUE(static_cast<bool>(allocated.is_allocated));
  EXPECT_EQ(allocated.owner_id, current_requests[1].map_id);
  EXPECT_EQ(allocated.owner_page, current_requests[1].page);
  EXPECT_TRUE(static_cast<bool>(allocated.view_uncached));
}

} // namespace

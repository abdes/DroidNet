//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>

#include "VirtualShadowStageGpuHarness.h"

namespace {

using oxygen::engine::VsmPageManagementFinalStage;
using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::testing::VsmStageGpuHarness;

class VsmAvailablePagePackingTest : public VsmStageGpuHarness { };

NOLINT_TEST_F(VsmAvailablePagePackingTest,
  CompactsFreePagesIntoDeterministicAscendingStackAfterReuse)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("vsm-available-page-packing.shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto previous_virtual_frame = MakeSinglePageLocalFrame(1ULL, 10U);
  const auto previous_request = VsmPageRequest {
    .map_id = previous_virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(MakeSeam(pool_manager, previous_virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-available-page-packing.previous" });
  manager.SetPageRequests({ &previous_request, 1U });
  static_cast<void>(CommitFrame(manager));
  manager.ExtractFrameData();

  const auto current_virtual_frame = MakeSinglePageLocalFrame(2ULL, 20U);
  const auto current_request = VsmPageRequest {
    .map_id = current_virtual_frame.local_light_layouts[0].id,
    .page = {},
  };

  manager.BeginFrame(
    MakeSeam(pool_manager, current_virtual_frame, &previous_virtual_frame),
    VsmCacheManagerFrameConfig {
      .debug_name = "vsm-available-page-packing.current" });
  manager.SetPageRequests({ &current_request, 1U });
  const auto& frame = CommitFrame(manager);

  const auto available_count_buffer = ExecutePageManagementPass(frame,
    VsmPageManagementFinalStage::kPackAvailablePages,
    "vsm-available-page-packing.execute");
  ASSERT_NE(available_count_buffer, nullptr);

  const auto count = ReadBufferAs<std::uint32_t>(
    available_count_buffer, 1U, "vsm-available-page-packing.count");
  ASSERT_EQ(count.size(), 1U);
  EXPECT_EQ(count[0], frame.snapshot.physical_pages.size() - 1U);

  const auto available_pages = ReadBufferAs<std::uint32_t>(
    frame.physical_page_list_buffer, 8U, "vsm-available-page-packing.list");
  const auto reused_page = frame.plan.decisions[0].current_physical_page.value;
  auto expected = std::array<std::uint32_t, 8> {};
  std::uint32_t cursor = 0U;
  for (std::uint32_t page = 0U; cursor < expected.size(); ++page) {
    if (page == reused_page) {
      continue;
    }
    expected[cursor++] = page;
  }
  EXPECT_EQ(available_pages,
    std::vector<std::uint32_t>(expected.begin(), expected.end()));
}

} // namespace

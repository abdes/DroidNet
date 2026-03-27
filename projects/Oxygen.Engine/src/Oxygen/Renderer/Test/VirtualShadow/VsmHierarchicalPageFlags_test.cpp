//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmShaderTypes.h>

#include "VirtualShadowStageGpuHarness.h"

namespace {

using oxygen::renderer::vsm::VsmCacheManager;
using oxygen::renderer::vsm::VsmCacheManagerFrameConfig;
using oxygen::renderer::vsm::VsmPageRequest;
using oxygen::renderer::vsm::VsmShaderPageFlagBits;
using oxygen::renderer::vsm::VsmShaderPageFlags;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::VsmStageGpuHarness;

class VsmHierarchicalPageFlagsTest : public VsmStageGpuHarness { };

NOLINT_TEST_F(VsmHierarchicalPageFlagsTest,
  BuildsAllocatedAndUncachedFlagsAcrossRequestedLeafAndAncestorPages)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("vsm-hierarchical-page-flags.shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame = MakeMultiLevelLocalFrame(1ULL, 10U,
    "local-propagate", 3U, 2U, 2U, "vsm-hierarchical-page-flags.frame");
  const auto& layout = virtual_frame.local_light_layouts[0];
  const auto requests = MakePageRequests(layout.id,
    {
      VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U },
      VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 1U },
      VsmVirtualPageCoord { .level = 1U, .page_x = 1U, .page_y = 0U },
    });

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "vsm-hierarchical-page-flags" });
  manager.SetPageRequests(requests);
  const auto& frame = CommitFrame(manager);

  ExecutePageManagementPass(frame,
    oxygen::engine::VsmPageManagementFinalStage::kAllocateNewPages,
    "vsm-hierarchical-page-flags.management");
  ExecutePropagationPass(frame, "vsm-hierarchical-page-flags.propagation");

  const auto page_flags
    = ReadBufferAs<VsmShaderPageFlags>(frame.page_flags_buffer,
      frame.snapshot.page_table.size(), "vsm-hierarchical-page-flags.readback");
  ASSERT_EQ(page_flags.size(), layout.total_page_count);

  const auto allocated_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kAllocated);
  const auto dynamic_uncached_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kDynamicUncached);
  const auto static_uncached_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kStaticUncached);
  const auto hierarchical_bits
    = allocated_bit | dynamic_uncached_bit | static_uncached_bit;
  const auto has_hierarchical_flags = [&](const VsmVirtualPageCoord& page) {
    return (page_flags[ResolvePageTableEntryIndex(layout, page)].bits
             & hierarchical_bits)
      == hierarchical_bits;
  };
  const auto has_no_hierarchical_flags = [&](const VsmVirtualPageCoord& page) {
    return (page_flags[ResolvePageTableEntryIndex(layout, page)].bits
             & hierarchical_bits)
      == 0U;
  };

  EXPECT_TRUE(has_hierarchical_flags(
    VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U }));
  EXPECT_TRUE(has_hierarchical_flags(
    VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 1U }));
  EXPECT_TRUE(has_hierarchical_flags(
    VsmVirtualPageCoord { .level = 1U, .page_x = 0U, .page_y = 0U }));
  EXPECT_TRUE(has_hierarchical_flags(
    VsmVirtualPageCoord { .level = 1U, .page_x = 1U, .page_y = 0U }));
  EXPECT_TRUE(has_hierarchical_flags(
    VsmVirtualPageCoord { .level = 1U, .page_x = 1U, .page_y = 1U }));
  EXPECT_TRUE(has_hierarchical_flags(
    VsmVirtualPageCoord { .level = 2U, .page_x = 0U, .page_y = 0U }));
  EXPECT_TRUE(has_hierarchical_flags(
    VsmVirtualPageCoord { .level = 2U, .page_x = 1U, .page_y = 0U }));
  EXPECT_TRUE(has_hierarchical_flags(
    VsmVirtualPageCoord { .level = 2U, .page_x = 1U, .page_y = 1U }));

  EXPECT_TRUE(has_no_hierarchical_flags(
    VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 0U }));
  EXPECT_TRUE(has_no_hierarchical_flags(
    VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 1U }));
  EXPECT_TRUE(has_no_hierarchical_flags(
    VsmVirtualPageCoord { .level = 1U, .page_x = 0U, .page_y = 1U }));
  EXPECT_TRUE(has_no_hierarchical_flags(
    VsmVirtualPageCoord { .level = 2U, .page_x = 0U, .page_y = 1U }));
}

} // namespace

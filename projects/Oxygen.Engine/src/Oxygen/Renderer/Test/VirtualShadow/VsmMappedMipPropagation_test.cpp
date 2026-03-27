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

class VsmMappedMipPropagationTest : public VsmStageGpuHarness { };

NOLINT_TEST_F(VsmMappedMipPropagationTest,
  MarksMappedDescendantsAcrossRequestedLeafAndParentPages)
{
  auto pool_manager
    = oxygen::renderer::vsm::VsmPhysicalPagePoolManager(&Backend());
  ASSERT_EQ(pool_manager.EnsureShadowPool(
              MakeShadowPoolConfig("vsm-mapped-mip-propagation.shadow")),
    oxygen::renderer::vsm::VsmPhysicalPoolChangeResult::kCreated);

  auto manager = VsmCacheManager(&Backend());
  const auto virtual_frame = MakeMultiLevelLocalFrame(1ULL, 10U,
    "local-propagate", 3U, 2U, 2U, "vsm-mapped-mip-propagation.frame");
  const auto& layout = virtual_frame.local_light_layouts[0];
  const auto requests = MakePageRequests(layout.id,
    {
      VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U },
      VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 1U },
      VsmVirtualPageCoord { .level = 1U, .page_x = 1U, .page_y = 0U },
    });

  manager.BeginFrame(MakeSeam(pool_manager, virtual_frame),
    VsmCacheManagerFrameConfig { .debug_name = "vsm-mapped-mip-propagation" });
  manager.SetPageRequests(requests);
  const auto& frame = CommitFrame(manager);

  ExecutePageManagementPass(frame,
    oxygen::engine::VsmPageManagementFinalStage::kAllocateNewPages,
    "vsm-mapped-mip-propagation.management");
  ExecutePropagationPass(frame, "vsm-mapped-mip-propagation.propagation");

  const auto page_flags
    = ReadBufferAs<VsmShaderPageFlags>(frame.page_flags_buffer,
      frame.snapshot.page_table.size(), "vsm-mapped-mip-propagation.readback");
  ASSERT_EQ(page_flags.size(), layout.total_page_count);

  const auto mapped_descendant_bit
    = static_cast<std::uint32_t>(VsmShaderPageFlagBits::kMappedDescendant);
  const auto has_mapped_descendant = [&](const VsmVirtualPageCoord& page) {
    return (page_flags[ResolvePageTableEntryIndex(layout, page)].bits
             & mapped_descendant_bit)
      != 0U;
  };

  EXPECT_FALSE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U }));
  EXPECT_FALSE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 1U }));
  EXPECT_FALSE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 1U, .page_x = 1U, .page_y = 0U }));

  EXPECT_TRUE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 1U, .page_x = 0U, .page_y = 0U }));
  EXPECT_TRUE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 1U, .page_x = 1U, .page_y = 1U }));
  EXPECT_TRUE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 2U, .page_x = 0U, .page_y = 0U }));
  EXPECT_TRUE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 2U, .page_x = 1U, .page_y = 0U }));
  EXPECT_TRUE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 2U, .page_x = 1U, .page_y = 1U }));

  EXPECT_FALSE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 0U, .page_x = 1U, .page_y = 0U }));
  EXPECT_FALSE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 1U }));
  EXPECT_FALSE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 1U, .page_x = 0U, .page_y = 1U }));
  EXPECT_FALSE(has_mapped_descendant(
    VsmVirtualPageCoord { .level = 2U, .page_x = 0U, .page_y = 1U }));
}

} // namespace

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowTestFixtures.h"

namespace {

using oxygen::renderer::vsm::to_string;
using oxygen::renderer::vsm::VsmLocalLightDesc;
using oxygen::renderer::vsm::VsmReuseRejectionReason;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::testing::VirtualShadowTest;

class VsmVirtualAddressSpaceIdsTest : public VirtualShadowTest { };

NOLINT_TEST_F(VsmVirtualAddressSpaceIdsTest,
  VirtualAddressSpaceResetsIdAllocatorAndPublishesStableFrameSnapshots)
{
  auto address_space = VsmVirtualAddressSpace {};

  VsmVirtualAddressSpaceConfig config {};
  config.first_virtual_id = 7;
  config.debug_name = "phase0-vsm-address-space";

  address_space.BeginFrame(config, 11ULL);
  const auto first_layout
    = address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {});
  auto paged_desc = VsmLocalLightDesc {
    .level_count = 2,
    .pages_per_level_x = 3,
    .pages_per_level_y = 2,
    .debug_name = "paged-light",
  };
  const auto second_layout = address_space.AllocatePagedLocalLight(paged_desc);
  const auto previous_frame = address_space.DescribeFrame();

  EXPECT_EQ(first_layout.id, 7U);
  EXPECT_EQ(second_layout.id, 8U);
  EXPECT_EQ(previous_frame.frame_generation, 11ULL);
  EXPECT_EQ(previous_frame.local_light_layouts.size(), 2U);
  EXPECT_EQ(previous_frame.total_page_table_entry_count, 13U);

  address_space.BeginFrame(config, 12ULL);
  const auto reset_layout
    = address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {});

  EXPECT_EQ(reset_layout.id, 7U);
  EXPECT_EQ(previous_frame.frame_generation, 11ULL);
  EXPECT_EQ(address_space.DescribeFrame().frame_generation, 12ULL);
  EXPECT_STREQ(to_string(VsmReuseRejectionReason::kNone), "None");
}

NOLINT_TEST_F(
  VsmVirtualAddressSpaceIdsTest, VirtualAddressSpaceAllocatesMonotonicIds)
{
  auto address_space = VsmVirtualAddressSpace {};
  address_space.BeginFrame(
    VsmVirtualAddressSpaceConfig { .first_virtual_id = 3 }, 1ULL);

  const auto first
    = address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {});
  const auto second
    = address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {});
  const auto third = address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
    .level_count = 2,
    .pages_per_level_x = 2,
    .pages_per_level_y = 2,
  });

  EXPECT_EQ(first.id, 3U);
  EXPECT_EQ(second.id, 4U);
  EXPECT_EQ(third.id, 5U);
}

NOLINT_TEST_F(
  VsmVirtualAddressSpaceIdsTest, VirtualAddressSpaceRejectsZeroFirstVirtualId)
{
  auto address_space = VsmVirtualAddressSpace {};

  EXPECT_THROW(static_cast<void>(address_space.BeginFrame(
                 VsmVirtualAddressSpaceConfig { .first_virtual_id = 0 }, 1ULL)),
    std::invalid_argument);
}

} // namespace

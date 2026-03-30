//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <stdexcept>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowStageCpuHarness.h"

namespace {

using oxygen::renderer::vsm::TotalPageCount;
using oxygen::renderer::vsm::VsmDirectionalClipmapDesc;
using oxygen::renderer::vsm::VsmLocalLightDesc;
using oxygen::renderer::vsm::VsmSinglePageLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

class VsmVirtualAddressSpaceIdsTest : public VsmStageCpuHarness { };

NOLINT_TEST_F(VsmVirtualAddressSpaceIdsTest,
  BeginFrameRebuildsFromConfiguredBaseAndKeepsPreviousFrameSnapshotStable)
{
  auto address_space = VsmVirtualAddressSpace {};

  const auto previous_config = VsmVirtualAddressSpaceConfig {
    .first_virtual_id = 41U,
    .clipmap_reuse_config =
      {
        .max_page_offset_x = 4,
        .max_page_offset_y = 4,
        .depth_range_epsilon = 0.01F,
        .page_world_size_epsilon = 0.01F,
      },
    .debug_name = "vsm-virtual-address-space.previous",
  };
  address_space.BeginFrame(previous_config, 11ULL);

  const auto previous_sun
    = address_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
      .remap_key = "sun-main",
      .clip_level_count = 2U,
      .pages_per_axis = 2U,
      .page_grid_origin = { { 0, 0 }, { 1, -1 } },
      .page_world_size = { 32.0F, 64.0F },
      .near_depth = { 1.0F, 2.0F },
      .far_depth = { 100.0F, 200.0F },
      .debug_name = "sun-main",
    });
  const auto previous_spot
    = address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
      .remap_key = "hero-spot",
      .level_count = 2U,
      .pages_per_level_x = 3U,
      .pages_per_level_y = 2U,
      .debug_name = "hero-spot",
    });
  const auto previous_frame = address_space.DescribeFrame();

  address_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 90U,
      .debug_name = "vsm-virtual-address-space.current",
    },
    12ULL);
  const auto current_fill
    = address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {
      .remap_key = "fill-distant",
      .debug_name = "fill-distant",
    });

  EXPECT_EQ(previous_frame.frame_generation, 11ULL);
  EXPECT_EQ(previous_frame.config, previous_config);
  ASSERT_EQ(previous_frame.directional_layouts.size(), 1U);
  ASSERT_EQ(previous_frame.local_light_layouts.size(), 1U);
  EXPECT_EQ(previous_frame.directional_layouts[0], previous_sun);
  EXPECT_EQ(previous_frame.local_light_layouts[0], previous_spot);
  EXPECT_EQ(previous_frame.total_page_table_entry_count,
    TotalPageCount(previous_sun) + previous_spot.total_page_count);

  const auto& current_frame = address_space.DescribeFrame();
  EXPECT_EQ(current_frame.frame_generation, 12ULL);
  EXPECT_EQ(current_frame.config.first_virtual_id, 90U);
  ASSERT_EQ(current_frame.local_light_layouts.size(), 1U);
  EXPECT_EQ(current_fill.id, 90U);
  EXPECT_EQ(current_frame.local_light_layouts[0].remap_key, "fill-distant");
  EXPECT_EQ(current_frame.total_page_table_entry_count, 1U);
}

NOLINT_TEST_F(VsmVirtualAddressSpaceIdsTest,
  RejectsZeroFirstVirtualIdBeforeAnyLayoutAllocationBegins)
{
  auto address_space = VsmVirtualAddressSpace {};

  EXPECT_THROW(static_cast<void>(address_space.BeginFrame(
                 VsmVirtualAddressSpaceConfig {
                   .first_virtual_id = 0U,
                   .debug_name = "vsm-virtual-address-space.invalid-zero-id",
                 },
                 1ULL)),
    std::invalid_argument);
}

} // namespace

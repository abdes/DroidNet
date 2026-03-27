//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowStageCpuHarness.h"

namespace {

using oxygen::renderer::vsm::PageCountPerClipLevel;
using oxygen::renderer::vsm::PageCountPerLevel;
using oxygen::renderer::vsm::TotalPageCount;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::DirectionalStageClipmapSpec;
using oxygen::renderer::vsm::testing::LocalStageLightSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

class VsmVirtualAddressAllocationTest : public VsmStageCpuHarness { };

NOLINT_TEST_F(VsmVirtualAddressAllocationTest,
  PublishesContiguousVirtualIdBlocksAndPageTableRangesForMixedLightLayouts)
{
  const auto directional_specs = std::array {
    DirectionalStageClipmapSpec {
      .remap_key = "sun-main",
      .clip_level_count = 3U,
      .pages_per_axis = 4U,
      .page_grid_origin = { { 0, 0 }, { 2, -2 }, { 4, -4 } },
      .page_world_size = { 32.0F, 64.0F, 128.0F },
      .near_depth = { 1.0F, 2.0F, 4.0F },
      .far_depth = { 100.0F, 200.0F, 400.0F },
    },
    DirectionalStageClipmapSpec {
      .remap_key = "sun-fill",
      .clip_level_count = 2U,
      .pages_per_axis = 2U,
      .page_grid_origin = { { 1, 1 }, { 2, 2 } },
      .page_world_size = { 48.0F, 96.0F },
      .near_depth = { 0.5F, 1.5F },
      .far_depth = { 80.0F, 160.0F },
    },
  };
  const auto local_specs = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-spot",
      .level_count = 3U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
    LocalStageLightSpec {
      .remap_key = "fill-distant",
      .level_count = 1U,
      .pages_per_level_x = 1U,
      .pages_per_level_y = 1U,
    },
  };

  const auto frame = MakeFrame(17ULL, 24U, directional_specs, local_specs,
    "vsm-virtual-address-allocation.mixed");

  ASSERT_EQ(frame.frame_generation, 17ULL);
  EXPECT_EQ(frame.config.first_virtual_id, 24U);
  ASSERT_EQ(frame.directional_layouts.size(), 2U);
  ASSERT_EQ(frame.local_light_layouts.size(), 2U);

  const auto& primary_sun = frame.directional_layouts[0];
  const auto& fill_sun = frame.directional_layouts[1];
  const auto& hero_spot = frame.local_light_layouts[0];
  const auto& distant_fill = frame.local_light_layouts[1];

  EXPECT_EQ(primary_sun.remap_key, "sun-main");
  EXPECT_EQ(primary_sun.first_id, 24U);
  EXPECT_EQ(primary_sun.clip_level_count, 3U);
  EXPECT_EQ(PageCountPerClipLevel(primary_sun), 16U);
  EXPECT_EQ(TotalPageCount(primary_sun), 48U);
  EXPECT_EQ(primary_sun.first_page_table_entry, 0U);

  EXPECT_EQ(fill_sun.remap_key, "sun-fill");
  EXPECT_EQ(fill_sun.first_id, 27U);
  EXPECT_EQ(fill_sun.clip_level_count, 2U);
  EXPECT_EQ(PageCountPerClipLevel(fill_sun), 4U);
  EXPECT_EQ(TotalPageCount(fill_sun), 8U);
  EXPECT_EQ(fill_sun.first_page_table_entry, 48U);

  EXPECT_EQ(hero_spot.remap_key, "hero-spot");
  EXPECT_EQ(hero_spot.id, 29U);
  EXPECT_EQ(hero_spot.level_count, 3U);
  EXPECT_EQ(hero_spot.pages_per_level_x, 2U);
  EXPECT_EQ(hero_spot.pages_per_level_y, 2U);
  EXPECT_EQ(PageCountPerLevel(hero_spot), 4U);
  EXPECT_EQ(hero_spot.total_page_count, 12U);
  EXPECT_EQ(hero_spot.first_page_table_entry, 56U);

  EXPECT_EQ(distant_fill.remap_key, "fill-distant");
  EXPECT_EQ(distant_fill.id, 30U);
  EXPECT_EQ(distant_fill.total_page_count, 1U);
  EXPECT_EQ(distant_fill.first_page_table_entry, 68U);

  EXPECT_EQ(frame.total_page_table_entry_count, 69U);

  EXPECT_EQ(ResolveDirectionalEntryIndex(frame,
              VsmVirtualPageCoord { .level = 2U, .page_x = 3U, .page_y = 1U }),
    39U);
  EXPECT_EQ(
    ResolveDirectionalEntryIndex(frame,
      VsmVirtualPageCoord { .level = 1U, .page_x = 1U, .page_y = 1U }, 1U),
    55U);
  EXPECT_EQ(ResolveLocalEntryIndex(frame,
              VsmVirtualPageCoord { .level = 2U, .page_x = 1U, .page_y = 0U }),
    65U);
  EXPECT_EQ(
    ResolveLocalEntryIndex(frame,
      VsmVirtualPageCoord { .level = 0U, .page_x = 0U, .page_y = 0U }, 1U),
    68U);
}

NOLINT_TEST_F(VsmVirtualAddressAllocationTest,
  ConsumesDirectionalVirtualIdsAsPerClipLevelBlocksBeforeAllocatingLaterLights)
{
  const auto directional_specs = std::array {
    DirectionalStageClipmapSpec {
      .remap_key = "sun-near",
      .clip_level_count = 4U,
      .pages_per_axis = 2U,
      .page_grid_origin = { { 0, 0 }, { 1, 0 }, { 2, 0 }, { 3, 0 } },
      .page_world_size = { 16.0F, 32.0F, 64.0F, 128.0F },
      .near_depth = { 0.5F, 1.0F, 2.0F, 4.0F },
      .far_depth = { 50.0F, 100.0F, 200.0F, 400.0F },
    },
    DirectionalStageClipmapSpec {
      .remap_key = "sun-far",
      .clip_level_count = 1U,
      .pages_per_axis = 4U,
      .page_grid_origin = { { -2, 3 } },
      .page_world_size = { 256.0F },
      .near_depth = { 8.0F },
      .far_depth = { 800.0F },
    },
  };
  const auto local_specs = std::array {
    LocalStageLightSpec {
      .remap_key = "architectural-spot",
      .level_count = 2U,
      .pages_per_level_x = 3U,
      .pages_per_level_y = 2U,
    },
  };

  const auto frame = MakeFrame(23ULL, 60U, directional_specs, local_specs,
    "vsm-virtual-address-allocation.directional-blocks");

  ASSERT_EQ(frame.directional_layouts.size(), 2U);
  ASSERT_EQ(frame.local_light_layouts.size(), 1U);

  const auto& near_sun = frame.directional_layouts[0];
  const auto& far_sun = frame.directional_layouts[1];
  const auto& spot = frame.local_light_layouts[0];

  EXPECT_EQ(near_sun.first_id, 60U);
  EXPECT_EQ(near_sun.clip_level_count, 4U);
  EXPECT_EQ(far_sun.first_id, 64U);
  EXPECT_EQ(far_sun.clip_level_count, 1U);
  EXPECT_EQ(spot.id, 65U);

  EXPECT_EQ(near_sun.first_page_table_entry, 0U);
  EXPECT_EQ(TotalPageCount(near_sun), 16U);
  EXPECT_EQ(far_sun.first_page_table_entry, 16U);
  EXPECT_EQ(TotalPageCount(far_sun), 16U);
  EXPECT_EQ(spot.first_page_table_entry, 32U);
  EXPECT_EQ(spot.total_page_count, 12U);
  EXPECT_EQ(frame.total_page_table_entry_count, 44U);

  EXPECT_EQ(ResolveDirectionalEntryIndex(frame,
              VsmVirtualPageCoord { .level = 3U, .page_x = 1U, .page_y = 1U }),
    15U);
  EXPECT_EQ(
    ResolveDirectionalEntryIndex(frame,
      VsmVirtualPageCoord { .level = 0U, .page_x = 3U, .page_y = 2U }, 1U),
    27U);
  EXPECT_EQ(ResolveLocalEntryIndex(frame,
              VsmVirtualPageCoord { .level = 1U, .page_x = 2U, .page_y = 1U }),
    43U);
}

} // namespace

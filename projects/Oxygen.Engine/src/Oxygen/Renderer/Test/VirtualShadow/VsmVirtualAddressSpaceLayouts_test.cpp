//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <stdexcept>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

#include "VirtualShadowStageCpuHarness.h"

namespace {

using oxygen::renderer::vsm::PageCountPerClipLevel;
using oxygen::renderer::vsm::PageCountPerLevel;
using oxygen::renderer::vsm::TotalPageCount;
using oxygen::renderer::vsm::TryGetPageTableEntryIndex;
using oxygen::renderer::vsm::VsmDirectionalClipmapDesc;
using oxygen::renderer::vsm::VsmLocalLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::DirectionalStageClipmapSpec;
using oxygen::renderer::vsm::testing::LocalStageLightSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

class VsmVirtualAddressSpaceLayoutsTest : public VsmStageCpuHarness { };

NOLINT_TEST_F(VsmVirtualAddressSpaceLayoutsTest,
  PublishesSinglePageAndMultiLevelLocalLayoutsFromConcreteLightSpecs)
{
  const auto local_specs = std::array {
    LocalStageLightSpec {
      .remap_key = "distant-fill",
      .level_count = 1U,
      .pages_per_level_x = 1U,
      .pages_per_level_y = 1U,
    },
    LocalStageLightSpec {
      .remap_key = "hero-spot",
      .level_count = 2U,
      .pages_per_level_x = 4U,
      .pages_per_level_y = 2U,
    },
  };

  const auto frame = MakeLocalFrame(
    5ULL, 20U, local_specs, "vsm-virtual-address-space.local-layouts");

  ASSERT_EQ(frame.local_light_layouts.size(), 2U);
  EXPECT_TRUE(frame.directional_layouts.empty());
  EXPECT_EQ(frame.frame_generation, 5ULL);

  const auto& distant_fill = frame.local_light_layouts[0];
  const auto& hero_spot = frame.local_light_layouts[1];

  EXPECT_EQ(distant_fill.id, 20U);
  EXPECT_EQ(distant_fill.remap_key, "distant-fill");
  EXPECT_EQ(distant_fill.level_count, 1U);
  EXPECT_EQ(PageCountPerLevel(distant_fill), 1U);
  EXPECT_EQ(distant_fill.total_page_count, 1U);
  EXPECT_EQ(distant_fill.first_page_table_entry, 0U);

  EXPECT_EQ(hero_spot.id, 21U);
  EXPECT_EQ(hero_spot.remap_key, "hero-spot");
  EXPECT_EQ(hero_spot.level_count, 2U);
  EXPECT_EQ(hero_spot.pages_per_level_x, 4U);
  EXPECT_EQ(hero_spot.pages_per_level_y, 2U);
  EXPECT_EQ(PageCountPerLevel(hero_spot), 8U);
  EXPECT_EQ(hero_spot.total_page_count, 16U);
  EXPECT_EQ(hero_spot.first_page_table_entry, 1U);

  EXPECT_EQ(
    ResolveLocalEntryIndex(frame,
      VsmVirtualPageCoord { .level = 1U, .page_x = 3U, .page_y = 1U }, 1U),
    16U);
  EXPECT_EQ(frame.total_page_table_entry_count, 17U);
}

NOLINT_TEST_F(VsmVirtualAddressSpaceLayoutsTest,
  RejectsOutOfBoundsCoordinatesForPublishedLocalAndDirectionalLayouts)
{
  const auto directional_specs = std::array {
    DirectionalStageClipmapSpec {
      .remap_key = "sun-main",
      .clip_level_count = 2U,
      .pages_per_axis = 4U,
      .page_grid_origin = { { 0, 0 }, { 1, 1 } },
      .page_world_size = { 32.0F, 64.0F },
      .near_depth = { 1.0F, 2.0F },
      .far_depth = { 100.0F, 200.0F },
    },
  };
  const auto local_specs = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-spot",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 3U,
    },
  };

  const auto frame = MakeFrame(9ULL, 30U, directional_specs, local_specs,
    "vsm-virtual-address-space.bounds");

  ASSERT_EQ(frame.directional_layouts.size(), 1U);
  ASSERT_EQ(frame.local_light_layouts.size(), 1U);

  const auto& clipmap = frame.directional_layouts[0];
  const auto& local = frame.local_light_layouts[0];

  EXPECT_FALSE(TryGetPageTableEntryIndex(
    clipmap, VsmVirtualPageCoord { .level = 2U, .page_x = 0U, .page_y = 0U })
      .has_value());
  EXPECT_FALSE(TryGetPageTableEntryIndex(
    clipmap, VsmVirtualPageCoord { .level = 1U, .page_x = 4U, .page_y = 0U })
      .has_value());
  EXPECT_FALSE(TryGetPageTableEntryIndex(
    clipmap, VsmVirtualPageCoord { .level = 1U, .page_x = 0U, .page_y = 4U })
      .has_value());

  EXPECT_FALSE(TryGetPageTableEntryIndex(
    local, VsmVirtualPageCoord { .level = 2U, .page_x = 0U, .page_y = 0U })
      .has_value());
  EXPECT_FALSE(TryGetPageTableEntryIndex(
    local, VsmVirtualPageCoord { .level = 1U, .page_x = 2U, .page_y = 0U })
      .has_value());
  EXPECT_FALSE(TryGetPageTableEntryIndex(
    local, VsmVirtualPageCoord { .level = 1U, .page_x = 0U, .page_y = 3U })
      .has_value());

  EXPECT_EQ(PageCountPerClipLevel(clipmap), 16U);
  EXPECT_EQ(TotalPageCount(clipmap), 32U);
  EXPECT_EQ(PageCountPerLevel(local), 6U);
}

NOLINT_TEST_F(VsmVirtualAddressSpaceLayoutsTest,
  RejectsMalformedLocalAndDirectionalDescriptors)
{
  auto address_space = VsmVirtualAddressSpace {};
  address_space.BeginFrame(
    VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 10U,
      .debug_name = "vsm-virtual-address-space.invalid-layouts",
    },
    1ULL);

  EXPECT_THROW(
    static_cast<void>(address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
      .remap_key = "zero-level",
      .level_count = 0U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
      .debug_name = "zero-level",
    })),
    std::invalid_argument);
  EXPECT_THROW(
    static_cast<void>(address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
      .remap_key = "zero-pages-x",
      .level_count = 2U,
      .pages_per_level_x = 0U,
      .pages_per_level_y = 2U,
      .debug_name = "zero-pages-x",
    })),
    std::invalid_argument);
  EXPECT_THROW(
    static_cast<void>(address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
      .remap_key = "zero-pages-y",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 0U,
      .debug_name = "zero-pages-y",
    })),
    std::invalid_argument);

  EXPECT_THROW(static_cast<void>(address_space.AllocateDirectionalClipmap(
                 VsmDirectionalClipmapDesc {
                   .remap_key = "zero-clips",
                   .clip_level_count = 0U,
                   .pages_per_axis = 4U,
                   .page_grid_origin = {},
                   .page_world_size = {},
                   .near_depth = {},
                   .far_depth = {},
                   .debug_name = "zero-clips",
                 })),
    std::invalid_argument);
  EXPECT_THROW(static_cast<void>(address_space.AllocateDirectionalClipmap(
                 VsmDirectionalClipmapDesc {
                   .remap_key = "zero-pages",
                   .clip_level_count = 2U,
                   .pages_per_axis = 0U,
                   .page_grid_origin = { { 0, 0 }, { 1, 1 } },
                   .page_world_size = { 32.0F, 64.0F },
                   .near_depth = { 1.0F, 2.0F },
                   .far_depth = { 100.0F, 200.0F },
                   .debug_name = "zero-pages",
                 })),
    std::invalid_argument);
  EXPECT_THROW(static_cast<void>(address_space.AllocateDirectionalClipmap(
                 VsmDirectionalClipmapDesc {
                   .remap_key = "mismatched-metadata",
                   .clip_level_count = 2U,
                   .pages_per_axis = 4U,
                   .page_grid_origin = { { 0, 0 } },
                   .page_world_size = { 32.0F, 64.0F },
                   .near_depth = { 1.0F, 2.0F },
                   .far_depth = { 100.0F, 200.0F },
                   .debug_name = "mismatched-metadata",
                 })),
    std::invalid_argument);
}

} // namespace

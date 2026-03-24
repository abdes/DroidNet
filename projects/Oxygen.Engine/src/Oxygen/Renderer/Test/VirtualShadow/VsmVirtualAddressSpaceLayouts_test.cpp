//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <stdexcept>

#include <glm/vec2.hpp>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>

namespace {

using oxygen::renderer::vsm::PageCountPerClipLevel;
using oxygen::renderer::vsm::PageCountPerLevel;
using oxygen::renderer::vsm::TotalPageCount;
using oxygen::renderer::vsm::TryGetPageTableEntryIndex;
using oxygen::renderer::vsm::VsmClipmapLayout;
using oxygen::renderer::vsm::VsmDirectionalClipmapDesc;
using oxygen::renderer::vsm::VsmLocalLightDesc;
using oxygen::renderer::vsm::VsmVirtualAddressSpace;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::VsmVirtualPageCoord;
using oxygen::renderer::vsm::testing::VirtualShadowTest;

class VsmVirtualAddressSpaceLayoutsTest : public VirtualShadowTest { };

NOLINT_TEST_F(VsmVirtualAddressSpaceLayoutsTest,
  VirtualAddressSpaceCreatesSingleAndPagedLocalLightLayouts)
{
  auto address_space = VsmVirtualAddressSpace {};
  address_space.BeginFrame(VsmVirtualAddressSpaceConfig {}, 1ULL);

  const auto single_page
    = address_space.AllocateSinglePageLocalLight({ .debug_name = "single" });
  EXPECT_EQ(single_page.total_page_count, 1U);
  EXPECT_EQ(single_page.first_page_table_entry, 0U);
  EXPECT_EQ(PageCountPerLevel(single_page), 1U);
  const auto single_entry
    = TryGetPageTableEntryIndex(single_page, VsmVirtualPageCoord {});
  ASSERT_TRUE(single_entry.has_value());
  EXPECT_EQ(*single_entry, 0U);

  VsmLocalLightDesc desc {};
  desc.level_count = 2;
  desc.pages_per_level_x = 4;
  desc.pages_per_level_y = 2;

  const auto layout = address_space.AllocatePagedLocalLight(desc);
  EXPECT_EQ(layout.total_page_count, 16U);
  EXPECT_EQ(layout.first_page_table_entry, 1U);
  EXPECT_EQ(PageCountPerLevel(layout), 8U);
  const auto paged_entry = TryGetPageTableEntryIndex(
    layout, VsmVirtualPageCoord { .level = 1, .page_x = 3, .page_y = 1 });
  ASSERT_TRUE(paged_entry.has_value());
  EXPECT_EQ(*paged_entry, 16U);
  EXPECT_FALSE(TryGetPageTableEntryIndex(
    layout, VsmVirtualPageCoord { .level = 2, .page_x = 0, .page_y = 0 })
      .has_value());
}

NOLINT_TEST_F(VsmVirtualAddressSpaceLayoutsTest,
  VirtualAddressSpaceCreatesDirectionalClipmapLayouts)
{
  auto address_space = VsmVirtualAddressSpace {};
  address_space.BeginFrame(
    VsmVirtualAddressSpaceConfig { .first_virtual_id = 10 }, 2ULL);

  const auto layout
    = address_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
      .clip_level_count = 3,
      .pages_per_axis = 4,
      .page_grid_origin = { { 0, 0 }, { 1, 2 }, { 3, 4 } },
      .page_world_size = { 32.0F, 64.0F, 128.0F },
      .near_depth = { 1.0F, 2.0F, 4.0F },
      .far_depth = { 100.0F, 200.0F, 400.0F },
      .debug_name = "sun",
    });

  EXPECT_EQ(layout.first_id, 10U);
  EXPECT_EQ(layout.clip_level_count, 3U);
  EXPECT_EQ(PageCountPerClipLevel(layout), 16U);
  EXPECT_EQ(TotalPageCount(layout), 48U);
  const auto clipmap_entry = TryGetPageTableEntryIndex(
    layout, VsmVirtualPageCoord { .level = 2, .page_x = 3, .page_y = 1 });
  ASSERT_TRUE(clipmap_entry.has_value());
  EXPECT_EQ(*clipmap_entry, 39U);
  EXPECT_FALSE(TryGetPageTableEntryIndex(
    layout, VsmVirtualPageCoord { .level = 3, .page_x = 0, .page_y = 0 })
      .has_value());
  EXPECT_EQ(address_space.DescribeFrame().total_page_table_entry_count, 48U);
}

NOLINT_TEST_F(VsmVirtualAddressSpaceLayoutsTest,
  VirtualAddressSpaceFrameCopiesRemainStableAcrossFutureFrames)
{
  auto address_space = VsmVirtualAddressSpace {};
  address_space.BeginFrame(
    VsmVirtualAddressSpaceConfig { .first_virtual_id = 20 }, 5ULL);

  const auto first_layout
    = address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
      .level_count = 2, .pages_per_level_x = 2, .pages_per_level_y = 3 });
  const auto first_frame = address_space.DescribeFrame();

  address_space.BeginFrame(
    VsmVirtualAddressSpaceConfig { .first_virtual_id = 40 }, 6ULL);
  const auto second_layout = address_space.AllocateSinglePageLocalLight(
    { .debug_name = "next-frame" });

  EXPECT_EQ(first_frame.frame_generation, 5ULL);
  ASSERT_EQ(first_frame.local_light_layouts.size(), 1U);
  EXPECT_EQ(first_frame.local_light_layouts[0], first_layout);
  EXPECT_EQ(first_frame.total_page_table_entry_count, 12U);
  EXPECT_EQ(second_layout.id, 40U);
  EXPECT_EQ(address_space.DescribeFrame().frame_generation, 6ULL);
}

NOLINT_TEST_F(VsmVirtualAddressSpaceLayoutsTest,
  VirtualAddressSpaceRejectsMalformedPagedAndClipmapDescriptors)
{
  auto address_space = VsmVirtualAddressSpace {};
  address_space.BeginFrame(
    VsmVirtualAddressSpaceConfig { .first_virtual_id = 10 }, 1ULL);

  EXPECT_THROW(
    static_cast<void>(address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
      .level_count = 2,
      .pages_per_level_x = 0,
      .pages_per_level_y = 2,
    })),
    std::invalid_argument);

  EXPECT_THROW(static_cast<void>(address_space.AllocateDirectionalClipmap(
                 VsmDirectionalClipmapDesc {
                   .clip_level_count = 2,
                   .pages_per_axis = 4,
                   .page_grid_origin = { { 0, 0 } },
                   .page_world_size = { 32.0F, 64.0F },
                   .near_depth = { 1.0F, 2.0F },
                   .far_depth = { 100.0F, 200.0F },
                 })),
    std::invalid_argument);
}

NOLINT_TEST_F(VsmVirtualAddressSpaceLayoutsTest,
  VirtualLayoutValueTypesPreserveConfiguredValues)
{
  const auto expected_local_layout = VsmVirtualMapLayout {
    .id = 17,
    .level_count = 3,
    .pages_per_level_x = 4,
    .pages_per_level_y = 2,
    .total_page_count = 24,
    .first_page_table_entry = 5,
  };
  const auto copied_local_layout = expected_local_layout;
  EXPECT_EQ(copied_local_layout, expected_local_layout);

  const auto expected_clipmap_layout = VsmClipmapLayout {
    .first_id = 40,
    .clip_level_count = 2,
    .pages_per_axis = 8,
    .first_page_table_entry = 11,
    .page_grid_origin = { { 4, 5 }, { 6, 7 } },
    .page_world_size = { 32.0F, 64.0F },
    .near_depth = { 1.0F, 2.0F },
    .far_depth = { 100.0F, 200.0F },
  };
  const auto copied_clipmap_layout = expected_clipmap_layout;
  EXPECT_EQ(copied_clipmap_layout, expected_clipmap_layout);
}

} // namespace

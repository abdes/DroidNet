//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include "VirtualShadowTestFixtures.h"

#include <algorithm>

#include <glm/vec2.hpp>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualRemapBuilder.h>

namespace {

using oxygen::renderer::vsm::BuildVirtualRemapTable;
using oxygen::renderer::vsm::VsmClipmapLayout;
using oxygen::renderer::vsm::VsmClipmapReuseConfig;
using oxygen::renderer::vsm::VsmReuseRejectionReason;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig;
using oxygen::renderer::vsm::VsmVirtualAddressSpaceFrame;
using oxygen::renderer::vsm::VsmVirtualMapLayout;
using oxygen::renderer::vsm::VsmVirtualRemapEntry;
using oxygen::renderer::vsm::testing::VirtualShadowTest;

auto MakeConfig() -> VsmVirtualAddressSpaceConfig
{
  return VsmVirtualAddressSpaceConfig {
    .first_virtual_id = 1,
    .clipmap_reuse_config = {
      .max_page_offset_x = 4,
      .max_page_offset_y = 4,
      .depth_range_epsilon = 0.01F,
      .page_world_size_epsilon = 0.01F,
    },
    .debug_name = "phase5-frame",
  };
}

auto FindEntry(const std::vector<VsmVirtualRemapEntry>& entries,
  const std::uint32_t previous_id) -> const VsmVirtualRemapEntry*
{
  const auto it = std::find_if(entries.begin(), entries.end(),
    [previous_id](const VsmVirtualRemapEntry& entry) {
      return entry.previous_id == previous_id;
    });
  return it == entries.end() ? nullptr : &*it;
}

class VsmVirtualRemapBuilderTest : public VirtualShadowTest { };

NOLINT_TEST_F(
  VsmVirtualRemapBuilderTest, RemapTableMapsRetainedEntriesExactlyOnce)
{
  const auto previous_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 1,
    .config = MakeConfig(),
    .total_page_table_entry_count = 40,
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 10,
        .remap_key = "local-a",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
        .first_page_table_entry = 0,
      },
      VsmVirtualMapLayout {
        .id = 11,
        .remap_key = "local-b",
        .level_count = 2,
        .pages_per_level_x = 2,
        .pages_per_level_y = 2,
        .total_page_count = 8,
        .first_page_table_entry = 1,
      },
    },
    .directional_layouts = {
      VsmClipmapLayout {
        .first_id = 20,
        .remap_key = "sun",
        .clip_level_count = 2,
        .pages_per_axis = 4,
        .first_page_table_entry = 9,
        .page_grid_origin = { { 0, 0 }, { 4, 4 } },
        .page_world_size = { 32.0F, 64.0F },
        .near_depth = { 1.0F, 2.0F },
        .far_depth = { 101.0F, 202.0F },
      },
    },
  };
  const auto current_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 2,
    .config = MakeConfig(),
    .total_page_table_entry_count = 40,
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 101,
        .remap_key = "local-a",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
        .first_page_table_entry = 0,
      },
      VsmVirtualMapLayout {
        .id = 102,
        .remap_key = "local-b",
        .level_count = 2,
        .pages_per_level_x = 2,
        .pages_per_level_y = 2,
        .total_page_count = 8,
        .first_page_table_entry = 1,
      },
    },
    .directional_layouts = {
      VsmClipmapLayout {
        .first_id = 200,
        .remap_key = "sun",
        .clip_level_count = 2,
        .pages_per_axis = 4,
        .first_page_table_entry = 9,
        .page_grid_origin = { { 1, -1 }, { 5, 3 } },
        .page_world_size = { 32.0F, 64.0F },
        .near_depth = { 1.0F, 2.0F },
        .far_depth = { 101.0F, 202.0F },
      },
    },
  };

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 4U);
  const auto* local_a = FindEntry(table.entries, 10);
  const auto* local_b = FindEntry(table.entries, 11);
  const auto* sun0 = FindEntry(table.entries, 20);
  const auto* sun1 = FindEntry(table.entries, 21);
  ASSERT_NE(local_a, nullptr);
  ASSERT_NE(local_b, nullptr);
  ASSERT_NE(sun0, nullptr);
  ASSERT_NE(sun1, nullptr);
  EXPECT_EQ(local_a->current_id, 101U);
  EXPECT_EQ(local_b->current_id, 102U);
  EXPECT_EQ(local_a->rejection_reason, VsmReuseRejectionReason::kNone);
  EXPECT_EQ(local_b->rejection_reason, VsmReuseRejectionReason::kNone);
  EXPECT_EQ(sun0->current_id, 200U);
  EXPECT_EQ(sun1->current_id, 201U);
  EXPECT_EQ(sun0->page_offset, glm::ivec2(1, -1));
  EXPECT_EQ(sun1->page_offset, glm::ivec2(1, -1));
  EXPECT_EQ(sun0->rejection_reason, VsmReuseRejectionReason::kNone);
  EXPECT_EQ(sun1->rejection_reason, VsmReuseRejectionReason::kNone);
}

NOLINT_TEST_F(
  VsmVirtualRemapBuilderTest, RemapMatchingUsesStableKeysNotAllocationOrder)
{
  const auto previous_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 1,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 5,
        .remap_key = "local-a",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
      },
      VsmVirtualMapLayout {
        .id = 6,
        .remap_key = "local-b",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
      },
    },
  };
  const auto current_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 2,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 60,
        .remap_key = "local-b",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
      },
      VsmVirtualMapLayout {
        .id = 50,
        .remap_key = "local-a",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
      },
    },
  };

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);
  const auto* local_a = FindEntry(table.entries, 5);
  const auto* local_b = FindEntry(table.entries, 6);
  ASSERT_NE(local_a, nullptr);
  ASSERT_NE(local_b, nullptr);
  EXPECT_EQ(local_a->current_id, 50U);
  EXPECT_EQ(local_b->current_id, 60U);
}

NOLINT_TEST_F(VsmVirtualRemapBuilderTest,
  FreshIdsForRetainedEntriesStillPermitRemapBookkeeping)
{
  const auto previous_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 10,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 30,
        .remap_key = "hero-light",
        .level_count = 3,
        .pages_per_level_x = 2,
        .pages_per_level_y = 2,
        .total_page_count = 12,
      },
    },
  };
  const auto current_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 11,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 77,
        .remap_key = "hero-light",
        .level_count = 3,
        .pages_per_level_x = 2,
        .pages_per_level_y = 2,
        .total_page_count = 12,
      },
    },
  };

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 1U);
  EXPECT_EQ(table.entries[0].previous_id, 30U);
  EXPECT_EQ(table.entries[0].current_id, 77U);
  EXPECT_EQ(table.entries[0].rejection_reason, VsmReuseRejectionReason::kNone);
}

NOLINT_TEST_F(VsmVirtualRemapBuilderTest,
  DuplicateCurrentLocalLightRemapKeysReportExplicitRejection)
{
  const auto previous_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 1,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 10,
        .remap_key = "hero",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
      },
    },
  };
  const auto current_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 2,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 20,
        .remap_key = "hero",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
      },
      VsmVirtualMapLayout {
        .id = 21,
        .remap_key = "hero",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
      },
    },
  };

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 1U);
  EXPECT_EQ(table.entries[0].previous_id, 10U);
  EXPECT_EQ(table.entries[0].current_id, 0U);
  EXPECT_EQ(table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kDuplicateRemapKey);
}

NOLINT_TEST_F(VsmVirtualRemapBuilderTest,
  MissingRemapKeysAndLayoutMismatchReportExplicitRejection)
{
  const auto missing_key_previous = VsmVirtualAddressSpaceFrame {
    .frame_generation = 1,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 10,
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
      },
    },
  };
  const auto mismatched_current = VsmVirtualAddressSpaceFrame {
    .frame_generation = 2,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 30,
        .remap_key = "hero",
        .level_count = 2,
        .pages_per_level_x = 2,
        .pages_per_level_y = 2,
        .total_page_count = 8,
      },
    },
  };
  const auto matched_previous = VsmVirtualAddressSpaceFrame {
    .frame_generation = 1,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 11,
        .remap_key = "hero",
        .level_count = 1,
        .pages_per_level_x = 1,
        .pages_per_level_y = 1,
        .total_page_count = 1,
      },
    },
  };

  const auto missing_key_table
    = BuildVirtualRemapTable(missing_key_previous, mismatched_current);
  ASSERT_EQ(missing_key_table.entries.size(), 1U);
  EXPECT_EQ(missing_key_table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kMissingRemapKey);

  const auto mismatch_table
    = BuildVirtualRemapTable(matched_previous, mismatched_current);
  ASSERT_EQ(mismatch_table.entries.size(), 1U);
  EXPECT_EQ(mismatch_table.entries[0].previous_id, 11U);
  EXPECT_EQ(mismatch_table.entries[0].current_id, 30U);
  EXPECT_EQ(mismatch_table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kLocalLightLayoutMismatch);
}

NOLINT_TEST_F(VsmVirtualRemapBuilderTest,
  DuplicateCurrentClipmapKeysAndMalformedCurrentLayoutsReportExplicitRejection)
{
  const auto previous_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 1,
    .config = MakeConfig(),
    .directional_layouts = {
      VsmClipmapLayout {
        .first_id = 40,
        .remap_key = "sun",
        .clip_level_count = 2,
        .pages_per_axis = 4,
        .page_grid_origin = { { 0, 0 }, { 4, 4 } },
        .page_world_size = { 32.0F, 64.0F },
        .near_depth = { 1.0F, 2.0F },
        .far_depth = { 101.0F, 202.0F },
      },
    },
  };
  const auto duplicate_current = VsmVirtualAddressSpaceFrame {
    .frame_generation = 2,
    .config = MakeConfig(),
    .directional_layouts = {
      VsmClipmapLayout {
        .first_id = 80,
        .remap_key = "sun",
        .clip_level_count = 2,
        .pages_per_axis = 4,
        .page_grid_origin = { { 1, 0 }, { 5, 4 } },
        .page_world_size = { 32.0F, 64.0F },
        .near_depth = { 1.0F, 2.0F },
        .far_depth = { 101.0F, 202.0F },
      },
      VsmClipmapLayout {
        .first_id = 90,
        .remap_key = "sun",
        .clip_level_count = 2,
        .pages_per_axis = 4,
        .page_grid_origin = { { 1, 0 }, { 5, 4 } },
        .page_world_size = { 32.0F, 64.0F },
        .near_depth = { 1.0F, 2.0F },
        .far_depth = { 101.0F, 202.0F },
      },
    },
  };
  auto malformed_current = VsmVirtualAddressSpaceFrame {
    .frame_generation = 3,
    .config = MakeConfig(),
    .directional_layouts = {
      VsmClipmapLayout {
        .first_id = 100,
        .remap_key = "sun",
        .clip_level_count = 2,
        .pages_per_axis = 4,
        .page_grid_origin = { { 1, 0 }, { 5, 4 } },
        .page_world_size = { 32.0F },
        .near_depth = { 1.0F, 2.0F },
        .far_depth = { 101.0F, 202.0F },
      },
    },
  };

  const auto duplicate_table
    = BuildVirtualRemapTable(previous_frame, duplicate_current);
  ASSERT_EQ(duplicate_table.entries.size(), 2U);
  EXPECT_EQ(duplicate_table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kDuplicateRemapKey);
  EXPECT_EQ(duplicate_table.entries[1].rejection_reason,
    VsmReuseRejectionReason::kDuplicateRemapKey);

  const auto malformed_table
    = BuildVirtualRemapTable(previous_frame, malformed_current);
  ASSERT_EQ(malformed_table.entries.size(), 2U);
  EXPECT_EQ(malformed_table.entries[0].rejection_reason,
    VsmReuseRejectionReason::kUnspecified);
  EXPECT_EQ(malformed_table.entries[1].rejection_reason,
    VsmReuseRejectionReason::kUnspecified);
  EXPECT_EQ(malformed_table.entries[0].current_id, 100U);
  EXPECT_EQ(malformed_table.entries[1].current_id, 101U);
}

NOLINT_TEST_F(VsmVirtualRemapBuilderTest,
  MalformedCurrentLocalLightLayoutsReportExplicitRejection)
{
  const auto previous_frame = VsmVirtualAddressSpaceFrame {
    .frame_generation = 1,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 15,
        .remap_key = "hero",
        .level_count = 2,
        .pages_per_level_x = 2,
        .pages_per_level_y = 2,
        .total_page_count = 8,
      },
    },
  };
  const auto malformed_current = VsmVirtualAddressSpaceFrame {
    .frame_generation = 2,
    .config = MakeConfig(),
    .local_light_layouts = {
      VsmVirtualMapLayout {
        .id = 55,
        .remap_key = "hero",
        .level_count = 2,
        .pages_per_level_x = 2,
        .pages_per_level_y = 2,
        .total_page_count = 7,
      },
    },
  };

  const auto table = BuildVirtualRemapTable(previous_frame, malformed_current);

  ASSERT_EQ(table.entries.size(), 1U);
  EXPECT_EQ(table.entries[0].previous_id, 15U);
  EXPECT_EQ(table.entries[0].current_id, 55U);
  EXPECT_EQ(
    table.entries[0].rejection_reason, VsmReuseRejectionReason::kUnspecified);
}

} // namespace

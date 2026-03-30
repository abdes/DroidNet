//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualRemapBuilder.h>

#include "VirtualShadowStageCpuHarness.h"

namespace {

using oxygen::renderer::vsm::BuildVirtualRemapTable;
using oxygen::renderer::vsm::VsmReuseRejectionReason;
using oxygen::renderer::vsm::VsmVirtualRemapEntry;
using oxygen::renderer::vsm::testing::DirectionalStageClipmapSpec;
using oxygen::renderer::vsm::testing::LocalStageLightSpec;
using oxygen::renderer::vsm::testing::VsmStageCpuHarness;

auto FindEntry(const std::vector<VsmVirtualRemapEntry>& entries,
  const std::uint32_t previous_id) -> const VsmVirtualRemapEntry*
{
  const auto it = std::find_if(
    entries.begin(), entries.end(), [previous_id](const auto& entry) {
      return entry.previous_id == previous_id;
    });
  return it == entries.end() ? nullptr : &*it;
}

class VsmRemapConstructionTest : public VsmStageCpuHarness { };

NOLINT_TEST_F(VsmRemapConstructionTest,
  MapsRetainedLocalLightsByStableKeyInsteadOfAllocationOrder)
{
  const auto previous_locals = std::array {
    LocalStageLightSpec {
      .remap_key = "local-a",
    },
    LocalStageLightSpec {
      .remap_key = "local-b",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
  };
  const auto current_locals = std::array {
    LocalStageLightSpec {
      .remap_key = "new-fill",
    },
    LocalStageLightSpec {
      .remap_key = "local-b",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
    LocalStageLightSpec {
      .remap_key = "local-a",
    },
  };

  const auto previous_frame
    = MakeLocalFrame(1ULL, 10U, previous_locals, "vsm-remap.locals.previous");
  const auto current_frame
    = MakeLocalFrame(2ULL, 100U, current_locals, "vsm-remap.locals.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 2U);

  const auto* local_a
    = FindEntry(table.entries, previous_frame.local_light_layouts[0].id);
  const auto* local_b
    = FindEntry(table.entries, previous_frame.local_light_layouts[1].id);
  ASSERT_NE(local_a, nullptr);
  ASSERT_NE(local_b, nullptr);

  EXPECT_EQ(local_a->current_id, current_frame.local_light_layouts[2].id);
  EXPECT_EQ(local_b->current_id, current_frame.local_light_layouts[1].id);
  EXPECT_NE(local_a->current_id, previous_frame.local_light_layouts[0].id);
  EXPECT_NE(local_b->current_id, previous_frame.local_light_layouts[1].id);
  EXPECT_EQ(local_a->page_offset, glm::ivec2(0, 0));
  EXPECT_EQ(local_b->page_offset, glm::ivec2(0, 0));
  EXPECT_EQ(local_a->rejection_reason, VsmReuseRejectionReason::kNone);
  EXPECT_EQ(local_b->rejection_reason, VsmReuseRejectionReason::kNone);
  EXPECT_EQ(
    FindEntry(table.entries, current_frame.local_light_layouts[0].id), nullptr);
}

NOLINT_TEST_F(VsmRemapConstructionTest,
  ProducesMixedLocalAndDirectionalRemapEntriesWithPerLevelPageOffsets)
{
  const auto previous_directional = std::array {
    DirectionalStageClipmapSpec {
      .remap_key = "sun",
      .clip_level_count = 2U,
      .pages_per_axis = 4U,
      .page_grid_origin = { { 0, 0 }, { 4, 4 } },
      .page_world_size = { 32.0F, 64.0F },
      .near_depth = { 1.0F, 2.0F },
      .far_depth = { 101.0F, 202.0F },
    },
  };
  const auto current_directional = std::array {
    DirectionalStageClipmapSpec {
      .remap_key = "sun",
      .clip_level_count = 2U,
      .pages_per_axis = 4U,
      .page_grid_origin = { { 1, -1 }, { 7, 2 } },
      .page_world_size = { 32.0F, 64.0F },
      .near_depth = { 1.0F, 2.0F },
      .far_depth = { 101.0F, 202.0F },
    },
    DirectionalStageClipmapSpec {
      .remap_key = "moon",
      .clip_level_count = 1U,
      .pages_per_axis = 2U,
      .page_grid_origin = { { 0, 0 } },
      .page_world_size = { 16.0F },
      .near_depth = { 0.5F },
      .far_depth = { 32.0F },
    },
  };
  const auto previous_locals = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
  };
  const auto current_locals = std::array {
    LocalStageLightSpec {
      .remap_key = "fill-local",
    },
    LocalStageLightSpec {
      .remap_key = "hero-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
  };

  const auto previous_frame = MakeFrame(1ULL, 10U, previous_directional,
    previous_locals, "vsm-remap.mixed.previous");
  const auto current_frame = MakeFrame(
    2ULL, 100U, current_directional, current_locals, "vsm-remap.mixed.current");

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 3U);

  const auto* local
    = FindEntry(table.entries, previous_frame.local_light_layouts[0].id);
  const auto* sun_level0
    = FindEntry(table.entries, previous_frame.directional_layouts[0].first_id);
  const auto* sun_level1 = FindEntry(
    table.entries, previous_frame.directional_layouts[0].first_id + 1U);
  ASSERT_NE(local, nullptr);
  ASSERT_NE(sun_level0, nullptr);
  ASSERT_NE(sun_level1, nullptr);

  EXPECT_EQ(local->current_id, current_frame.local_light_layouts[1].id);
  EXPECT_EQ(local->page_offset, glm::ivec2(0, 0));
  EXPECT_EQ(local->rejection_reason, VsmReuseRejectionReason::kNone);

  EXPECT_EQ(
    sun_level0->current_id, current_frame.directional_layouts[0].first_id);
  EXPECT_EQ(sun_level0->page_offset, glm::ivec2(1, -1));
  EXPECT_EQ(sun_level0->rejection_reason, VsmReuseRejectionReason::kNone);

  EXPECT_EQ(
    sun_level1->current_id, current_frame.directional_layouts[0].first_id + 1U);
  EXPECT_EQ(sun_level1->page_offset, glm::ivec2(3, -2));
  EXPECT_EQ(sun_level1->rejection_reason, VsmReuseRejectionReason::kNone);
  EXPECT_EQ(
    FindEntry(table.entries, current_frame.directional_layouts[1].first_id),
    nullptr);
}

} // namespace

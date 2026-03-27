//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>

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
  PreservesStableIdentityAndClipmapPageOffsetsAcrossFreshVirtualIds)
{
  constexpr auto kPreviousLocals = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
  };
  constexpr auto kCurrentLocals = std::array {
    LocalStageLightSpec {
      .remap_key = "hero-local",
      .level_count = 2U,
      .pages_per_level_x = 2U,
      .pages_per_level_y = 2U,
    },
  };
  const auto previous_directional = std::array {
    DirectionalStageClipmapSpec {
      .remap_key = "sun",
      .clip_level_count = 2U,
      .pages_per_axis = 2U,
      .page_grid_origin = { { 0, 0 }, { 4, 4 } },
      .page_world_size = { 32.0F, 64.0F },
      .near_depth = { 1.0F, 2.0F },
      .far_depth = { 100.0F, 200.0F },
    },
  };
  const auto current_directional = std::array {
    DirectionalStageClipmapSpec {
      .remap_key = "sun",
      .clip_level_count = 2U,
      .pages_per_axis = 2U,
      .page_grid_origin = { { 1, -1 }, { 5, 3 } },
      .page_world_size = { 32.0F, 64.0F },
      .near_depth = { 1.0F, 2.0F },
      .far_depth = { 100.0F, 200.0F },
    },
  };

  auto previous_address_space
    = oxygen::renderer::vsm::VsmVirtualAddressSpace {};
  previous_address_space.BeginFrame(
    oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 10U,
      .clipmap_reuse_config =
        {
          .max_page_offset_x = 4,
          .max_page_offset_y = 4,
          .depth_range_epsilon = 0.01F,
          .page_world_size_epsilon = 0.01F,
        },
      .debug_name = "vsm-remap.previous",
    },
    1ULL);
  for (const auto& spec : kPreviousLocals) {
    previous_address_space.AllocatePagedLocalLight(
      oxygen::renderer::vsm::VsmLocalLightDesc {
        .remap_key = std::string(spec.remap_key),
        .level_count = spec.level_count,
        .pages_per_level_x = spec.pages_per_level_x,
        .pages_per_level_y = spec.pages_per_level_y,
        .debug_name = std::string(spec.remap_key),
      });
  }
  for (const auto& spec : previous_directional) {
    previous_address_space.AllocateDirectionalClipmap(
      oxygen::renderer::vsm::VsmDirectionalClipmapDesc {
        .remap_key = std::string(spec.remap_key),
        .clip_level_count = spec.clip_level_count,
        .pages_per_axis = spec.pages_per_axis,
        .page_grid_origin = spec.page_grid_origin,
        .page_world_size = spec.page_world_size,
        .near_depth = spec.near_depth,
        .far_depth = spec.far_depth,
        .debug_name = std::string(spec.remap_key),
      });
  }
  const auto previous_frame = previous_address_space.DescribeFrame();

  auto current_address_space = oxygen::renderer::vsm::VsmVirtualAddressSpace {};
  current_address_space.BeginFrame(
    oxygen::renderer::vsm::VsmVirtualAddressSpaceConfig {
      .first_virtual_id = 100U,
      .clipmap_reuse_config =
        {
          .max_page_offset_x = 4,
          .max_page_offset_y = 4,
          .depth_range_epsilon = 0.01F,
          .page_world_size_epsilon = 0.01F,
        },
      .debug_name = "vsm-remap.current",
    },
    2ULL);
  for (const auto& spec : kCurrentLocals) {
    current_address_space.AllocatePagedLocalLight(
      oxygen::renderer::vsm::VsmLocalLightDesc {
        .remap_key = std::string(spec.remap_key),
        .level_count = spec.level_count,
        .pages_per_level_x = spec.pages_per_level_x,
        .pages_per_level_y = spec.pages_per_level_y,
        .debug_name = std::string(spec.remap_key),
      });
  }
  for (const auto& spec : current_directional) {
    current_address_space.AllocateDirectionalClipmap(
      oxygen::renderer::vsm::VsmDirectionalClipmapDesc {
        .remap_key = std::string(spec.remap_key),
        .clip_level_count = spec.clip_level_count,
        .pages_per_axis = spec.pages_per_axis,
        .page_grid_origin = spec.page_grid_origin,
        .page_world_size = spec.page_world_size,
        .near_depth = spec.near_depth,
        .far_depth = spec.far_depth,
        .debug_name = std::string(spec.remap_key),
      });
  }
  const auto current_frame = current_address_space.DescribeFrame();

  const auto table = BuildVirtualRemapTable(previous_frame, current_frame);

  ASSERT_EQ(table.entries.size(), 3U);
  const auto* local
    = FindEntry(table.entries, previous_frame.local_light_layouts[0].id);
  const auto* sun0
    = FindEntry(table.entries, previous_frame.directional_layouts[0].first_id);
  const auto* sun1 = FindEntry(
    table.entries, previous_frame.directional_layouts[0].first_id + 1U);
  ASSERT_NE(local, nullptr);
  ASSERT_NE(sun0, nullptr);
  ASSERT_NE(sun1, nullptr);
  EXPECT_EQ(local->current_id, current_frame.local_light_layouts[0].id);
  EXPECT_EQ(local->rejection_reason, VsmReuseRejectionReason::kNone);
  EXPECT_EQ(sun0->current_id, current_frame.directional_layouts[0].first_id);
  EXPECT_EQ(
    sun1->current_id, current_frame.directional_layouts[0].first_id + 1U);
  EXPECT_EQ(sun0->page_offset, glm::ivec2(1, -1));
  EXPECT_EQ(sun1->page_offset, glm::ivec2(1, -1));
  EXPECT_EQ(sun0->rejection_reason, VsmReuseRejectionReason::kNone);
  EXPECT_EQ(sun1->rejection_reason, VsmReuseRejectionReason::kNone);
}

} // namespace

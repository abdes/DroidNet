//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <glm/vec2.hpp>

#include <Oxygen/Renderer/VirtualShadowMaps/VsmCacheManager.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpace.h>
#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualAddressSpaceTypes.h>

#include "VirtualShadowTestFixtures.h"

namespace oxygen::renderer::vsm::testing {

struct LocalStageLightSpec {
  std::string_view remap_key {};
  std::uint32_t level_count { 1U };
  std::uint32_t pages_per_level_x { 1U };
  std::uint32_t pages_per_level_y { 1U };
};

struct DirectionalStageClipmapSpec {
  std::string_view remap_key {};
  std::uint32_t clip_level_count { 1U };
  std::uint32_t pages_per_axis { 1U };
  std::vector<glm::ivec2> page_grid_origin {};
  std::vector<float> page_world_size {};
  std::vector<float> near_depth {};
  std::vector<float> far_depth {};
};

class VsmStageCpuHarness : public VsmCacheManagerTestBase {
protected:
  [[nodiscard]] static auto MakeFrame(const std::uint64_t frame_generation,
    const std::uint32_t first_virtual_id,
    std::span<const DirectionalStageClipmapSpec> directional_specs,
    std::span<const LocalStageLightSpec> local_specs, const char* debug_name)
    -> VsmVirtualAddressSpaceFrame
  {
    auto address_space = VsmVirtualAddressSpace {};
    address_space.BeginFrame(
      VsmVirtualAddressSpaceConfig {
        .first_virtual_id = first_virtual_id,
        .clipmap_reuse_config =
          {
            .max_page_offset_x = 4,
            .max_page_offset_y = 4,
            .depth_range_epsilon = 0.01F,
            .page_world_size_epsilon = 0.01F,
          },
        .debug_name = debug_name,
      },
      frame_generation);

    for (const auto& spec : directional_specs) {
      address_space.AllocateDirectionalClipmap(VsmDirectionalClipmapDesc {
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

    for (const auto& spec : local_specs) {
      if (spec.level_count == 1U && spec.pages_per_level_x == 1U
        && spec.pages_per_level_y == 1U) {
        address_space.AllocateSinglePageLocalLight(VsmSinglePageLightDesc {
          .remap_key = std::string(spec.remap_key),
          .debug_name = std::string(spec.remap_key),
        });
        continue;
      }

      address_space.AllocatePagedLocalLight(VsmLocalLightDesc {
        .remap_key = std::string(spec.remap_key),
        .level_count = spec.level_count,
        .pages_per_level_x = spec.pages_per_level_x,
        .pages_per_level_y = spec.pages_per_level_y,
        .debug_name = std::string(spec.remap_key),
      });
    }

    return address_space.DescribeFrame();
  }

  [[nodiscard]] static auto MakeLocalFrame(const std::uint64_t frame_generation,
    const std::uint32_t first_virtual_id,
    std::span<const LocalStageLightSpec> specs, const char* debug_name)
    -> VsmVirtualAddressSpaceFrame
  {
    return MakeFrame(frame_generation, first_virtual_id, {}, specs, debug_name);
  }

  [[nodiscard]] static auto MakeDirectionalFrame(
    const std::uint64_t frame_generation, const std::uint32_t first_virtual_id,
    std::span<const DirectionalStageClipmapSpec> specs, const char* debug_name)
    -> VsmVirtualAddressSpaceFrame
  {
    return MakeFrame(frame_generation, first_virtual_id, specs, {}, debug_name);
  }

  [[nodiscard]] static auto MakeSinglePageRequest(
    const VsmVirtualAddressSpaceFrame& frame,
    const std::size_t light_index = 0U,
    const VsmPageRequestFlags flags = VsmPageRequestFlags::kRequired)
    -> VsmPageRequest
  {
    return VsmPageRequest {
      .map_id = frame.local_light_layouts.at(light_index).id,
      .page = {},
      .flags = flags,
    };
  }

  [[nodiscard]] static auto ResolveLocalEntryIndex(
    const VsmVirtualAddressSpaceFrame& frame,
    const std::size_t light_index = 0U) -> std::uint32_t
  {
    return ResolveLocalEntryIndex(frame, VsmVirtualPageCoord {}, light_index);
  }

  [[nodiscard]] static auto ResolveLocalEntryIndex(
    const VsmVirtualAddressSpaceFrame& frame, const VsmVirtualPageCoord& coord,
    const std::size_t light_index = 0U) -> std::uint32_t
  {
    const auto entry_index = TryGetPageTableEntryIndex(
      frame.local_light_layouts.at(light_index), coord);
    EXPECT_TRUE(entry_index.has_value());
    return entry_index.value_or(0U);
  }

  [[nodiscard]] static auto ResolveDirectionalEntryIndex(
    const VsmVirtualAddressSpaceFrame& frame, const VsmVirtualPageCoord& coord,
    const std::size_t light_index = 0U) -> std::uint32_t
  {
    const auto entry_index = TryGetPageTableEntryIndex(
      frame.directional_layouts.at(light_index), coord);
    EXPECT_TRUE(entry_index.has_value());
    return entry_index.value_or(0U);
  }

  [[nodiscard]] static auto MakeProjectionRecord(const std::uint32_t map_id,
    const std::uint32_t first_page_table_entry,
    const std::uint32_t pages_x = 1U, const std::uint32_t pages_y = 1U,
    const std::uint32_t level_count = 1U,
    const VsmProjectionLightType light_type = VsmProjectionLightType::kLocal)
    -> VsmPageRequestProjection
  {
    return VsmPageRequestProjection {
      .projection = VsmProjectionData {
        .view_matrix = glm::mat4 { 1.0F },
        .projection_matrix = glm::mat4 { 1.0F },
        .view_origin_ws_pad = { 0.0F, 0.0F, 0.0F, 0.0F },
        .clipmap_corner_offset = { 0, 0 },
        .clipmap_level = 0U,
        .light_type = static_cast<std::uint32_t>(light_type),
      },
      .map_id = map_id,
      .first_page_table_entry = first_page_table_entry,
      .map_pages_x = pages_x,
      .map_pages_y = pages_y,
      .pages_x = pages_x,
      .pages_y = pages_y,
      .page_offset_x = 0U,
      .page_offset_y = 0U,
      .level_count = level_count,
      .coarse_level = level_count > 1U ? level_count - 1U : 0U,
    };
  }
};

} // namespace oxygen::renderer::vsm::testing

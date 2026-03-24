//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Renderer/VirtualShadowMaps/VsmVirtualClipmapHelpers.h>

#include <cmath>

#include <Oxygen/Base/Logging.h>

namespace oxygen::renderer::vsm {

auto ComputeClipmapReuse(const VsmClipmapLayout& previous_layout,
  const VsmClipmapLayout& current_layout,
  const VsmClipmapReuseConfig& config) noexcept -> VsmClipmapReuseResult
{
  if (previous_layout.clip_level_count != current_layout.clip_level_count) {
    return {
      .reusable = false,
      .rejection_reason = VsmReuseRejectionReason::kClipLevelCountMismatch,
    };
  }

  if (previous_layout.pages_per_axis != current_layout.pages_per_axis) {
    return {
      .reusable = false,
      .rejection_reason = VsmReuseRejectionReason::kPagesPerAxisMismatch,
    };
  }

  if (previous_layout.page_grid_origin.size()
      != previous_layout.clip_level_count
    || current_layout.page_grid_origin.size() != current_layout.clip_level_count
    || previous_layout.page_world_size.size()
      != previous_layout.clip_level_count
    || current_layout.page_world_size.size() != current_layout.clip_level_count
    || previous_layout.near_depth.size() != previous_layout.clip_level_count
    || current_layout.near_depth.size() != current_layout.clip_level_count
    || previous_layout.far_depth.size() != previous_layout.clip_level_count
    || current_layout.far_depth.size() != current_layout.clip_level_count) {
    LOG_F(WARNING,
      "rejecting malformed clipmap layouts for remap_key previous=`{}` "
      "current=`{}`",
      previous_layout.remap_key, current_layout.remap_key);
    return {
      .reusable = false,
      .rejection_reason = VsmReuseRejectionReason::kUnspecified,
    };
  }

  auto page_offsets
    = std::vector<glm::ivec2>(current_layout.clip_level_count, { 0, 0 });

  for (std::uint32_t clip_index = 0;
    clip_index < current_layout.clip_level_count; ++clip_index) {
    if (std::fabs(current_layout.page_world_size[clip_index]
          - previous_layout.page_world_size[clip_index])
      > config.page_world_size_epsilon) {
      return {
        .reusable = false,
        .rejection_reason = VsmReuseRejectionReason::kPageWorldSizeMismatch,
      };
    }

    const auto previous_range = previous_layout.far_depth[clip_index]
      - previous_layout.near_depth[clip_index];
    const auto current_range = current_layout.far_depth[clip_index]
      - current_layout.near_depth[clip_index];
    if (std::fabs(current_range - previous_range)
      > config.depth_range_epsilon) {
      return {
        .reusable = false,
        .rejection_reason = VsmReuseRejectionReason::kDepthRangeMismatch,
      };
    }

    const auto offset = current_layout.page_grid_origin[clip_index]
      - previous_layout.page_grid_origin[clip_index];
    page_offsets[clip_index] = offset;

    if (std::abs(offset.x) > config.max_page_offset_x
      || std::abs(offset.y) > config.max_page_offset_y) {
      return {
        .reusable = false,
        .page_offsets = std::move(page_offsets),
        .rejection_reason = VsmReuseRejectionReason::kPageOffsetOutOfRange,
      };
    }
  }

  return {
    .reusable = true,
    .page_offsets = std::move(page_offsets),
    .rejection_reason = VsmReuseRejectionReason::kNone,
  };
}

} // namespace oxygen::renderer::vsm

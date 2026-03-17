//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/vec4.hpp>

#include <Oxygen/Renderer/Types/DirectionalVirtualShadowMetadata.h>

namespace oxygen::renderer::test::virtual_shadow_support {

inline auto MakeDirectionalVirtualMetadata(const std::int32_t clip0_grid_x,
  const std::int32_t clip0_grid_y, const float clip0_page_world,
  const std::int32_t clip1_grid_x, const std::int32_t clip1_grid_y,
  const float clip1_page_world) -> engine::DirectionalVirtualShadowMetadata
{
  engine::DirectionalVirtualShadowMetadata metadata {};
  metadata.clip_level_count = 2U;
  metadata.pages_per_axis = 16U;
  metadata.page_size_texels = 128U;
  metadata.clip_metadata[0].origin_page_scale
    = glm::vec4(static_cast<float>(clip0_grid_x) * clip0_page_world,
      static_cast<float>(clip0_grid_y) * clip0_page_world, clip0_page_world,
      0.0F);
  metadata.clip_metadata[1].origin_page_scale
    = glm::vec4(static_cast<float>(clip1_grid_x) * clip1_page_world,
      static_cast<float>(clip1_grid_y) * clip1_page_world, clip1_page_world,
      0.0F);
  return metadata;
}

inline auto SetDirectionalDepthRange(
  engine::DirectionalVirtualShadowMetadata& metadata, const float near_plane,
  const float far_plane) -> void
{
  const float depth_scale = 1.0F / (near_plane - far_plane);
  const float depth_bias = near_plane / (near_plane - far_plane);
  for (std::uint32_t clip_index = 0U; clip_index < metadata.clip_level_count;
    ++clip_index) {
    metadata.clip_metadata[clip_index].origin_page_scale.w = depth_scale;
    metadata.clip_metadata[clip_index].bias_reserved.x = depth_bias;
  }
}

} // namespace oxygen::renderer::test::virtual_shadow_support

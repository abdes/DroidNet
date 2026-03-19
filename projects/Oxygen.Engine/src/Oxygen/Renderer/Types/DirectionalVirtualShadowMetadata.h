//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace oxygen::engine {

inline constexpr std::uint32_t kMaxVirtualDirectionalClipLevels = 12U;

struct alignas(16) DirectionalVirtualClipMetadata {
  glm::vec4 origin_page_scale { 0.0F, 0.0F, 1.0F,
    1.0F }; // xy origin_ls, z page_world_size, w depth_scale
  glm::vec4 bias_reserved { 0.0F, 0.0F, 0.0F,
    0.0F }; // x depth_bias, yzw reserved
  glm::ivec4 clipmap_level_data { 0, 0, 0,
    0 }; // x clipmap level, y remaining levels, zw reserved
};

static_assert(sizeof(DirectionalVirtualClipMetadata) == 48U,
  "DirectionalVirtualClipMetadata size must match HLSL packing");

struct alignas(16) DirectionalVirtualShadowMetadata {
  std::uint32_t shadow_instance_index { 0U };
  std::uint32_t flags { 0U };
  float constant_bias { 0.0F };
  float normal_bias { 0.0F };

  std::uint32_t clip_level_count { 0U };
  std::uint32_t pages_per_axis { 0U };
  std::uint32_t page_size_texels { 0U };
  std::uint32_t page_table_offset { 0U };
  std::uint32_t coarse_clip_mask { 0U };
  float receiver_normal_bias_scale { 1.0F };
  float receiver_constant_bias_scale { 1.0F };
  float receiver_slope_bias_scale { 1.0F };
  float raster_constant_bias_scale { 0.0F };
  float raster_slope_bias_scale { 0.0F };
  std::uint32_t reserved0 { 0U };
  std::uint32_t reserved1 { 0U };
  glm::vec4 clipmap_selection_world_origin_lod_bias { 0.0F, 0.0F, 0.0F,
    0.0F }; // xyz clipmap selection origin ws, w selection lod bias
  glm::vec4 clipmap_receiver_origin_lod_bias { 0.0F, 0.0F, 0.0F,
    0.0F }; // xyz receiver-bias origin ws, w receiver clip bias
  std::array<glm::ivec4, 3> clip_grid_origin_x_packed {};
  std::array<glm::ivec4, 3> clip_grid_origin_y_packed {};

  std::array<DirectionalVirtualClipMetadata, kMaxVirtualDirectionalClipLevels>
    clip_metadata {};
  glm::mat4 light_view { 1.0F };
};

static_assert(sizeof(DirectionalVirtualShadowMetadata) == 832U,
  "DirectionalVirtualShadowMetadata size must match HLSL packing");
static_assert(sizeof(DirectionalVirtualShadowMetadata) % 16U == 0U,
  "DirectionalVirtualShadowMetadata size must be 16-byte aligned");

} // namespace oxygen::engine

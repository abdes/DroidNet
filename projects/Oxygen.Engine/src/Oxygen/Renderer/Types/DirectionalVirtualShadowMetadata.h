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

inline constexpr std::uint32_t kMaxVirtualDirectionalClipLevels = 6U;

struct alignas(16) DirectionalVirtualClipMetadata {
  glm::vec4 origin_page_scale { 0.0F, 0.0F, 1.0F,
    1.0F }; // xy origin_ls, z page_world_size, w depth_scale
  glm::vec4 bias_reserved { 0.0F, 0.0F, 0.0F,
    0.0F }; // x depth_bias, yzw reserved
};

static_assert(sizeof(DirectionalVirtualClipMetadata) == 32U,
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

  std::array<DirectionalVirtualClipMetadata, kMaxVirtualDirectionalClipLevels>
    clip_metadata {};
  glm::mat4 light_view { 1.0F };
};

static_assert(sizeof(DirectionalVirtualShadowMetadata) == 288U,
  "DirectionalVirtualShadowMetadata size must match HLSL packing");
static_assert(sizeof(DirectionalVirtualShadowMetadata) % 16U == 0U,
  "DirectionalVirtualShadowMetadata size must be 16-byte aligned");

} // namespace oxygen::engine

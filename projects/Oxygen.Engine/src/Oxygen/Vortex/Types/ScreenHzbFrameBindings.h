//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kScreenHzbFrameBindingsFlagAvailable = 1U << 0U;
inline constexpr std::uint32_t kScreenHzbFrameBindingsFlagFurthestValid
  = 1U << 1U;
inline constexpr std::uint32_t kScreenHzbFrameBindingsFlagClosestValid
  = 1U << 2U;

struct alignas(16) ScreenHzbFrameBindings {
  ShaderVisibleIndex closest_srv { kInvalidShaderVisibleIndex };
  ShaderVisibleIndex furthest_srv { kInvalidShaderVisibleIndex };
  std::uint32_t width { 0U };
  std::uint32_t height { 0U };
  std::uint32_t mip_count { 0U };
  std::uint32_t flags { 0U };
  float hzb_size_x { 0.0F };
  float hzb_size_y { 0.0F };
  float hzb_view_size_x { 0.0F };
  float hzb_view_size_y { 0.0F };
  std::int32_t hzb_view_rect_min_x { 0 };
  std::int32_t hzb_view_rect_min_y { 0 };
  std::int32_t hzb_view_rect_width { 0 };
  std::int32_t hzb_view_rect_height { 0 };
  float viewport_uv_to_hzb_buffer_uv_x { 0.0F };
  float viewport_uv_to_hzb_buffer_uv_y { 0.0F };
  float hzb_uv_factor_x { 0.0F };
  float hzb_uv_factor_y { 0.0F };
  float hzb_uv_inv_factor_x { 0.0F };
  float hzb_uv_inv_factor_y { 0.0F };
  float hzb_uv_to_screen_uv_scale_x { 0.0F };
  float hzb_uv_to_screen_uv_scale_y { 0.0F };
  float hzb_uv_to_screen_uv_bias_x { 0.0F };
  float hzb_uv_to_screen_uv_bias_y { 0.0F };
  float hzb_base_texel_size_x { 0.0F };
  float hzb_base_texel_size_y { 0.0F };
  float sample_pixel_to_hzb_uv_x { 0.0F };
  float sample_pixel_to_hzb_uv_y { 0.0F };
  float screen_pos_to_hzb_uv_scale_x { 0.0F };
  float screen_pos_to_hzb_uv_scale_y { 0.0F };
  float screen_pos_to_hzb_uv_bias_x { 0.0F };
  float screen_pos_to_hzb_uv_bias_y { 0.0F };
};

static_assert(std::is_standard_layout_v<ScreenHzbFrameBindings>);
static_assert(sizeof(ScreenHzbFrameBindings) == 128U);
static_assert(alignof(ScreenHzbFrameBindings) == 16U);

} // namespace oxygen::vortex

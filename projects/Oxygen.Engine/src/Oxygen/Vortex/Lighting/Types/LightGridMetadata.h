//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kLightGridPerspective = 0U;
inline constexpr std::uint32_t kLightGridOrthographic = 1U;

struct alignas(packing::kShaderDataFieldAlignment) LightGridMetadata {
  glm::uvec3 grid_size { 0U };
  std::uint32_t pixel_size_shift { 0U };
  glm::vec2 content_origin_px { 0.0F };
  glm::vec2 content_extent_px { 0.0F };
  glm::vec3 grid_z_params { 0.0F };
  float far_depth_m { 0.0F };
  float near_depth_m { 0.0F };
  std::uint32_t projection_kind { kLightGridPerspective };
  std::array<std::uint32_t, 2> reserved {};
};

// NOLINTBEGIN(*-magic-numbers)
static_assert(std::is_standard_layout_v<LightGridMetadata>);
static_assert(std::is_trivially_copyable_v<LightGridMetadata>);
static_assert(alignof(LightGridMetadata) == 16U);
static_assert(sizeof(LightGridMetadata) == 64U);
static_assert(offsetof(LightGridMetadata, grid_size) == 0U);
static_assert(offsetof(LightGridMetadata, pixel_size_shift) == 12U);
static_assert(offsetof(LightGridMetadata, content_origin_px) == 16U);
static_assert(offsetof(LightGridMetadata, content_extent_px) == 24U);
static_assert(offsetof(LightGridMetadata, grid_z_params) == 32U);
static_assert(offsetof(LightGridMetadata, far_depth_m) == 44U);
static_assert(offsetof(LightGridMetadata, near_depth_m) == 48U);
static_assert(offsetof(LightGridMetadata, projection_kind) == 52U);
static_assert(offsetof(LightGridMetadata, reserved) == 56U);
// NOLINTEND(*-magic-numbers)

} // namespace oxygen::vortex

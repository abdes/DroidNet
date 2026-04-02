//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <glm/vec4.hpp>

namespace oxygen::renderer {

enum ConventionalShadowReceiverMaskFlagBits : std::uint32_t {
  kConventionalShadowReceiverMaskFlagValid = 1U << 0U,
  kConventionalShadowReceiverMaskFlagEmpty = 1U << 1U,
  kConventionalShadowReceiverMaskFlagHierarchyBuilt = 1U << 2U,
};

//! Final per-raster-job receiver-mask summary produced by `CSM-3`.
/*!
 The full-resolution and hierarchy masks are published in separate fixed-size
 per-job buffers. This summary provides the per-job metadata and occupancy
 counts required for RenderDoc validation and later culling phases.
*/
struct alignas(16) ConventionalShadowReceiverMaskSummary {
  glm::vec4 full_rect_center_half_extent { 0.0F };
  glm::vec4 raw_xy_min_max { 0.0F };
  glm::vec4 raw_depth_and_dilation { 0.0F };
  std::uint32_t target_array_slice { 0U };
  std::uint32_t flags { 0U };
  std::uint32_t sample_count { 0U };
  std::uint32_t occupied_tile_count { 0U };
  std::uint32_t hierarchy_occupied_tile_count { 0U };
  std::uint32_t base_tile_resolution { 0U };
  std::uint32_t hierarchy_tile_resolution { 0U };
  std::uint32_t dilation_tile_radius { 0U };
  std::uint32_t hierarchy_reduction { 0U };
  std::uint32_t _pad0[3] { 0U, 0U, 0U };

  auto operator==(const ConventionalShadowReceiverMaskSummary&) const -> bool
    = default;
};

static_assert(std::is_standard_layout_v<ConventionalShadowReceiverMaskSummary>);
static_assert(sizeof(ConventionalShadowReceiverMaskSummary) == 96U);
static_assert(
  sizeof(ConventionalShadowReceiverMaskSummary) % alignof(glm::vec4) == 0U);

} // namespace oxygen::renderer

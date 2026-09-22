//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

#include <Oxygen/Base/Types/Geometry.h>

namespace oxygen::vortex {

//! CPU geometry helpers for the canonical per-view LightGridMetadata record.
//! GPU routing and dispatch live in
//! LightingFrameBindings/LightGridPassConstants.
struct LightCullingConfig {
  static constexpr uint32_t kLightGridPixelSizeShift = 6U;
  static constexpr uint32_t kLightGridPixelSize = 1U
    << kLightGridPixelSizeShift;
  static constexpr uint32_t kLightGridSizeZ = 32U;
  static constexpr float kSliceDistributionScale = 4.05F;

  //! Compute grid dimensions for a given screen resolution.
  struct GridDimensions {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    std::uint64_t total_clusters;
  };

  struct ZParams {
    float depth_span_m { 0.0F };
    float curve_scale { 0.0F };
    float slice_scale { 0.0F };
  };

  [[nodiscard]] constexpr auto ComputeGridDimensions(
    Extent<uint32_t> screen_size) const noexcept -> GridDimensions
  {
    const uint32_t items_x = (screen_size.width / kLightGridPixelSize)
      + (screen_size.width % kLightGridPixelSize != 0U ? 1U : 0U);
    const uint32_t items_y = (screen_size.height / kLightGridPixelSize)
      + (screen_size.height % kLightGridPixelSize != 0U ? 1U : 0U);
    const uint32_t items_z = kLightGridSizeZ;
    return {
      .x = items_x,
      .y = items_y,
      .z = items_z,
      .total_clusters = static_cast<std::uint64_t>(items_x) * items_y * items_z,
    };
  }

  //! Resolve the clipped near-relative logarithmic mapping without plane
  //! padding.
  [[nodiscard]] static auto ComputeLightGridZParams(const float near_plane,
    const float far_plane) noexcept -> std::optional<ZParams>
  {
    const double span = static_cast<double>(far_plane) - near_plane;
    if (!std::isfinite(near_plane) || !std::isfinite(far_plane)
      || near_plane <= 0.0F || span < std::numeric_limits<float>::min()
      || span > std::numeric_limits<float>::max()) {
      return std::nullopt;
    }
    const double exponent = static_cast<double>(kLightGridSizeZ)
      / static_cast<double>(kSliceDistributionScale);
    return ZParams {
      .depth_span_m = static_cast<float>(span),
      .curve_scale = static_cast<float>(std::exp2(exponent) - 1.0),
      .slice_scale = kSliceDistributionScale,
    };
  }
};

} // namespace oxygen::vortex

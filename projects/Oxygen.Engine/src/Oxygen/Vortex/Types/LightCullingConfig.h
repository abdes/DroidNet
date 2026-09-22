//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

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
  static constexpr float kNearOffsetMeters = 0.095F;
  static constexpr float kFarPlanePadMeters = 0.1F;

  //! Compute grid dimensions for a given screen resolution.
  struct GridDimensions {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    std::uint64_t total_clusters;
  };

  struct ZParams {
    float b { 0.0F };
    float o { 0.0F };
    float s { 0.0F };
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

  //! Compute UE-style LightGridZParams adapted to Oxygen's meter-scale world.
  [[nodiscard]] static auto ComputeLightGridZParams(
    float near_plane, float far_plane) noexcept -> ZParams
  {
    const float clamped_near = std::max(near_plane, 1.0e-4F);
    const float clamped_far = std::max(
      far_plane + kFarPlanePadMeters, clamped_near + kFarPlanePadMeters);
    const float n = clamped_near + kNearOffsetMeters;
    const float f = std::max(clamped_far, n + 1.0e-3F);
    const double s = static_cast<double>(kSliceDistributionScale);
    const double exponent = static_cast<double>(kLightGridSizeZ - 1U) / s;
    const double o
      = (static_cast<double>(f) - static_cast<double>(n) * std::exp2(exponent))
      / static_cast<double>(f - n);
    const double b = (1.0 - o) / static_cast<double>(n);
    return ZParams {
      .b = static_cast<float>(b),
      .o = static_cast<float>(o),
      .s = static_cast<float>(s),
    };
  }

  [[nodiscard]] static auto ComputeZSlice(float linear_depth,
    const ZParams& z_params, uint32_t slice_count = kLightGridSizeZ) noexcept
    -> uint32_t
  {
    if (slice_count == 0U || linear_depth <= 0.0F || z_params.b <= 0.0F
      || z_params.s <= 0.0F) {
      return 0U;
    }

    const float encoded_depth = linear_depth * z_params.b + z_params.o;
    if (!(encoded_depth > 0.0F) || !std::isfinite(encoded_depth)) {
      return 0U;
    }

    const float slice_f = std::log2(encoded_depth) * z_params.s;
    if (!std::isfinite(slice_f)) {
      return 0U;
    }

    const float clamped_slice
      = std::clamp(slice_f, 0.0F, static_cast<float>(slice_count - 1U));
    return static_cast<uint32_t>(clamped_slice);
  }
};

} // namespace oxygen::vortex

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
#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::engine {

// We defines and use strong types for bindless slots to avoid accidental
// mixups.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(TypeName)                             \
  struct TypeName {                                                            \
    ShaderVisibleIndex value;                                                  \
    constexpr TypeName()                                                       \
      : TypeName(kInvalidShaderVisibleIndex)                                   \
    {                                                                          \
    }                                                                          \
    explicit constexpr TypeName(const ShaderVisibleIndex v)                    \
      : value(v)                                                               \
    {                                                                          \
    }                                                                          \
    [[nodiscard]] constexpr auto IsValid() const noexcept                      \
    {                                                                          \
      return value != kInvalidShaderVisibleIndex;                              \
    }                                                                          \
    constexpr auto operator<=>(const TypeName&) const = default;               \
  };                                                                           \
  static_assert(sizeof(TypeName) == 4);

OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(ClusterGridSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(ClusterIndexListSlot)

#undef OXYGEN_DEFINE_BINDLESS_SLOT_TYPE

//! Shader-facing clustered light-grid routing payload.
/*!
 Publishes the final clustered light-grid product to downstream consumers.

 The implementation shape is intentionally fixed to the engine-owned shipping
 path:

 - 64px XY cells (`LightGridPixelSizeShift = 6`)
 - 32 Z slices
 - UE-style `LightGridZParams = (B, O, S)` mapping
 - one flat grid/list output contract

 There are no runtime knobs here for alternate cell sizes, slice counts, or
 manual Z-range overrides. A zero-initialized instance represents "light grid
 unavailable for this view".

 @see LightCullingPass, LightingFrameBindings
*/
struct LightCullingConfig {
  static constexpr size_t kSize = 48;

  static constexpr uint32_t kLightGridPixelSizeShift = 6U;
  static constexpr uint32_t kLightGridPixelSize = 1U
    << kLightGridPixelSizeShift;
  static constexpr uint32_t kLightGridSizeZ = 32U;
  static constexpr uint32_t kMaxCulledLightsPerCell = 32U;
  static constexpr uint32_t kThreadGroupSize = 4U;
  static constexpr float kSliceDistributionScale = 4.05F;
  static constexpr float kNearOffsetMeters = 0.095F;
  static constexpr float kFarPlanePadMeters = 0.1F;

  // --- GPU-Compatible Data (Keep in sync with LightCulling.hlsl) ---

  ClusterGridSlot bindless_cluster_grid_slot;
  ClusterIndexListSlot bindless_cluster_index_list_slot;
  uint32_t cluster_dim_x { 0 };
  uint32_t cluster_dim_y { 0 };
  uint32_t cluster_dim_z { 0 };
  uint32_t light_grid_pixel_size_shift { 0 };
  float light_grid_z_params_b { 0.0F };
  float light_grid_z_params_o { 0.0F };
  float light_grid_z_params_s { 0.0F };
  uint32_t max_lights_per_cell { 0 };
  uint32_t light_grid_pixel_size_px { 0 };
  uint32_t _pad { 0 };

  //=== Computed Properties ===----------------------------------------------//

  //! Compute grid dimensions for a given screen resolution.
  struct GridDimensions {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t total_clusters;
  };

  struct ZParams {
    float b { 0.0F };
    float o { 0.0F };
    float s { 0.0F };
  };

  [[nodiscard]] constexpr auto ComputeGridDimensions(
    Extent<uint32_t> screen_size) const noexcept -> GridDimensions
  {
    const uint32_t items_x
      = (screen_size.width + kLightGridPixelSize - 1) / kLightGridPixelSize;
    const uint32_t items_y
      = (screen_size.height + kLightGridPixelSize - 1) / kLightGridPixelSize;
    const uint32_t items_z = kLightGridSizeZ;
    return {
      .x = items_x,
      .y = items_y,
      .z = items_z,
      .total_clusters = items_x * items_y * items_z,
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

  [[nodiscard]] static constexpr auto MakePublishedConfig(
    const ClusterGridSlot cluster_grid_slot,
    const ClusterIndexListSlot cluster_index_list_slot,
    const GridDimensions& grid_dimensions, const ZParams& z_params) noexcept
    -> LightCullingConfig
  {
    return LightCullingConfig {
      .bindless_cluster_grid_slot = cluster_grid_slot,
      .bindless_cluster_index_list_slot = cluster_index_list_slot,
      .cluster_dim_x = grid_dimensions.x,
      .cluster_dim_y = grid_dimensions.y,
      .cluster_dim_z = grid_dimensions.z,
      .light_grid_pixel_size_shift = kLightGridPixelSizeShift,
      .light_grid_z_params_b = z_params.b,
      .light_grid_z_params_o = z_params.o,
      .light_grid_z_params_s = z_params.s,
      .max_lights_per_cell = kMaxCulledLightsPerCell,
      .light_grid_pixel_size_px = kLightGridPixelSize,
      ._pad = 0U,
    };
  }

  [[nodiscard]] constexpr auto IsAvailable() const noexcept -> bool
  {
    return bindless_cluster_grid_slot.IsValid()
      && bindless_cluster_index_list_slot.IsValid() && cluster_dim_x > 0U
      && cluster_dim_y > 0U && cluster_dim_z > 0U
      && light_grid_pixel_size_shift > 0U && max_lights_per_cell > 0U;
  }
};

static_assert(sizeof(LightCullingConfig) == LightCullingConfig::kSize);

} // namespace oxygen::engine

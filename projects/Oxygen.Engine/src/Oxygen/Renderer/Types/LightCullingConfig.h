//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

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

//! Configuration for clustered light culling.
/*!
 Defines the grid dimensions and depth slicing parameters for Clustered Forward
 light culling.

 ### Z-Binning

 The depth range is divided using logarithmic slicing:

   slice = log(z / near) * scale + bias

 This concentrates precision near the camera where it matters most.

 ### Future: Override Attachment Integration

 This configuration can be set per-scene or per-node via `OverrideAttachment`
 with domain `kRendering`:

 | Property Key           | Type     | Description                          |
 | ---------------------- | -------- | ------------------------------------ |
 | `rndr_cluster_depth`   | uint32_t | Number of depth slices (1-64)        |
 | `rndr_cluster_tile_px` | uint32_t | Tile size in pixels (8, 16, 32)      |

 @see LightCullingPass, EnvironmentDynamicData
*/
struct LightCullingConfig {
  static constexpr size_t kSize = 48;

  static constexpr uint32_t kDefaultTileSizePx = 16;
  static constexpr uint32_t kDefaultDepthSlices = 24;
  static constexpr uint32_t kDefaultMaxLightsPerCluster = 64;
  static constexpr float kUseCameraPlanes = 0.0F;
  static constexpr float kDefaultClusteredZNear = 0.01F;
  static constexpr float kDefaultClusteredZFar = 1000.0F;

  static constexpr uint32_t kHighDensityDepthSlices = 32;
  static constexpr float kHighDensityZFarScale = 0.5F;

  // --- GPU-Compatible Data (Keep in sync with LightCulling.hlsl) ---

  ClusterGridSlot bindless_cluster_grid_slot;
  ClusterIndexListSlot bindless_cluster_index_list_slot;
  uint32_t cluster_dim_x { 0 };
  uint32_t cluster_dim_y { 0 };
  uint32_t cluster_dim_z { kDefaultDepthSlices };
  uint32_t tile_size_px { kDefaultTileSizePx };
  float z_near { kUseCameraPlanes };
  float z_far { kUseCameraPlanes };
  float z_scale { 0.0F };
  float z_bias { 0.0F };
  uint32_t max_lights_per_cluster { kDefaultMaxLightsPerCluster };
  uint32_t _pad { 0 };

  //=== Computed Properties ===----------------------------------------------//

  //! Compute grid dimensions for a given screen resolution.
  struct GridDimensions {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t total_clusters;
  };

  [[nodiscard]] constexpr auto ComputeGridDimensions(
    Extent<uint32_t> screen_size) const noexcept -> GridDimensions
  {
    const uint32_t items_x
      = (screen_size.width + tile_size_px - 1) / tile_size_px;
    const uint32_t items_y
      = (screen_size.height + tile_size_px - 1) / tile_size_px;
    const uint32_t items_z = cluster_dim_z;
    return {
      .x = items_x,
      .y = items_y,
      .z = items_z,
      .total_clusters = items_x * items_y * items_z,
    };
  }

  //! Compute Z-binning scale for logarithmic depth slicing.
  [[nodiscard]] constexpr auto ComputeZScale(
    float effective_z_near, float effective_z_far) const noexcept -> float
  {
    if (cluster_dim_z <= 1 || effective_z_near <= 0.0F
      || effective_z_far <= effective_z_near) {
      return 0.0F;
    }
    // scale = depth_slices / log2(far / near)
    const float log_ratio = std::log2(effective_z_far / effective_z_near);
    return static_cast<float>(cluster_dim_z) / log_ratio;
  }

  //! Compute Z-binning bias for logarithmic depth slicing.
  [[nodiscard]] constexpr auto ComputeZBias() const noexcept -> float
  {
    return 0.0F; // Not used in current implementation
  }

  //=== Presets ===----------------------------------------------------------//

  //! Default clustered configuration (16×16 tiles with 24 depth slices).
  static constexpr auto Default() noexcept -> LightCullingConfig
  {
    return LightCullingConfig {
      .bindless_cluster_grid_slot = ClusterGridSlot {},
      .bindless_cluster_index_list_slot = ClusterIndexListSlot {},
      .cluster_dim_z = kDefaultDepthSlices,
      .tile_size_px = kDefaultTileSizePx,
      .z_near = kDefaultClusteredZNear,
      .z_far = kDefaultClusteredZFar,
      .max_lights_per_cluster = kDefaultMaxLightsPerCluster,
    };
  }

  //! High-density clustered for complex indoor scenes.
  static constexpr auto HighDensity() noexcept -> LightCullingConfig
  {
    return LightCullingConfig {
      .bindless_cluster_grid_slot = ClusterGridSlot {},
      .bindless_cluster_index_list_slot = ClusterIndexListSlot {},
      .cluster_dim_z = kHighDensityDepthSlices,
      .tile_size_px = kDefaultTileSizePx,
      .z_near = kDefaultClusteredZNear,
      .z_far = kDefaultClusteredZFar * kHighDensityZFarScale,
      .max_lights_per_cluster = kDefaultMaxLightsPerCluster * 2,
    };
  }
};

static_assert(sizeof(LightCullingConfig) == LightCullingConfig::kSize);

} // namespace oxygen::engine

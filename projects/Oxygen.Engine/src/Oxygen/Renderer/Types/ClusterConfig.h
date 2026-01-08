//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cmath>
#include <cstdint>

namespace oxygen::engine {

//! Configuration for clustered/tiled light culling.
/*!
 Defines the grid dimensions and depth slicing parameters for Forward+ light
 culling. Supports both tile-based (2D) and clustered (3D) configurations.

 ### Tile-Based vs Clustered

 - **Tile-based (Forward+)**: Set `depth_slices = 1`. The grid is 2D with
   per-tile min/max depth bounds from the depth prepass.
 - **Clustered**: Set `depth_slices > 1`. The grid becomes 3D with explicit
   depth slices (froxels) for tighter culling in depth-complex scenes.

 ### Z-Binning

 When `depth_slices > 1`, the depth range is divided using logarithmic slicing:

   slice = log(z / near) * scale + bias

 This concentrates precision near the camera where it matters most.

 ### Future: Override Attachment Integration

 This configuration can be set per-scene or per-node via `OverrideAttachment`
 with domain `kRendering`:

 | Property Key           | Type     | Description                          |
 | ---------------------- | -------- | ------------------------------------ |
 | `rndr_cluster_mode`    | uint32_t | 0=tile-based, 1=clustered            |
 | `rndr_cluster_depth`   | uint32_t | Number of depth slices (1-64)        |
 | `rndr_cluster_tile_px` | uint32_t | Tile size in pixels (8, 16, 32)      |

 @see LightCullingPass, EnvironmentDynamicData
*/
struct ClusterConfig {
  //! Tile size in pixels (width and height). Common values: 8, 16, 32.
  uint32_t tile_size_px { 16 };

  //! Number of depth slices. Set to 1 for tile-based (2D), >1 for clustered.
  uint32_t depth_slices { 1 };

  //! Maximum lights per cluster/tile before clamping.
  uint32_t max_lights_per_cluster { 64 };

  //! Near plane for Z-binning. Set to 0 to use camera near plane (recommended).
  float z_near { 0.0F };

  //! Far plane for Z-binning. Set to 0 to use camera far plane (recommended).
  float z_far { 0.0F };

  //=== Computed Properties ===----------------------------------------------//

  //! Compute grid dimensions for a given screen resolution.
  struct GridDimensions {
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint32_t total_clusters;
  };

  [[nodiscard]] constexpr auto ComputeGridDimensions(uint32_t screen_width,
    uint32_t screen_height) const noexcept -> GridDimensions
  {
    const uint32_t tiles_x = (screen_width + tile_size_px - 1) / tile_size_px;
    const uint32_t tiles_y = (screen_height + tile_size_px - 1) / tile_size_px;
    const uint32_t tiles_z = depth_slices;
    return { tiles_x, tiles_y, tiles_z, tiles_x * tiles_y * tiles_z };
  }

  //! Compute Z-binning scale for logarithmic depth slicing.
  [[nodiscard]] constexpr auto ComputeZScale() const noexcept -> float
  {
    if (depth_slices <= 1 || z_near <= 0.0F || z_far <= z_near) {
      return 0.0F;
    }
    // scale = depth_slices / log(far / near)
    // Using log2 for better precision
    const float log_ratio = std::log2(z_far / z_near);
    return static_cast<float>(depth_slices) / log_ratio;
  }

  //! Compute Z-binning bias for logarithmic depth slicing.
  /*!
   @note Currently unused - the simplified formula slice = log2(z/z_near) *
   scale does not require a bias term.
  */
  [[nodiscard]] constexpr auto ComputeZBias() const noexcept -> float
  {
    return 0.0F; // Not used in current implementation
  }

  //=== Presets ===----------------------------------------------------------//

  //! Standard tile-based Forward+ configuration (16×16 tiles, no depth slices).
  //!
  //! Uses per-tile depth bounds from the depth prepass for tight culling.
  //! z_near/z_far are still used for cluster grid sizing.
  static constexpr auto TileBased() noexcept -> ClusterConfig
  {
    return ClusterConfig {
      .tile_size_px = 16,
      .depth_slices = 1,
      .max_lights_per_cluster = 64,
      .z_near = 0.1F,
      .z_far = 1000.0F,
    };
  }

  //! Clustered configuration (16×16 tiles with 24 depth slices).
  //!
  //! Uses logarithmic depth distribution: slice = log2(z / z_near) × scale.
  //! A smaller z_near increases slice thickness at far distances, improving
  //! stability at the cost of wasting some slices on the very near range.
  //!
  //! Recommended: Set z_near to 10× smaller than your camera near plane
  //! for stable visualization with minimal precision loss.
  static constexpr auto Clustered() noexcept -> ClusterConfig
  {
    return ClusterConfig {
      .tile_size_px = 16,
      .depth_slices = 24,
      .max_lights_per_cluster = 64,
      .z_near = 0.01F, // Smaller for stability (see class docs)
      .z_far = 1000.0F,
    };
  }

  //! High-density clustered for complex indoor scenes.
  //!
  //! Uses 8×8 tiles and 32 depth slices for finer culling granularity.
  //! Better for scenes with many small, overlapping lights.
  static constexpr auto ClusteredHighDensity() noexcept -> ClusterConfig
  {
    return ClusterConfig {
      .tile_size_px = 16, // Fixed at 16 (compile-time shader constant)
      .depth_slices = 32,
      .max_lights_per_cluster = 128,
      .z_near = 0.01F,
      .z_far = 500.0F,
    };
  }
};

static_assert(ClusterConfig::TileBased().depth_slices == 1,
  "TileBased preset must have depth_slices == 1");

} // namespace oxygen::engine

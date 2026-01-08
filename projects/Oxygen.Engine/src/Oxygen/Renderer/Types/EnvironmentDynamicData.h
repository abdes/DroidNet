//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <Oxygen/Renderer/Types/SceneConstants.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen::engine {

//! Per-frame environment payload consumed directly by shaders.
/*!
 This structure is intended to be bound as a **root CBV** (register b3) and is
 therefore kept small and frequently updated ("hot").

 The payload contains:

 - View-exposure values computed by the renderer (potentially from authored
   post-process settings + auto-exposure).
 - Bindless SRV slots and dimensions for clustered lighting buffers.
 - Z-binning parameters for clustered light culling.

 Layout mirrors the HLSL struct `EnvironmentDynamicData`.

 ### Cluster Configuration

 For tile-based Forward+ (cluster_dim_z == 1):
 - Only X/Y dimensions are used
 - Z-binning parameters are ignored
 - Per-tile min/max depth from depth prepass provides depth bounds

 For clustered (cluster_dim_z > 1):
 - Full 3D grid with logarithmic depth slices
 - Z-binning: slice = log2(z / z_near) * z_scale + z_bias

 ### Ownership and Future Extension

 Currently owned and uploaded by **LightCullingPass**, which populates cluster
 data and uses default values (1.0) for exposure/white_point.

 When post-process/auto-exposure is implemented, the owning pass has two
 options:
 1. Query the LightCullingPass buffer and overwrite exposure fields, or
 2. Split into separate CBVs (ClusterData on b3, ExposureData on b4).

 @warning This struct must remain 16-byte aligned for D3D12 root CBV bindings.
 @see ClusterConfig, LightCullingPass
*/
struct alignas(16) EnvironmentDynamicData {
  // Exposure and tonemapping
  float exposure { 1.0F };
  float white_point { 1.0F };

  // Cluster grid bindless slots (from LightCullingPass)
  uint32_t bindless_cluster_grid_slot { kInvalidDescriptorSlot };
  uint32_t bindless_cluster_index_list_slot { kInvalidDescriptorSlot };

  // Cluster grid dimensions
  uint32_t cluster_dim_x { 0 };
  uint32_t cluster_dim_y { 0 };
  uint32_t cluster_dim_z { 0 };
  uint32_t tile_size_px { 16 };

  // Z-binning parameters for clustered lighting
  float z_near { 0.1F };
  float z_far { 1000.0F };
  float z_scale { 0.0F };
  float z_bias { 0.0F };
};
static_assert(sizeof(EnvironmentDynamicData) % 16 == 0,
  "EnvironmentDynamicData size must be 16-byte aligned");
static_assert(sizeof(EnvironmentDynamicData) == 48,
  "EnvironmentDynamicData size must match HLSL packing");

} // namespace oxygen::engine

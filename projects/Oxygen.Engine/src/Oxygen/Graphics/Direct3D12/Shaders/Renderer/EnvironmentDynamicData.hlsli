//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI

// Mirrors oxygen::engine::EnvironmentDynamicData (sizeof = 64)
// Per-frame environment payload bound as root CBV at b3.
struct EnvironmentDynamicData
{
    // Exposure and tonemapping
    float exposure;
    float white_point;

    // Cluster grid bindless slots (from LightCullingPass)
    uint bindless_cluster_grid_slot;
    uint bindless_cluster_index_list_slot;

    // Cluster grid dimensions
    uint cluster_dim_x;
    uint cluster_dim_y;
    uint cluster_dim_z;
    uint tile_size_px;

    // Z-binning parameters for clustered lighting
    float z_near;
    float z_far;
    float z_scale;
    float z_bias;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI

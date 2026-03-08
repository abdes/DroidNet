//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_LIGHTCULLINGCONFIG_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_LIGHTCULLINGCONFIG_HLSLI

struct LightCullingConfig
{
    uint bindless_cluster_grid_slot;
    uint bindless_cluster_index_list_slot;
    uint cluster_dim_x;
    uint cluster_dim_y;
    uint cluster_dim_z;
    uint tile_size_px;
    float z_near;
    float z_far;
    float z_scale;
    float z_bias;
    uint max_lights_per_cluster;
    uint _pad;
};

#endif // OXYGEN_D3D12_SHADERS_RENDERER_LIGHTCULLINGCONFIG_HLSLI

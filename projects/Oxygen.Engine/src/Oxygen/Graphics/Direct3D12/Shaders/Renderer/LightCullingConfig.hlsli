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
    uint light_grid_pixel_size_shift;
    float light_grid_z_params_b;
    float light_grid_z_params_o;
    float light_grid_z_params_s;
    uint max_lights_per_cell;
    uint light_grid_pixel_size_px;
    uint _pad;
};

#endif // OXYGEN_D3D12_SHADERS_RENDERER_LIGHTCULLINGCONFIG_HLSLI

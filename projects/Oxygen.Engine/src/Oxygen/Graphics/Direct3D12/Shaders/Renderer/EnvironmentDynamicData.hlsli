//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI

// Mirrors oxygen::engine::EnvironmentDynamicData (sizeof = 32)
struct EnvironmentDynamicData
{
    float exposure;
    float white_point;

    uint bindless_cluster_grid_slot;
    uint bindless_cluster_index_list_slot;

    uint cluster_dim_x;
    uint cluster_dim_y;
    uint cluster_dim_z;
    uint _pad0;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI

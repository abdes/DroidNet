//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI
#define OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Lighting/LightGridData.hlsli"

// Clustered light-grid lookup utilities shared by shading and debug consumers.
//
// The light grid is always a 3D clustered structure. XY cells are derived from
// a fixed power-of-two pixel size; Z slices use the UE-style
// `log2(depth * B + O) * S` mapping published in `LightCullingConfig`.

//=== Cluster Index Computation ===-------------------------------------------//

uint ComputeClusterZSlice(
    float linear_depth,
    float3 light_grid_z_params,
    uint cluster_dim_z)
{
    if (cluster_dim_z == 0u || linear_depth <= 0.0f
        || light_grid_z_params.x <= 0.0f
        || light_grid_z_params.z <= 0.0f) {
        return 0u;
    }

    const float encoded_depth
        = linear_depth * light_grid_z_params.x + light_grid_z_params.y;
    if (encoded_depth <= 0.0f) {
        return 0u;
    }

    const float z_slice_f = log2(encoded_depth) * light_grid_z_params.z;
    return min((uint)max(z_slice_f, 0.0f), cluster_dim_z - 1u);
}

// Compute cluster index from screen coordinates and linear depth.
uint ComputeClusterIndex(
    float2 screen_pos,
    float linear_depth,
    uint3 cluster_dims,
    uint light_grid_pixel_size_shift,
    float3 light_grid_z_params)
{
    if (any(cluster_dims == 0u)) {
        return 0u;
    }

    const uint2 cluster_xy = min(
        uint2(screen_pos) >> light_grid_pixel_size_shift,
        cluster_dims.xy - 1u);
    const uint z_slice = ComputeClusterZSlice(
        linear_depth, light_grid_z_params, cluster_dims.z);

    return z_slice * (cluster_dims.x * cluster_dims.y)
         + cluster_xy.y * cluster_dims.x
         + cluster_xy.x;
}

//=== Light List Access ===---------------------------------------------------//

// Read the canonical range produced by the lighting publisher/culler.
ClusterLightRange GetClusterLightRange(uint cluster_grid_slot, uint cluster_index)
{
    if (cluster_grid_slot == K_INVALID_BINDLESS_INDEX) {
        return (ClusterLightRange)0;
    }
    StructuredBuffer<ClusterLightRange> cluster_grid = ResourceDescriptorHeap[cluster_grid_slot];
    return cluster_grid[cluster_index];
}

#endif // OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI

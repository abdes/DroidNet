//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI
#define OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

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

// Cluster grid entry: packed (offset, count)
struct ClusterLightInfo {
    uint light_list_offset;  // Offset into the light index list
    uint light_count;        // Number of lights in this cluster
};

// Get light info for a cluster from the cluster grid buffer.
//
// @param cluster_grid_slot  Bindless slot for the cluster grid buffer
// @param cluster_index  Linear cluster index
// @return Cluster light info (offset and count)
ClusterLightInfo GetClusterLightInfo(uint cluster_grid_slot, uint cluster_index)
{
    ClusterLightInfo info;
    info.light_list_offset = 0;
    info.light_count = 0;

    if (cluster_grid_slot == K_INVALID_BINDLESS_INDEX) {
        return info;
    }

    StructuredBuffer<uint2> cluster_grid = ResourceDescriptorHeap[cluster_grid_slot];
    uint2 packed = cluster_grid[cluster_index];
    info.light_list_offset = packed.x;
    info.light_count = packed.y;

    return info;
}

// Get a light index from the light index list.
//
// @param light_list_slot  Bindless slot for the light index list buffer
// @param offset  Offset from GetClusterLightInfo
// @param local_index  Index within the cluster's light list [0, light_count)
// @return Global light index into the positional lights buffer
uint GetClusterLightIndex(uint light_list_slot, uint offset, uint local_index)
{
    if (light_list_slot == K_INVALID_BINDLESS_INDEX) {
        return 0xFFFFFFFF; // Invalid
    }

    StructuredBuffer<uint> light_list = ResourceDescriptorHeap[light_list_slot];
    return light_list[offset + local_index];
}

//=== Convenience Macros ===--------------------------------------------------//

// Helper macro to iterate over cluster lights.
// Usage:
//   CLUSTER_LIGHT_LOOP_BEGIN(cluster_grid_slot, light_list_slot, cluster_index)
//       PositionalLightData light = positional_lights[light_index];
//       // ... process light ...
//   CLUSTER_LIGHT_LOOP_END

#define CLUSTER_LIGHT_LOOP_BEGIN(grid_slot, list_slot, cluster_idx) \
    { \
        ClusterLightInfo _cluster_info = GetClusterLightInfo(grid_slot, cluster_idx); \
        for (uint _light_i = 0; _light_i < _cluster_info.light_count; ++_light_i) { \
            uint light_index = GetClusterLightIndex(list_slot, \
                _cluster_info.light_list_offset, _light_i); \
            if (light_index == 0xFFFFFFFF) continue;

#define CLUSTER_LIGHT_LOOP_END \
        } \
    }

#endif // OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI

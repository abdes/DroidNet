//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI
#define OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI

// Cluster/tile lookup utilities for Forward+ light culling.
//
// This header provides functions to compute cluster indices from screen
// coordinates and depth, and to iterate over lights affecting a cluster.
//
// === Tile-Based vs Clustered ===
// - Tile-based (cluster_dim_z == 1): Only X/Y used, depth bounds from prepass
// - Clustered (cluster_dim_z > 1): X/Y/Z with logarithmic depth slicing
//
// === Usage ===
// 1. Call GetClusterIndex() with screen position and linear depth
// 2. Call GetClusterLightInfo() to get offset and count
// 3. Loop through lights using the light index list

//=== Cluster Index Computation ===-------------------------------------------//

// Compute cluster index from screen coordinates and linear depth.
// For tile-based (cluster_dim_z == 1), z_slice is always 0.
// For clustered, z_slice is computed using logarithmic depth distribution.
//
// @param screen_pos  Screen coordinates (pixels)
// @param linear_depth  Linear depth value (world units from camera)
// @param screen_dims  Screen dimensions (pixels)
// @param cluster_dims  Cluster grid dimensions (tiles_x, tiles_y, depth_slices)
// @param tile_size  Tile size in pixels
// @param z_near  Near plane for Z-binning
// @param z_scale  Logarithmic Z scale factor
// @param z_bias  Logarithmic Z bias factor
// @return Linear cluster index into the cluster grid
uint ComputeClusterIndex(
    float2 screen_pos,
    float linear_depth,
    float2 screen_dims,
    uint3 cluster_dims,
    uint tile_size,
    float z_near,
    float z_scale,
    float z_bias)
{
    // Compute tile X/Y from screen position
    uint tile_x = uint(screen_pos.x) / tile_size;
    uint tile_y = uint(screen_pos.y) / tile_size;

    // Clamp to valid range
    tile_x = min(tile_x, cluster_dims.x - 1);
    tile_y = min(tile_y, cluster_dims.y - 1);

    // Compute Z slice
    uint z_slice = 0;
    if (cluster_dims.z > 1) {
        // Logarithmic depth slicing: slice = log2(z / near) * scale + bias
        float z_ratio = max(linear_depth / z_near, 1.0);
        z_slice = uint(log2(z_ratio) * z_scale + z_bias);
        z_slice = clamp(z_slice, 0u, cluster_dims.z - 1u);
    }

    // Linear cluster index: z * (x_dim * y_dim) + y * x_dim + x
    return z_slice * (cluster_dims.x * cluster_dims.y)
         + tile_y * cluster_dims.x
         + tile_x;
}

// Simplified cluster index for tile-based (no depth slicing)
uint ComputeTileIndex(float2 screen_pos, float2 screen_dims, uint tile_size)
{
    uint tile_x = uint(screen_pos.x) / tile_size;
    uint tile_y = uint(screen_pos.y) / tile_size;
    uint tiles_x = (uint(screen_dims.x) + tile_size - 1) / tile_size;
    return tile_y * tiles_x + tile_x;
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

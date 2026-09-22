//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI
#define OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Lighting/LightGridData.hlsli"

// Per-view lookup uses the same content rectangle and depth convention as the builder.
LightGridMetadata LoadLightGridMetadata(uint slot)
{
    if (!BX_IN_GLOBAL_SRV(slot)) {
        return (LightGridMetadata)0;
    }
    StructuredBuffer<LightGridMetadata> records = ResourceDescriptorHeap[slot];
    return records[0];
}

uint ComputeClusterZSlice(float view_depth, LightGridMetadata grid)
{
    if (grid.grid_size.z == 0u) {
        return 0u;
    }
    float slice = 0.0f;
    if (grid.projection_kind == LIGHT_GRID_ORTHOGRAPHIC) {
        const float depth_span = grid.far_depth_m - grid.near_depth_m;
        if (depth_span <= 0.0f) {
            return 0u;
        }
        slice = (view_depth - grid.near_depth_m) / depth_span * grid.grid_size.z;
    } else {
        const float encoded_depth = view_depth * grid.grid_z_params.x + grid.grid_z_params.y;
        if (encoded_depth <= 0.0f || grid.grid_z_params.x <= 0.0f || grid.grid_z_params.z <= 0.0f) {
            return 0u;
        }
        slice = log2(encoded_depth) * grid.grid_z_params.z;
    }
    return (uint)clamp(slice, 0.0f, (float)(grid.grid_size.z - 1u));
}

uint ComputeClusterIndex(float2 screen_pos, float view_depth, LightGridMetadata grid)
{
    if (any(grid.grid_size == 0u)) {
        return 0u;
    }
    // Preserve fractional final tiles; subtracting a whole pixel can erase one.
    // The integer grid clamp below also handles the exact upper boundary.
    const float2 local_pixel = clamp(screen_pos - grid.content_origin_px,
        0.0f.xx, max(grid.content_extent_px, 0.0f.xx));
    const uint2 cluster_xy = min(uint2(local_pixel) >> grid.pixel_size_shift,
        grid.grid_size.xy - 1u);
    const uint z_slice = ComputeClusterZSlice(view_depth, grid);
    return z_slice * (grid.grid_size.x * grid.grid_size.y)
        + cluster_xy.y * grid.grid_size.x + cluster_xy.x;
}

ClusterLightRange GetClusterLightRange(uint cluster_grid_slot, uint cluster_index)
{
    if (!BX_IN_GLOBAL_SRV(cluster_grid_slot)) {
        return (ClusterLightRange)0;
    }
    StructuredBuffer<ClusterLightRange> cluster_grid = ResourceDescriptorHeap[cluster_grid_slot];
    return cluster_grid[cluster_index];
}

// Transient evaluator state, not a published wire record.
struct ClusterLightIteration {
    uint indices_srv;
    uint offset;
    uint count;
    bool complete;
};

bool TryResolveClusterLightIteration(ClusterLightRange range, uint local_count,
    uint indices_srv, out ClusterLightIteration iteration)
{
    iteration = (ClusterLightIteration)0;
    iteration.indices_srv = indices_srv;
    if (range.offset == COMPLETE_LIGHT_LIST_OFFSET) {
        if (range.count != local_count) {
            return false;
        }
        iteration.count = local_count;
        iteration.complete = true;
        return true;
    }
    if (range.count == 0u) {
        return range.offset == 0u;
    }
    if (range.count > local_count || !BX_IN_GLOBAL_SRV(indices_srv)) {
        return false;
    }
    StructuredBuffer<uint> indices = ResourceDescriptorHeap[indices_srv];
    uint capacity = 0u, stride = 0u;
    indices.GetDimensions(capacity, stride);
    if (range.offset > capacity || range.count > capacity - range.offset) {
        return false;
    }
    iteration.offset = range.offset;
    iteration.count = range.count;
    return true;
}

uint LoadClusterLightIndex(ClusterLightIteration iteration, uint ordinal)
{
    if (iteration.complete) {
        return ordinal;
    }
    StructuredBuffer<uint> indices = ResourceDescriptorHeap[iteration.indices_srv];
    return indices[iteration.offset + ordinal];
}

#endif // OXYGEN_D3D12_SHADERS_PASSES_LIGHTING_CLUSTERLOOKUP_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Lighting/LightingFrameBindings.hlsli"
#include "Vortex/Contracts/Lighting/ForwardLocalLightRecord.hlsli"
#include "Vortex/Contracts/Lighting/LightGridData.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

uint2 AddCount(uint2 a, uint2 b)
{
    uint lo = a.x + b.x;
    return uint2(lo, a.y + b.y + (lo < a.x ? 1u : 0u));
}

float SliceDepth(uint slice, LightGridMetadata grid)
{
    float fraction = (float)slice / (float)grid.grid_size.z;
    if (grid.projection_kind == LIGHT_GRID_PERSPECTIVE)
        fraction = (exp2((float)slice / grid.grid_z_params.z) - 1.0f)
            / grid.grid_z_params.y;
    return lerp(grid.near_depth_m, grid.far_depth_m, saturate(fraction));
}

bool CellBounds(uint cell, LightGridMetadata grid, LightGridPassConstants pass,
    out float3 lower, out float3 upper)
{
    uint z = cell / (grid.grid_size.x * grid.grid_size.y);
    uint xy = cell % (grid.grid_size.x * grid.grid_size.y);
    uint2 tile = uint2(xy % grid.grid_size.x, xy / grid.grid_size.x);
    float2 first = float2(tile << grid.pixel_size_shift);
    float2 last = min(float2((tile + 1u) << grid.pixel_size_shift), grid.content_extent_px);
    float2 ndc_first = first / grid.content_extent_px * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    float2 ndc_last = last / grid.content_extent_px * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f);
    lower = 3.402823466e+38f.xxx;
    upper = -lower;
    [unroll] for (uint corner = 0u; corner < 8u; ++corner)
    {
        float view_z = -SliceDepth(z + ((corner >> 2u) & 1u), grid);
        float clip_z = (pass.depth_projection.x * view_z + pass.depth_projection.y)
            / (pass.depth_projection.z * view_z + pass.depth_projection.w);
        float4 p = mul(pass.inverse_projection, float4(
            (corner & 1u) ? ndc_last.x : ndc_first.x,
            (corner & 2u) ? ndc_last.y : ndc_first.y, clip_z, 1.0f));
        p.xyz /= p.w;
        if (!all(isfinite(p.xyz))) return false;
        lower = min(lower, p.xyz);
        upper = max(upper, p.xyz);
    }
    return true;
}

float4 PrepareLightBounds(ForwardLocalLightRecord light, LightGridPassConstants pass)
{
    if (light.range_m <= 0.0f || !any(light.intensity_rgb_cd > 0.0f))
        return float4(0.0f, 0.0f, 0.0f, -1.0f);
    float3 center = mul(pass.view_matrix, float4(light.position_ws, 1.0f)).xyz;
    // The row-sum bound on A*A^T covers non-rigid view transforms as well.
    float3 a = pass.view_matrix[0].xyz;
    float3 b = pass.view_matrix[1].xyz;
    float3 c = pass.view_matrix[2].xyz;
    float scale2 = max(dot(a, a) + abs(dot(a, b)) + abs(dot(a, c)),
        max(dot(b, b) + abs(dot(a, b)) + abs(dot(b, c)),
            dot(c, c) + abs(dot(a, c)) + abs(dot(b, c))));
    float radius = light.range_m * sqrt(max(scale2, 0.0f));
    radius += 1.0e-4f * max(1.0f, max(radius, max(abs(center.x), max(abs(center.y), abs(center.z)))));
    return float4(center, radius);
}

groupshared float4 g_LocalLightBounds[64];
groupshared uint g_ActiveCells;

uint VisitLocalLights(StructuredBuffer<ForwardLocalLightRecord> lights,
    LightingFrameBindings lighting, LightGridPassConstants pass,
    uint group_index, bool active, float3 lower, float3 upper,
    bool emit_indices, uint output_offset)
{
    if (group_index == 0u) g_ActiveCells = 0u;
    GroupMemoryBarrierWithGroupSync();
    if (active) InterlockedOr(g_ActiveCells, 1u);
    GroupMemoryBarrierWithGroupSync();
    // This decision is uniform for the group, including partially filled groups.
    if (g_ActiveCells == 0u) return 0u;

    uint count = 0u;
    for (uint first = 0u; first < lighting.local_count;) {
        const uint batch_count = min(64u, lighting.local_count - first);
        if (group_index < batch_count)
            g_LocalLightBounds[group_index] = PrepareLightBounds(lights[first + group_index], pass);
        GroupMemoryBarrierWithGroupSync();
        if (active) {
            for (uint i = 0u; i < batch_count; ++i) {
                const float4 bound = g_LocalLightBounds[i];
                const float3 delta = bound.xyz - clamp(bound.xyz, lower, upper);
                if (bound.w >= 0.0f && dot(delta, delta) <= bound.w * bound.w) {
                    if (emit_indices) {
                        RWStructuredBuffer<uint> indices = ResourceDescriptorHeap[pass.indices_uav];
                        indices[output_offset + count] = first + i;
                    }
                    ++count;
                }
            }
        }
        // No lane may overwrite a batch while another cell still consumes it.
        GroupMemoryBarrierWithGroupSync();
        first += batch_count;
    }
    return count;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void SpatialLightGridCS(uint3 dispatch_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex)
{
    StructuredBuffer<LightGridPassConstants> passes = ResourceDescriptorHeap[g_PassConstantsIndex];
    LightGridPassConstants pass = passes[0];
    LightingFrameBindings lighting = LoadLightingFrameBindings(pass.lighting_bindings_srv);
    RWStructuredBuffer<LightGridBuildStatus> status = ResourceDescriptorHeap[pass.status_uav];
    uint cell = dispatch_id.x;
    if (pass.subpass == 0u)
    {
        if (cell == 0u)
        {
            LightGridBuildStatus initial = (LightGridBuildStatus)0;
            initial.selection_revision = lighting.selection_revision;
            status[0] = initial;
        }
        return;
    }
    RWStructuredBuffer<uint2> source = ResourceDescriptorHeap[pass.offsets_uav];
    if (pass.subpass == 4u)
    {
        if (cell == 0u)
        {
            status[0].required_index_count = source[pass.work_count - 1u];
            if (status[0].state != LIGHT_GRID_BUILD_FAILED)
                status[0].state = LIGHT_GRID_BUILD_VALID;
        }
        return;
    }
    if (pass.subpass == 2u)
    {
        if (cell >= pass.work_count) return;
        RWStructuredBuffer<uint2> destination = ResourceDescriptorHeap[pass.scan_destination_uav];
        uint2 sum = source[cell];
        if (cell >= pass.scan_stride) sum = AddCount(sum, source[cell - pass.scan_stride]);
        destination[cell] = sum;
        return;
    }

    StructuredBuffer<LightGridMetadata> metadata = ResourceDescriptorHeap[lighting.grid_metadata_srv];
    StructuredBuffer<ForwardLocalLightRecord> lights = ResourceDescriptorHeap[lighting.local_records_srv];
    RWStructuredBuffer<uint> counts = ResourceDescriptorHeap[pass.counts_uav];
    const bool owns_cell = cell < pass.work_count;
    float3 lower = 0.0f.xxx, upper = 0.0f.xxx;
    bool valid = false;
    if (owns_cell) valid = CellBounds(cell, metadata[0], pass, lower, upper);
    if (pass.subpass == 1u)
    {
        if (owns_cell && !valid)
        {
            InterlockedMax(status[0].state, LIGHT_GRID_BUILD_FAILED);
            InterlockedOr(status[0].reason, LIGHT_GRID_REASON_INVALID_BOUNDS);
        }
        const uint count = VisitLocalLights(lights, lighting, pass,
            group_index, valid, lower, upper, false, 0u);
        if (owns_cell) {
            counts[cell] = count;
            source[cell] = uint2(count, 0u);
        }
        return;
    }

    RWStructuredBuffer<ClusterLightRange> ranges = ResourceDescriptorHeap[pass.ranges_uav];
    ClusterLightRange range = (ClusterLightRange)0;
    uint count = 0u;
    uint2 offset = 0u.xx;
    bool emit_indices = false;
    if (owns_cell) {
        count = counts[cell];
        if (count != 0u) {
            const uint2 end = source[cell];
            offset = uint2(end.x - count, end.y - (end.x < count ? 1u : 0u));
            if (offset.y != 0u || offset.x > lighting.index_capacity
                || count > lighting.index_capacity - offset.x) {
                range.offset = COMPLETE_LIGHT_LIST_OFFSET;
                range.count = lighting.local_count;
                InterlockedAdd(status[0].fallback_cell_count, 1u);
                if (status[0].state != LIGHT_GRID_BUILD_FAILED)
                    InterlockedOr(status[0].reason, LIGHT_GRID_REASON_CAPACITY);
            } else {
                range.offset = offset.x;
                range.count = count;
                emit_indices = valid;
            }
        }
    }
    // Empty/fallback/padded cells cannot return before the group's barriers.
    VisitLocalLights(lights, lighting, pass, group_index, emit_indices,
        lower, upper, true, offset.x);
    if (owns_cell) ranges[cell] = range;
    if (emit_indices) InterlockedAdd(status[0].written_index_count, count);
}

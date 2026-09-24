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

bool Intersects(ForwardLocalLightRecord light, LightGridPassConstants pass,
    float3 lower, float3 upper)
{
    if (light.range_m <= 0.0f || !any(light.intensity_rgb_cd > 0.0f)) return false;
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
    float3 delta = center - clamp(center, lower, upper);
    return dot(delta, delta) <= radius * radius;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void SpatialLightGridCS(uint3 dispatch_id : SV_DispatchThreadID)
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
    if (cell >= pass.work_count) return;
    if (pass.subpass == 2u)
    {
        RWStructuredBuffer<uint2> destination = ResourceDescriptorHeap[pass.scan_destination_uav];
        uint2 sum = source[cell];
        if (cell >= pass.scan_stride) sum = AddCount(sum, source[cell - pass.scan_stride]);
        destination[cell] = sum;
        return;
    }

    StructuredBuffer<LightGridMetadata> metadata = ResourceDescriptorHeap[lighting.grid_metadata_srv];
    StructuredBuffer<ForwardLocalLightRecord> lights = ResourceDescriptorHeap[lighting.local_records_srv];
    RWStructuredBuffer<uint> counts = ResourceDescriptorHeap[pass.counts_uav];
    float3 lower, upper;
    bool valid = CellBounds(cell, metadata[0], pass, lower, upper);
    if (pass.subpass == 1u)
    {
        uint count = 0u;
        if (!valid)
        {
            InterlockedMax(status[0].state, LIGHT_GRID_BUILD_FAILED);
            InterlockedOr(status[0].reason, LIGHT_GRID_REASON_INVALID_BOUNDS);
        }
        else
        {
            for (uint i = 0u; i < lighting.local_count; ++i)
                count += Intersects(lights[i], pass, lower, upper) ? 1u : 0u;
        }
        counts[cell] = count;
        source[cell] = uint2(count, 0u);
        return;
    }

    RWStructuredBuffer<ClusterLightRange> ranges = ResourceDescriptorHeap[pass.ranges_uav];
    uint count = counts[cell];
    ClusterLightRange range = (ClusterLightRange)0;
    if (count == 0u)
    {
        ranges[cell] = range;
        return;
    }
    uint2 end = source[cell];
    uint2 offset = uint2(end.x - count, end.y - (end.x < count ? 1u : 0u));
    if (offset.y != 0u || offset.x > lighting.index_capacity
        || count > lighting.index_capacity - offset.x)
    {
        range.offset = COMPLETE_LIGHT_LIST_OFFSET;
        range.count = lighting.local_count;
        ranges[cell] = range;
        InterlockedAdd(status[0].fallback_cell_count, 1u);
        if (status[0].state != LIGHT_GRID_BUILD_FAILED)
            InterlockedOr(status[0].reason, LIGHT_GRID_REASON_CAPACITY);
        return;
    }
    RWStructuredBuffer<uint> indices = ResourceDescriptorHeap[pass.indices_uav];
    uint ordinal = 0u;
    for (uint i = 0u; i < lighting.local_count; ++i)
        if (Intersects(lights[i], pass, lower, upper)) indices[offset.x + ordinal++] = i;
    range.offset = offset.x;
    range.count = count;
    ranges[cell] = range;
    InterlockedAdd(status[0].written_index_count, count);
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/ConventionalShadowReceiverAnalysis.hlsli"
#include "Renderer/ConventionalShadowReceiverAnalysisJob.hlsli"
#include "Renderer/ConventionalShadowReceiverMask.hlsli"
#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint CONVENTIONAL_SHADOW_RECEIVER_MASK_SCREEN_THREAD_GROUP_SIZE = 8u;
static const uint CONVENTIONAL_SHADOW_RECEIVER_MASK_LINEAR_THREAD_GROUP_SIZE = 64u;

struct ConventionalShadowReceiverMaskPassConstants
{
    uint depth_texture_index;
    uint job_buffer_index;
    uint analysis_buffer_index;
    uint raw_mask_uav_index;
    uint raw_mask_srv_index;
    uint base_mask_uav_index;
    uint base_mask_srv_index;
    uint hierarchy_mask_uav_index;
    uint hierarchy_mask_srv_index;
    uint count_buffer_uav_index;
    uint count_buffer_srv_index;
    uint summary_buffer_uav_index;
    uint2 screen_dimensions;
    uint job_count;
    uint base_tile_resolution;
    uint hierarchy_tile_resolution;
    uint base_tiles_per_job;
    uint hierarchy_tiles_per_job;
    uint hierarchy_reduction;
    float4x4 inverse_view_projection;
    float4x4 view_matrix;
};

static float3 ReconstructWorldPosition(
    const uint2 pixel_coord,
    const float depth,
    const uint2 screen_dimensions,
    const float4x4 inverse_view_projection)
{
    const float2 uv
        = (float2(pixel_coord) + 0.5f) / float2(screen_dimensions);
    const float2 ndc_xy = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 clip = float4(ndc_xy, depth, 1.0f);
    const float4 world = mul(inverse_view_projection, clip);
    return world.xyz / max(world.w, 1.0e-6f);
}

static float ComputeEyeDepth(
    const float3 world_position_ws,
    const float4x4 view_matrix)
{
    const float4 view_position = mul(view_matrix, float4(world_position_ws, 1.0f));
    return max(0.0f, -view_position.z);
}

static uint ComputeDilationTileRadius(
    const float2 full_half_extent,
    const float xy_dilation_margin,
    const uint base_tile_resolution)
{
    if (base_tile_resolution == 0u || xy_dilation_margin <= 0.0f) {
        return 0u;
    }

    const float tiles_per_unit_x
        = float(base_tile_resolution) / max(2.0f * full_half_extent.x, 1.0e-6f);
    const float tiles_per_unit_y
        = float(base_tile_resolution) / max(2.0f * full_half_extent.y, 1.0e-6f);
    const float dilation_tiles
        = ceil(xy_dilation_margin * max(tiles_per_unit_x, tiles_per_unit_y));
    return (uint)max(dilation_tiles, 0.0f);
}

static int2 ComputeFullRectTileCoord(
    const float2 light_space_xy,
    const float4 full_rect_center_half_extent,
    const uint base_tile_resolution)
{
    const float2 center = full_rect_center_half_extent.xy;
    const float2 half_extent = max(
        full_rect_center_half_extent.zw, float2(1.0e-6f, 1.0e-6f));
    const float2 normalized = saturate(
        ((light_space_xy - center) / half_extent) * 0.5f + 0.5f);
    const float2 unclamped = normalized * float(base_tile_resolution);
    const int2 tile_coord = int2(floor(unclamped));
    return clamp(tile_coord, int2(0, 0),
        int2(base_tile_resolution - 1u, base_tile_resolution - 1u));
}

static uint CountBufferBaseIndex(const uint job_index)
{
    return job_index * 2u;
}

[shader("compute")]
[numthreads(CONVENTIONAL_SHADOW_RECEIVER_MASK_LINEAR_THREAD_GROUP_SIZE, 1, 1)]
void CS_ClearMasks(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ConventionalShadowReceiverMaskPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IsValidSlot(pass_constants.raw_mask_uav_index)
        || !BX_IsValidSlot(pass_constants.base_mask_uav_index)
        || !BX_IsValidSlot(pass_constants.hierarchy_mask_uav_index)
        || !BX_IsValidSlot(pass_constants.count_buffer_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> raw_mask
        = ResourceDescriptorHeap[pass_constants.raw_mask_uav_index];
    RWStructuredBuffer<uint> base_mask
        = ResourceDescriptorHeap[pass_constants.base_mask_uav_index];
    RWStructuredBuffer<uint> hierarchy_mask
        = ResourceDescriptorHeap[pass_constants.hierarchy_mask_uav_index];
    RWStructuredBuffer<uint> count_buffer
        = ResourceDescriptorHeap[pass_constants.count_buffer_uav_index];

    const uint total_raw_entries
        = pass_constants.job_count * pass_constants.base_tiles_per_job;
    const uint total_base_entries
        = pass_constants.job_count * pass_constants.base_tiles_per_job;
    const uint total_hierarchy_entries
        = pass_constants.job_count * pass_constants.hierarchy_tiles_per_job;
    const uint total_count_entries = pass_constants.job_count * 2u;

    if (dispatch_thread_id.x < total_raw_entries) {
        raw_mask[dispatch_thread_id.x] = 0u;
    }
    if (dispatch_thread_id.x < total_base_entries) {
        base_mask[dispatch_thread_id.x] = 0u;
    }
    if (dispatch_thread_id.x < total_hierarchy_entries) {
        hierarchy_mask[dispatch_thread_id.x] = 0u;
    }
    if (dispatch_thread_id.x < total_count_entries) {
        count_buffer[dispatch_thread_id.x] = 0u;
    }
}

[shader("compute")]
[numthreads(CONVENTIONAL_SHADOW_RECEIVER_MASK_SCREEN_THREAD_GROUP_SIZE,
    CONVENTIONAL_SHADOW_RECEIVER_MASK_SCREEN_THREAD_GROUP_SIZE, 1)]
void CS_Analyze(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ConventionalShadowReceiverMaskPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.screen_dimensions.x
        || dispatch_thread_id.y >= pass_constants.screen_dimensions.y
        || !BX_IsValidSlot(pass_constants.depth_texture_index)
        || !BX_IsValidSlot(pass_constants.job_buffer_index)
        || !BX_IsValidSlot(pass_constants.raw_mask_uav_index)) {
        return;
    }

    Texture2D<float> depth_texture = ResourceDescriptorHeap[pass_constants.depth_texture_index];
    StructuredBuffer<ConventionalShadowReceiverAnalysisJob> jobs
        = ResourceDescriptorHeap[pass_constants.job_buffer_index];
    RWStructuredBuffer<uint> raw_mask
        = ResourceDescriptorHeap[pass_constants.raw_mask_uav_index];

    const float depth = depth_texture.Load(int3(dispatch_thread_id.xy, 0)).r;
    if (depth <= 0.0f) {
        return;
    }

    const float3 world_position_ws = ReconstructWorldPosition(dispatch_thread_id.xy,
        depth, pass_constants.screen_dimensions, pass_constants.inverse_view_projection);
    const float eye_depth = ComputeEyeDepth(world_position_ws, pass_constants.view_matrix);
    if (eye_depth <= 0.0f) {
        return;
    }

    [loop]
    for (uint job_index = 0u; job_index < pass_constants.job_count; ++job_index) {
        const ConventionalShadowReceiverAnalysisJob job = jobs[job_index];
        if ((job.flags & CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_VALID) == 0u) {
            continue;
        }

        const float split_begin = job.split_and_full_depth_range.x;
        const float split_end = job.split_and_full_depth_range.y;
        const bool in_split = eye_depth <= split_end
            && (job_index == 0u ? eye_depth >= split_begin : eye_depth > split_begin);
        if (!in_split) {
            continue;
        }

        const float3 light_space = mul(job.light_rotation_matrix, float4(world_position_ws, 1.0f)).xyz;
        const int2 tile_coord = ComputeFullRectTileCoord(
            light_space.xy, job.full_rect_center_half_extent, pass_constants.base_tile_resolution);
        const uint job_offset = job_index * pass_constants.base_tiles_per_job;
        const uint flat_index = job_offset
            + uint(tile_coord.y) * pass_constants.base_tile_resolution
            + uint(tile_coord.x);
        InterlockedOr(raw_mask[flat_index], 1u);
    }
}

[shader("compute")]
[numthreads(CONVENTIONAL_SHADOW_RECEIVER_MASK_LINEAR_THREAD_GROUP_SIZE, 1, 1)]
void CS_DilateMasks(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ConventionalShadowReceiverMaskPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const uint total_base_entries
        = pass_constants.job_count * pass_constants.base_tiles_per_job;
    if (dispatch_thread_id.x >= total_base_entries
        || !BX_IsValidSlot(pass_constants.job_buffer_index)
        || !BX_IsValidSlot(pass_constants.analysis_buffer_index)
        || !BX_IsValidSlot(pass_constants.raw_mask_srv_index)
        || !BX_IsValidSlot(pass_constants.base_mask_uav_index)
        || !BX_IsValidSlot(pass_constants.count_buffer_uav_index)) {
        return;
    }

    StructuredBuffer<ConventionalShadowReceiverAnalysisJob> jobs
        = ResourceDescriptorHeap[pass_constants.job_buffer_index];
    StructuredBuffer<ConventionalShadowReceiverAnalysis> analysis_buffer
        = ResourceDescriptorHeap[pass_constants.analysis_buffer_index];
    StructuredBuffer<uint> raw_mask
        = ResourceDescriptorHeap[pass_constants.raw_mask_srv_index];
    RWStructuredBuffer<uint> base_mask
        = ResourceDescriptorHeap[pass_constants.base_mask_uav_index];
    RWStructuredBuffer<uint> count_buffer
        = ResourceDescriptorHeap[pass_constants.count_buffer_uav_index];

    const uint job_index = dispatch_thread_id.x / pass_constants.base_tiles_per_job;
    const uint local_index = dispatch_thread_id.x % pass_constants.base_tiles_per_job;
    const uint tile_x = local_index % pass_constants.base_tile_resolution;
    const uint tile_y = local_index / pass_constants.base_tile_resolution;
    const uint job_offset = job_index * pass_constants.base_tiles_per_job;
    const ConventionalShadowReceiverAnalysisJob job = jobs[job_index];
    const ConventionalShadowReceiverAnalysis analysis = analysis_buffer[job_index];

    if ((job.flags & CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_VALID) == 0u
        || (analysis.flags & CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_VALID) == 0u
        || analysis.sample_count == 0u) {
        base_mask[dispatch_thread_id.x] = 0u;
        return;
    }

    const uint dilation_tile_radius = ComputeDilationTileRadius(
        job.full_rect_center_half_extent.zw,
        analysis.raw_depth_and_dilation.z,
        pass_constants.base_tile_resolution);
    const int radius = int(dilation_tile_radius);

    uint occupied = 0u;
    [loop]
    for (int y = int(tile_y) - radius; y <= int(tile_y) + radius; ++y) {
        if (y < 0 || y >= int(pass_constants.base_tile_resolution)) {
            continue;
        }
        [loop]
        for (int x = int(tile_x) - radius; x <= int(tile_x) + radius; ++x) {
            if (x < 0 || x >= int(pass_constants.base_tile_resolution)) {
                continue;
            }

            const uint raw_index = job_offset
                + uint(y) * pass_constants.base_tile_resolution
                + uint(x);
            if (raw_mask[raw_index] != 0u) {
                occupied = 1u;
                break;
            }
        }
        if (occupied != 0u) {
            break;
        }
    }

    base_mask[dispatch_thread_id.x] = occupied;
    if (occupied != 0u) {
        InterlockedAdd(count_buffer[CountBufferBaseIndex(job_index)], 1u);
    }
}

[shader("compute")]
[numthreads(CONVENTIONAL_SHADOW_RECEIVER_MASK_LINEAR_THREAD_GROUP_SIZE, 1, 1)]
void CS_BuildHierarchy(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ConventionalShadowReceiverMaskPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IsValidSlot(pass_constants.base_mask_srv_index)
        || !BX_IsValidSlot(pass_constants.hierarchy_mask_uav_index)
        || !BX_IsValidSlot(pass_constants.count_buffer_uav_index)) {
        return;
    }

    const uint total_hierarchy_entries
        = pass_constants.job_count * pass_constants.hierarchy_tiles_per_job;
    if (dispatch_thread_id.x >= total_hierarchy_entries) {
        return;
    }

    StructuredBuffer<uint> base_mask
        = ResourceDescriptorHeap[pass_constants.base_mask_srv_index];
    RWStructuredBuffer<uint> hierarchy_mask
        = ResourceDescriptorHeap[pass_constants.hierarchy_mask_uav_index];
    RWStructuredBuffer<uint> count_buffer
        = ResourceDescriptorHeap[pass_constants.count_buffer_uav_index];

    const uint job_index = dispatch_thread_id.x / pass_constants.hierarchy_tiles_per_job;
    const uint local_index = dispatch_thread_id.x % pass_constants.hierarchy_tiles_per_job;
    const uint hierarchy_x = local_index % pass_constants.hierarchy_tile_resolution;
    const uint hierarchy_y = local_index / pass_constants.hierarchy_tile_resolution;
    const uint base_origin_x = hierarchy_x * pass_constants.hierarchy_reduction;
    const uint base_origin_y = hierarchy_y * pass_constants.hierarchy_reduction;
    const uint base_job_offset = job_index * pass_constants.base_tiles_per_job;

    uint occupied = 0u;
    [loop]
    for (uint y = 0u; y < pass_constants.hierarchy_reduction; ++y) {
        const uint base_y = base_origin_y + y;
        if (base_y >= pass_constants.base_tile_resolution) {
            continue;
        }
        [loop]
        for (uint x = 0u; x < pass_constants.hierarchy_reduction; ++x) {
            const uint base_x = base_origin_x + x;
            if (base_x >= pass_constants.base_tile_resolution) {
                continue;
            }

            const uint base_index = base_job_offset
                + base_y * pass_constants.base_tile_resolution
                + base_x;
            if (base_mask[base_index] != 0u) {
                occupied = 1u;
                break;
            }
        }
        if (occupied != 0u) {
            break;
        }
    }

    hierarchy_mask[dispatch_thread_id.x] = occupied;
    if (occupied != 0u) {
        InterlockedAdd(count_buffer[CountBufferBaseIndex(job_index) + 1u], 1u);
    }
}

[shader("compute")]
[numthreads(CONVENTIONAL_SHADOW_RECEIVER_MASK_LINEAR_THREAD_GROUP_SIZE, 1, 1)]
void CS_Finalize(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ConventionalShadowReceiverMaskPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.job_count
        || !BX_IsValidSlot(pass_constants.job_buffer_index)
        || !BX_IsValidSlot(pass_constants.analysis_buffer_index)
        || !BX_IsValidSlot(pass_constants.count_buffer_srv_index)
        || !BX_IsValidSlot(pass_constants.summary_buffer_uav_index)) {
        return;
    }

    StructuredBuffer<ConventionalShadowReceiverAnalysisJob> jobs
        = ResourceDescriptorHeap[pass_constants.job_buffer_index];
    StructuredBuffer<ConventionalShadowReceiverAnalysis> analysis_buffer
        = ResourceDescriptorHeap[pass_constants.analysis_buffer_index];
    StructuredBuffer<uint> count_buffer
        = ResourceDescriptorHeap[pass_constants.count_buffer_srv_index];
    RWStructuredBuffer<ConventionalShadowReceiverMaskSummary> summary_buffer
        = ResourceDescriptorHeap[pass_constants.summary_buffer_uav_index];

    const uint job_index = dispatch_thread_id.x;
    const ConventionalShadowReceiverAnalysisJob job = jobs[job_index];
    const ConventionalShadowReceiverAnalysis analysis = analysis_buffer[job_index];
    const uint occupied_tile_count = count_buffer[CountBufferBaseIndex(job_index)];
    const uint hierarchy_occupied_tile_count
        = count_buffer[CountBufferBaseIndex(job_index) + 1u];

    uint flags = 0u;
    if ((analysis.flags & CONVENTIONAL_SHADOW_RECEIVER_ANALYSIS_FLAG_VALID) != 0u
        && analysis.sample_count > 0u) {
        flags |= CONVENTIONAL_SHADOW_RECEIVER_MASK_FLAG_VALID;
        flags |= CONVENTIONAL_SHADOW_RECEIVER_MASK_FLAG_HIERARCHY_BUILT;
    } else {
        flags |= CONVENTIONAL_SHADOW_RECEIVER_MASK_FLAG_EMPTY;
    }

    ConventionalShadowReceiverMaskSummary summary
        = (ConventionalShadowReceiverMaskSummary)0;
    summary.full_rect_center_half_extent = analysis.full_rect_center_half_extent;
    summary.raw_xy_min_max = analysis.raw_xy_min_max;
    summary.raw_depth_and_dilation = analysis.raw_depth_and_dilation;
    summary.target_array_slice = analysis.target_array_slice;
    summary.flags = flags;
    summary.sample_count = analysis.sample_count;
    summary.occupied_tile_count = occupied_tile_count;
    summary.hierarchy_occupied_tile_count = hierarchy_occupied_tile_count;
    summary.base_tile_resolution = pass_constants.base_tile_resolution;
    summary.hierarchy_tile_resolution = pass_constants.hierarchy_tile_resolution;
    summary.dilation_tile_radius = ComputeDilationTileRadius(
        job.full_rect_center_half_extent.zw,
        analysis.raw_depth_and_dilation.z,
        pass_constants.base_tile_resolution);
    summary.hierarchy_reduction = pass_constants.hierarchy_reduction;
    summary_buffer[job_index] = summary;
}

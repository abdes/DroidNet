//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/ConventionalShadowCasterCullingPartition.hlsli"
#include "Renderer/ConventionalShadowDrawRecord.hlsli"
#include "Renderer/ConventionalShadowIndirectDrawCommand.hlsli"
#include "Renderer/ConventionalShadowReceiverAnalysisJob.hlsli"
#include "Renderer/ConventionalShadowReceiverMask.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/ViewConstants.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_PartitionIndex;
    uint g_PassConstantsIndex;
}

static const uint CONVENTIONAL_SHADOW_CASTER_CULLING_THREAD_GROUP_SIZE = 64u;
static const float CONVENTIONAL_SHADOW_CASTER_CULLING_EPSILON = 1.0e-5f;

struct ConventionalShadowCasterCullingPassConstants
{
    uint draw_record_buffer_index;
    uint draw_metadata_index;
    uint job_buffer_index;
    uint receiver_mask_summary_index;
    uint receiver_mask_base_index;
    uint receiver_mask_hierarchy_index;
    uint partition_buffer_index;
    uint job_count;
    uint base_tile_resolution;
    uint hierarchy_tile_resolution;
    uint base_tiles_per_job;
    uint hierarchy_tiles_per_job;
    uint hierarchy_reduction;
    uint partition_count;
    uint _pad0;
    uint _pad1;
};

static bool ComputeSphereTileBounds(const float2 center_xy, const float radius,
    const float4 full_rect_center_half_extent, const uint base_tile_resolution,
    out uint2 tile_min, out uint2 tile_max)
{
    tile_min = uint2(0u, 0u);
    tile_max = uint2(0u, 0u);

    if (base_tile_resolution == 0u) {
        return false;
    }

    const float2 half_extent = max(
        full_rect_center_half_extent.zw,
        float2(CONVENTIONAL_SHADOW_CASTER_CULLING_EPSILON,
            CONVENTIONAL_SHADOW_CASTER_CULLING_EPSILON));
    const float2 full_min = full_rect_center_half_extent.xy - half_extent;
    const float2 full_max = full_rect_center_half_extent.xy + half_extent;
    const float2 sphere_min = center_xy - radius.xx;
    const float2 sphere_max = center_xy + radius.xx;

    if (sphere_max.x < full_min.x || sphere_min.x > full_max.x
        || sphere_max.y < full_min.y || sphere_min.y > full_max.y) {
        return false;
    }

    const float2 normalized_min = saturate(
        ((sphere_min - full_rect_center_half_extent.xy) / half_extent) * 0.5f
        + 0.5f);
    const float2 normalized_max = saturate(
        ((sphere_max - full_rect_center_half_extent.xy) / half_extent) * 0.5f
        + 0.5f);
    const float2 ordered_min = min(normalized_min, normalized_max);
    const float2 ordered_max = max(normalized_min, normalized_max);

    tile_min = min((uint2)floor(ordered_min * float(base_tile_resolution)),
        uint2(base_tile_resolution - 1u, base_tile_resolution - 1u));
    tile_max = min((uint2)floor(max(
                        ordered_max * float(base_tile_resolution) - 1.0e-5f,
                        0.0f)),
        uint2(base_tile_resolution - 1u, base_tile_resolution - 1u));
    return true;
}

static bool HierarchyMaskOverlaps(StructuredBuffer<uint> hierarchy_mask,
    const ConventionalShadowReceiverMaskSummary summary,
    const ConventionalShadowCasterCullingPassConstants pass_constants,
    const uint job_index, const uint2 base_tile_min, const uint2 base_tile_max)
{
    if (summary.hierarchy_occupied_tile_count == 0u
        || summary.hierarchy_tile_resolution == 0u
        || summary.hierarchy_reduction == 0u) {
        return false;
    }

    const uint2 hierarchy_min = base_tile_min / summary.hierarchy_reduction;
    const uint2 hierarchy_max = base_tile_max / summary.hierarchy_reduction;
    const uint hierarchy_job_offset = job_index * pass_constants.hierarchy_tiles_per_job;

    [loop]
    for (uint y = hierarchy_min.y; y <= hierarchy_max.y; ++y) {
        [loop]
        for (uint x = hierarchy_min.x; x <= hierarchy_max.x; ++x) {
            const uint hierarchy_index = hierarchy_job_offset
                + y * summary.hierarchy_tile_resolution + x;
            if (hierarchy_mask[hierarchy_index] != 0u) {
                return true;
            }
        }
    }

    return false;
}

static bool BaseMaskOverlaps(StructuredBuffer<uint> base_mask,
    const ConventionalShadowReceiverMaskSummary summary,
    const ConventionalShadowCasterCullingPassConstants pass_constants,
    const uint job_index, const uint2 base_tile_min, const uint2 base_tile_max)
{
    if (summary.occupied_tile_count == 0u || summary.base_tile_resolution == 0u) {
        return false;
    }

    const uint base_job_offset = job_index * pass_constants.base_tiles_per_job;
    [loop]
    for (uint y = base_tile_min.y; y <= base_tile_max.y; ++y) {
        [loop]
        for (uint x = base_tile_min.x; x <= base_tile_max.x; ++x) {
            const uint base_index
                = base_job_offset + y * summary.base_tile_resolution + x;
            if (base_mask[base_index] != 0u) {
                return true;
            }
        }
    }

    return false;
}

[shader("compute")]
[numthreads(CONVENTIONAL_SHADOW_CASTER_CULLING_THREAD_GROUP_SIZE, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ConventionalShadowCasterCullingPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (g_PartitionIndex >= pass_constants.partition_count
        || dispatch_thread_id.y >= pass_constants.job_count
        || !BX_IsValidSlot(pass_constants.draw_record_buffer_index)
        || !BX_IsValidSlot(pass_constants.draw_metadata_index)
        || !BX_IsValidSlot(pass_constants.job_buffer_index)
        || !BX_IsValidSlot(pass_constants.receiver_mask_summary_index)
        || !BX_IsValidSlot(pass_constants.receiver_mask_base_index)
        || !BX_IsValidSlot(pass_constants.receiver_mask_hierarchy_index)
        || !BX_IsValidSlot(pass_constants.partition_buffer_index)) {
        return;
    }

    StructuredBuffer<ConventionalShadowDrawRecord> draw_records
        = ResourceDescriptorHeap[pass_constants.draw_record_buffer_index];
    StructuredBuffer<DrawMetadata> draw_metadata
        = ResourceDescriptorHeap[pass_constants.draw_metadata_index];
    StructuredBuffer<ConventionalShadowReceiverAnalysisJob> jobs
        = ResourceDescriptorHeap[pass_constants.job_buffer_index];
    StructuredBuffer<ConventionalShadowReceiverMaskSummary> receiver_mask_summaries
        = ResourceDescriptorHeap[pass_constants.receiver_mask_summary_index];
    StructuredBuffer<uint> receiver_mask_base
        = ResourceDescriptorHeap[pass_constants.receiver_mask_base_index];
    StructuredBuffer<uint> receiver_mask_hierarchy
        = ResourceDescriptorHeap[pass_constants.receiver_mask_hierarchy_index];
    StructuredBuffer<ConventionalShadowCasterCullingPartition> partitions
        = ResourceDescriptorHeap[pass_constants.partition_buffer_index];

    const ConventionalShadowCasterCullingPartition partition
        = partitions[g_PartitionIndex];
    if (dispatch_thread_id.x >= partition.record_count
        || !BX_IsValidSlot(partition.command_uav_index)
        || !BX_IsValidSlot(partition.count_uav_index)
        || partition.max_commands_per_job == 0u) {
        return;
    }

    const uint record_index = partition.record_begin + dispatch_thread_id.x;
    const uint job_index = dispatch_thread_id.y;
    const ConventionalShadowDrawRecord draw_record = draw_records[record_index];
    const DrawMetadata metadata = draw_metadata[draw_record.draw_index];
    const ConventionalShadowReceiverAnalysisJob job = jobs[job_index];
    const ConventionalShadowReceiverMaskSummary summary
        = receiver_mask_summaries[job_index];

    const uint vertex_count_per_instance
        = metadata.is_indexed != 0u ? metadata.index_count : metadata.vertex_count;
    if (metadata.instance_count == 0u || vertex_count_per_instance == 0u) {
        return;
    }

    const float4 sphere_ws = draw_record.world_bounding_sphere;
    if (sphere_ws.w <= 0.0f) {
        return;
    }

    if ((summary.flags & CONVENTIONAL_SHADOW_RECEIVER_MASK_FLAG_VALID) == 0u
        || (summary.flags & CONVENTIONAL_SHADOW_RECEIVER_MASK_FLAG_HIERARCHY_BUILT)
            == 0u
        || summary.sample_count == 0u) {
        return;
    }

    const float3 sphere_center_ls
        = mul(job.light_rotation_matrix, float4(sphere_ws.xyz, 1.0f)).xyz;
    const float radius = sphere_ws.w;

    const float2 receiver_min
        = summary.raw_xy_min_max.xy - summary.raw_depth_and_dilation.zz;
    const float2 receiver_max
        = summary.raw_xy_min_max.zw + summary.raw_depth_and_dilation.zz;
    if (sphere_center_ls.x + radius < receiver_min.x
        || sphere_center_ls.x - radius > receiver_max.x
        || sphere_center_ls.y + radius < receiver_min.y
        || sphere_center_ls.y - radius > receiver_max.y) {
        return;
    }

    const float receiver_min_z
        = summary.raw_depth_and_dilation.x - summary.raw_depth_and_dilation.w;
    const float receiver_max_z
        = summary.raw_depth_and_dilation.y + summary.raw_depth_and_dilation.w;
    if (sphere_center_ls.z + radius < receiver_min_z
        || sphere_center_ls.z - radius > receiver_max_z) {
        return;
    }

    uint2 base_tile_min = uint2(0u, 0u);
    uint2 base_tile_max = uint2(0u, 0u);
    if (!ComputeSphereTileBounds(sphere_center_ls.xy, radius,
            summary.full_rect_center_half_extent, summary.base_tile_resolution,
            base_tile_min, base_tile_max)) {
        return;
    }

    if (!HierarchyMaskOverlaps(receiver_mask_hierarchy, summary, pass_constants,
            job_index, base_tile_min, base_tile_max)) {
        return;
    }
    if (!BaseMaskOverlaps(receiver_mask_base, summary, pass_constants, job_index,
            base_tile_min, base_tile_max)) {
        return;
    }

    RWStructuredBuffer<ConventionalShadowIndirectDrawCommand> indirect_commands
        = ResourceDescriptorHeap[partition.command_uav_index];
    RWStructuredBuffer<uint> command_counts
        = ResourceDescriptorHeap[partition.count_uav_index];

    uint command_slot = 0u;
    InterlockedAdd(command_counts[job_index], 1u, command_slot);
    if (command_slot >= partition.max_commands_per_job) {
        return;
    }

    const uint command_index
        = job_index * partition.max_commands_per_job + command_slot;
    ConventionalShadowIndirectDrawCommand command;
    command.draw_index = draw_record.draw_index;
    command.vertex_count_per_instance = vertex_count_per_instance;
    command.instance_count = metadata.instance_count;
    command.start_vertex_location = 0u;
    command.start_instance_location = 0u;
    indirect_commands[command_index] = command;
}

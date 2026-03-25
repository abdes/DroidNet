//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/Vsm/VsmIndirectDrawCommand.hlsli"
#include "Renderer/Vsm/VsmPhysicalPageMeta.hlsli"
#include "Renderer/Vsm/VsmRasterPageJob.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_RASTER_RESULT_PUBLISH_THREAD_GROUP_SIZE = 64u;
static const uint VSM_RASTER_PAGE_JOB_FLAG_STATIC_ONLY = 1u << 0u;
static const uint VSM_RENDERED_PAGE_DIRTY_DYNAMIC_RASTERIZED = 1u << 0u;
static const uint VSM_RENDERED_PAGE_DIRTY_STATIC_RASTERIZED = 1u << 1u;
static const uint VSM_RENDERED_PAGE_DIRTY_REVEAL_FORCED = 1u << 2u;

struct VsmRasterResultPublishPassConstants
{
    uint page_job_buffer_index;
    uint indirect_commands_srv_index;
    uint command_counts_srv_index;
    uint reveal_flags_index;
    uint dirty_flags_uav_index;
    uint physical_meta_uav_index;
    uint prepared_page_count;
    uint max_commands_per_page;
    uint current_frame_generation_low;
    uint current_frame_generation_high;
    uint _pad0;
    uint _pad1;
};

[shader("compute")]
[numthreads(VSM_RASTER_RESULT_PUBLISH_THREAD_GROUP_SIZE, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmRasterResultPublishPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.prepared_page_count
        || pass_constants.prepared_page_count == 0u
        || pass_constants.max_commands_per_page == 0u) {
        return;
    }

    if (!BX_IsValidSlot(pass_constants.page_job_buffer_index)
        || !BX_IsValidSlot(pass_constants.indirect_commands_srv_index)
        || !BX_IsValidSlot(pass_constants.command_counts_srv_index)
        || !BX_IsValidSlot(pass_constants.dirty_flags_uav_index)
        || !BX_IsValidSlot(pass_constants.physical_meta_uav_index)) {
        return;
    }

    StructuredBuffer<VsmRasterPageJob> page_jobs
        = ResourceDescriptorHeap[pass_constants.page_job_buffer_index];
    StructuredBuffer<VsmIndirectDrawCommand> indirect_commands
        = ResourceDescriptorHeap[pass_constants.indirect_commands_srv_index];
    StructuredBuffer<uint> command_counts
        = ResourceDescriptorHeap[pass_constants.command_counts_srv_index];
    RWStructuredBuffer<uint> dirty_flags
        = ResourceDescriptorHeap[pass_constants.dirty_flags_uav_index];
    RWStructuredBuffer<VsmPhysicalPageMeta> physical_meta
        = ResourceDescriptorHeap[pass_constants.physical_meta_uav_index];

    const uint page_index = dispatch_thread_id.x;
    const uint command_count = command_counts[page_index];
    if (command_count == 0u) {
        return;
    }

    const VsmRasterPageJob page_job = page_jobs[page_index];
    uint dirty_bits = (page_job.job_flags & VSM_RASTER_PAGE_JOB_FLAG_STATIC_ONLY) != 0u
        ? VSM_RENDERED_PAGE_DIRTY_STATIC_RASTERIZED
        : VSM_RENDERED_PAGE_DIRTY_DYNAMIC_RASTERIZED;

    const bool reveal_flags_available = BX_IsValidSlot(pass_constants.reveal_flags_index);
    if (reveal_flags_available) {
        StructuredBuffer<uint> reveal_flags
            = ResourceDescriptorHeap[pass_constants.reveal_flags_index];
        const uint bounded_command_count
            = min(command_count, pass_constants.max_commands_per_page);
        const uint command_base = page_index * pass_constants.max_commands_per_page;
        for (uint command_slot = 0u; command_slot < bounded_command_count; ++command_slot) {
            const VsmIndirectDrawCommand command
                = indirect_commands[command_base + command_slot];
            const uint reveal_flag = reveal_flags[command.draw_index];
            if (reveal_flag != 0u) {
                dirty_bits |= VSM_RENDERED_PAGE_DIRTY_REVEAL_FORCED;
                break;
            }
        }
    }

    uint ignored_previous = 0u;
    InterlockedOr(dirty_flags[page_job.physical_page_index], dirty_bits, ignored_previous);

    VsmPhysicalPageMeta meta = physical_meta[page_job.physical_page_index];
    meta.is_dirty = 1u;
    meta.used_this_frame = 1u;
    meta.view_uncached = 0u;
    if ((page_job.job_flags & VSM_RASTER_PAGE_JOB_FLAG_STATIC_ONLY) != 0u) {
        meta.static_invalidated = 0u;
    } else {
        meta.dynamic_invalidated = 0u;
    }
    meta.last_touched_frame
        = uint2(pass_constants.current_frame_generation_low,
            pass_constants.current_frame_generation_high);
    physical_meta[page_job.physical_page_index] = meta;
}

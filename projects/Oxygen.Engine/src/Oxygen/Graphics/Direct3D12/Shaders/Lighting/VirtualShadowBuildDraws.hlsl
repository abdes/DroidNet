//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Lighting/VirtualShadowPassCommon.hlsli"

[shader("compute")]
[numthreads(64, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VirtualShadowPassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IN_GLOBAL_SRV(pass_constants.schedule_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_lookup_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_count_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.draw_args_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.draw_page_ranges_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.draw_page_indices_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.draw_page_counter_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint4> schedule =
        ResourceDescriptorHeap[pass_constants.schedule_uav_index];
    RWStructuredBuffer<uint> schedule_lookup =
        ResourceDescriptorHeap[pass_constants.schedule_lookup_uav_index];
    RWStructuredBuffer<uint> schedule_count =
        ResourceDescriptorHeap[pass_constants.schedule_count_uav_index];
    RWStructuredBuffer<DrawIndirectCommand> draw_args =
        ResourceDescriptorHeap[pass_constants.draw_args_uav_index];
    RWStructuredBuffer<DrawPageRange> draw_page_ranges =
        ResourceDescriptorHeap[pass_constants.draw_page_ranges_uav_index];
    RWStructuredBuffer<uint> draw_page_indices =
        ResourceDescriptorHeap[pass_constants.draw_page_indices_uav_index];
    RWStructuredBuffer<uint> draw_page_counter =
        ResourceDescriptorHeap[pass_constants.draw_page_counter_uav_index];

    const uint thread_index = dispatch_thread_id.x;
    if (thread_index >= pass_constants.draw_count) {
        return;
    }

    const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();
    if (!BX_IN_GLOBAL_SRV(draw_bindings.draw_metadata_slot)) {
        draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
        draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
        return;
    }

    StructuredBuffer<DrawMetadata> draw_metadata =
        ResourceDescriptorHeap[draw_bindings.draw_metadata_slot];
    const DrawMetadata meta = draw_metadata[thread_index];
    const bool has_draw_bounds = BX_IN_GLOBAL_SRV(pass_constants.draw_bounds_srv_index);
    float4 draw_bound = float4(0.0, 0.0, 0.0, 0.0);
    if (has_draw_bounds) {
        StructuredBuffer<float4> draw_bounds =
            ResourceDescriptorHeap[pass_constants.draw_bounds_srv_index];
        draw_bound = draw_bounds[thread_index];
    }
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    const ShadowFrameBindings shadow_bindings =
        LoadShadowFrameBindings(view_bindings.shadow_frame_slot);
    if (!BX_IN_GLOBAL_SRV(shadow_bindings.virtual_directional_shadow_metadata_slot)) {
        draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
        draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
        return;
    }
    StructuredBuffer<DirectionalVirtualShadowMetadata> metadata_buffer =
        ResourceDescriptorHeap[shadow_bindings.virtual_directional_shadow_metadata_slot];
    const DirectionalVirtualShadowMetadata directional_metadata = metadata_buffer[0];
    const bool is_shadow_caster = (meta.flags & kPassMaskShadowCaster) != 0u;
    const bool is_shadow_surface =
        (meta.flags & (kPassMaskOpaque | kPassMaskMasked)) != 0u;
    const uint vertex_count = meta.is_indexed != 0u ? meta.index_count : meta.vertex_count;
    const uint scheduled_page_count = schedule_count[0];
    const bool invalid_draw = vertex_count == 0u || meta.instance_count == 0u;

    if (!is_shadow_caster || !is_shadow_surface || invalid_draw) {
        draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
        draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
        return;
    }

    uint overlapping_page_count = 0u;
    if (!has_draw_bounds || draw_bound.w <= 0.0f) {
        for (uint scheduled_index = 0u;
             scheduled_index < scheduled_page_count;
             ++scheduled_index) {
            if (ScheduledPageOverlapsBoundingSphere(
                    directional_metadata,
                    schedule[scheduled_index],
                    draw_bound)) {
                ++overlapping_page_count;
            }
        }
    } else {
        const float4 center_ls =
            mul(directional_metadata.light_view, float4(draw_bound.xyz, 1.0f));
        for (uint clip_index = 0u;
             clip_index < directional_metadata.clip_level_count;
             ++clip_index) {
            int min_page_x = 0;
            int max_page_x = -1;
            int min_page_y = 0;
            int max_page_y = -1;
            if (!DrawBoundingSphereOverlapsClip(
                    directional_metadata,
                    draw_bound,
                    clip_index,
                    center_ls,
                    min_page_x,
                    max_page_x,
                    min_page_y,
                    max_page_y)) {
                continue;
            }

            for (int page_y = min_page_y; page_y <= max_page_y; ++page_y) {
                for (int page_x = min_page_x; page_x <= max_page_x; ++page_x) {
                    const uint global_page_index =
                        clip_index * pass_constants.pages_per_level
                        + uint(page_y) * pass_constants.pages_per_axis
                        + uint(page_x);
                    const uint scheduled_index = schedule_lookup[global_page_index];
                    if (scheduled_index == 0xFFFFFFFFu
                        || scheduled_index >= scheduled_page_count) {
                        continue;
                    }
                    if (ScheduledPageOverlapsBoundingSphere(
                            directional_metadata,
                            schedule[scheduled_index],
                            draw_bound)) {
                        ++overlapping_page_count;
                    }
                }
            }
        }
    }

    if (overlapping_page_count == 0u) {
        draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
        draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
        return;
    }

    uint range_offset = 0u;
    InterlockedAdd(draw_page_counter[0], overlapping_page_count, range_offset);

    uint writable_count = 0u;
    if (range_offset < pass_constants.draw_page_list_capacity) {
        writable_count = min(
            overlapping_page_count,
            pass_constants.draw_page_list_capacity - range_offset);
    }

    uint local_write_index = 0u;
    if (!has_draw_bounds || draw_bound.w <= 0.0f) {
        for (uint scheduled_index = 0u;
             scheduled_index < scheduled_page_count && local_write_index < writable_count;
             ++scheduled_index) {
            if (ScheduledPageOverlapsBoundingSphere(
                    directional_metadata,
                    schedule[scheduled_index],
                    draw_bound)) {
                draw_page_indices[range_offset + local_write_index] = scheduled_index;
                ++local_write_index;
            }
        }
    } else {
        const float4 center_ls =
            mul(directional_metadata.light_view, float4(draw_bound.xyz, 1.0f));
        for (uint clip_index = 0u;
             clip_index < directional_metadata.clip_level_count
             && local_write_index < writable_count;
             ++clip_index) {
            int min_page_x = 0;
            int max_page_x = -1;
            int min_page_y = 0;
            int max_page_y = -1;
            if (!DrawBoundingSphereOverlapsClip(
                    directional_metadata,
                    draw_bound,
                    clip_index,
                    center_ls,
                    min_page_x,
                    max_page_x,
                    min_page_y,
                    max_page_y)) {
                continue;
            }

            for (int page_y = min_page_y;
                 page_y <= max_page_y && local_write_index < writable_count;
                 ++page_y) {
                for (int page_x = min_page_x;
                     page_x <= max_page_x && local_write_index < writable_count;
                     ++page_x) {
                    const uint global_page_index =
                        clip_index * pass_constants.pages_per_level
                        + uint(page_y) * pass_constants.pages_per_axis
                        + uint(page_x);
                    const uint scheduled_index = schedule_lookup[global_page_index];
                    if (scheduled_index == 0xFFFFFFFFu
                        || scheduled_index >= scheduled_page_count) {
                        continue;
                    }
                    if (ScheduledPageOverlapsBoundingSphere(
                            directional_metadata,
                            schedule[scheduled_index],
                            draw_bound)) {
                        draw_page_indices[range_offset + local_write_index] = scheduled_index;
                        ++local_write_index;
                    }
                }
            }
        }
    }

    const bool instance_overflow = DrawIndirectInstanceCountOverflows(
        meta.instance_count, writable_count);
    if (instance_overflow || writable_count == 0u) {
        draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
        draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
        return;
    }

    DrawPageRange range;
    range.offset = range_offset;
    range.count = writable_count;
    range._pad0 = 0u;
    range._pad1 = 0u;
    draw_page_ranges[thread_index] = range;
    draw_args[thread_index] = MakeDrawIndirectCommand(
        thread_index,
        vertex_count,
        meta.instance_count * writable_count);
}

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
    if (!BX_IN_GLOBAL_SRV(pass_constants.page_table_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.page_flags_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_metadata_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_lists_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_lists_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.resolve_stats_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_uav_index];
    RWStructuredBuffer<uint> page_flags =
        ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    RWStructuredBuffer<VirtualShadowPhysicalPageMetadata> physical_page_metadata_uav =
        ResourceDescriptorHeap[pass_constants.physical_page_metadata_uav_index];
    StructuredBuffer<VirtualShadowPhysicalPageListEntry> physical_page_lists =
        ResourceDescriptorHeap[pass_constants.physical_page_lists_srv_index];
    RWStructuredBuffer<VirtualShadowPhysicalPageListEntry> physical_page_lists_uav =
        ResourceDescriptorHeap[pass_constants.physical_page_lists_uav_index];
    RWStructuredBuffer<VirtualShadowResolveStats> resolve_stats =
        ResourceDescriptorHeap[pass_constants.resolve_stats_uav_index];

    const uint thread_index = dispatch_thread_id.x;
    if (thread_index >= pass_constants.total_page_count) {
        return;
    }

    const bool has_request_words =
        pass_constants.request_word_count > 0u
        && BX_IN_GLOBAL_SRV(pass_constants.request_words_srv_index);
    const bool has_page_mark_flags =
        BX_IN_GLOBAL_SRV(pass_constants.page_mark_flags_srv_index);
    bool requested_this_frame = false;
    if (has_request_words) {
        StructuredBuffer<uint> request_words =
            ResourceDescriptorHeap[pass_constants.request_words_srv_index];
        requested_this_frame = IsPageRequestedThisFrame(
            request_words, pass_constants.request_word_count, thread_index);
    }

    uint mark_flags = 0u;
    if (has_page_mark_flags) {
        StructuredBuffer<uint> page_mark_flags =
            ResourceDescriptorHeap[pass_constants.page_mark_flags_srv_index];
        mark_flags = page_mark_flags[thread_index];
        requested_this_frame = requested_this_frame
            || VirtualShadowPageHasFlag(
                mark_flags, OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME);
    }
    if (!requested_this_frame) {
        return;
    }

    const VirtualShadowPageTableEntry existing_entry =
        DecodeVirtualShadowPageTableEntry(page_table[thread_index]);
    if (VirtualShadowPageTableEntryHasCurrentLod(existing_entry)) {
        return;
    }

    const uint clip_index = thread_index / pass_constants.pages_per_level;
    if (clip_index >= pass_constants.clip_level_count) {
        return;
    }

    const uint local_page_index = thread_index % pass_constants.pages_per_level;
    const uint page_x = local_page_index % pass_constants.pages_per_axis;
    const uint page_y = local_page_index / pass_constants.pages_per_axis;
    const int grid_x =
        LoadPackedInt(pass_constants.clip_grid_origin_x_packed, clip_index)
        + int(page_x);
    const int grid_y =
        LoadPackedInt(pass_constants.clip_grid_origin_y_packed, clip_index)
        + int(page_y);

    const uint available_page_count = resolve_stats[0].available_page_list_count;
    if (available_page_count == 0u) {
        return;
    }

    uint allocation_ordinal = 0u;
    InterlockedAdd(
        resolve_stats[0].allocated_page_count,
        1u,
        allocation_ordinal);
    if (allocation_ordinal >= available_page_count) {
        return;
    }

    const uint available_list_start =
        PhysicalPageListStart(
            pass_constants.physical_page_capacity,
            kPhysicalPageListAvailable);
    const uint available_list_index =
        available_list_start + (available_page_count - 1u - allocation_ordinal);
    const VirtualShadowPhysicalPageListEntry available_entry =
        physical_page_lists[available_list_index];
    const uint physical_page_index = available_entry.physical_page_index;
    if (physical_page_index == 0xFFFFFFFFu
        || physical_page_index >= pass_constants.physical_page_capacity) {
        return;
    }

    const uint published_flags =
        OXYGEN_VSM_PAGE_FLAG_ALLOCATED
        | OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED
        | OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED
        | (mark_flags & (OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY
            | OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME));
    const uint packed_tile_coords =
        PackAtlasTileCoordsFromPhysicalPageIndex(
            pass_constants.atlas_tiles_per_axis,
            physical_page_index);

    physical_page_metadata_uav[physical_page_index].resident_key =
        PackVirtualResidentPageKey(clip_index, grid_x, grid_y);
    physical_page_metadata_uav[physical_page_index].page_flags = published_flags;
    physical_page_metadata_uav[physical_page_index].packed_atlas_tile_coords =
        packed_tile_coords;

    page_table[thread_index] = PackVirtualShadowPageTableEntry(
        DecodePackedTileX(packed_tile_coords),
        DecodePackedTileY(packed_tile_coords),
        0u,
        true,
        true,
        true);
    page_flags[thread_index] = published_flags;
    uint requested_list_index = 0u;
    InterlockedAdd(resolve_stats[0].requested_page_list_count, 1u, requested_list_index);
    if (requested_list_index < pass_constants.physical_page_capacity) {
        const uint requested_list_start =
            PhysicalPageListStart(
                pass_constants.physical_page_capacity,
                kPhysicalPageListRequested);
        physical_page_lists_uav[requested_list_start + requested_list_index] =
            MakePhysicalPageListEntry(
                PackVirtualResidentPageKey(clip_index, grid_x, grid_y),
                physical_page_index,
                published_flags);
    }
}

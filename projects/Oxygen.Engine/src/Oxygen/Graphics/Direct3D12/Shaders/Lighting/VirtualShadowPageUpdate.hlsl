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
        || !BX_IN_GLOBAL_SRV(pass_constants.dirty_page_flags_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_metadata_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_metadata_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_lists_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.resolve_stats_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_uav_index];
    RWStructuredBuffer<uint> page_flags =
        ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    RWStructuredBuffer<uint> dirty_page_flags =
        ResourceDescriptorHeap[pass_constants.dirty_page_flags_uav_index];
    StructuredBuffer<VirtualShadowPhysicalPageMetadata> physical_page_metadata =
        ResourceDescriptorHeap[pass_constants.physical_page_metadata_srv_index];
    RWStructuredBuffer<VirtualShadowPhysicalPageMetadata> physical_page_metadata_uav =
        ResourceDescriptorHeap[pass_constants.physical_page_metadata_uav_index];
    RWStructuredBuffer<VirtualShadowPhysicalPageListEntry> physical_page_lists_uav =
        ResourceDescriptorHeap[pass_constants.physical_page_lists_uav_index];
    RWStructuredBuffer<VirtualShadowResolveStats> resolve_stats =
        ResourceDescriptorHeap[pass_constants.resolve_stats_uav_index];
    const bool has_page_mark_flags =
        BX_IN_GLOBAL_SRV(pass_constants.page_mark_flags_srv_index);

    const uint thread_index = dispatch_thread_id.x;
    if (thread_index >= pass_constants.physical_page_capacity) {
        return;
    }

    const uint requested_list_start =
        PhysicalPageListStart(
            pass_constants.physical_page_capacity,
            kPhysicalPageListRequested);
    const uint dirty_list_start =
        PhysicalPageListStart(
            pass_constants.physical_page_capacity,
            kPhysicalPageListDirty);
    const uint clean_list_start =
        PhysicalPageListStart(
            pass_constants.physical_page_capacity,
            kPhysicalPageListClean);
    const uint available_list_start =
        PhysicalPageListStart(
            pass_constants.physical_page_capacity,
            kPhysicalPageListAvailable);

    VirtualShadowPhysicalPageMetadata metadata = physical_page_metadata[thread_index];
    const uint packed_tile_coords =
        PackAtlasTileCoordsFromPhysicalPageIndex(
            pass_constants.atlas_tiles_per_axis,
            thread_index);
    const bool allocated =
        VirtualShadowPageHasFlag(metadata.page_flags, OXYGEN_VSM_PAGE_FLAG_ALLOCATED);
    if (!allocated) {
        uint available_index = 0u;
        InterlockedAdd(resolve_stats[0].available_page_list_count, 1u, available_index);
        if (available_index < pass_constants.physical_page_capacity) {
            physical_page_lists_uav[available_list_start + available_index] =
                MakePhysicalPageListEntry(0ull, thread_index, 0u);
        }
        return;
    }

    const uint64_t resident_key = metadata.resident_key;
    const uint clip_index = DecodeVirtualResidentPageKeyClipLevel(resident_key);
    if (clip_index >= pass_constants.clip_level_count) {
        metadata.resident_key = 0ull;
        metadata.page_flags = 0u;
        metadata.packed_atlas_tile_coords = packed_tile_coords;
        physical_page_metadata_uav[thread_index] = metadata;
        uint available_index = 0u;
        InterlockedAdd(resolve_stats[0].available_page_list_count, 1u, available_index);
        if (available_index < pass_constants.physical_page_capacity) {
            physical_page_lists_uav[available_list_start + available_index] =
                MakePhysicalPageListEntry(0ull, thread_index, 0u);
        }
        return;
    }

    const int local_page_x = DecodeVirtualResidentPageKeyGridX(resident_key)
        - LoadPackedInt(pass_constants.clip_grid_origin_x_packed, clip_index);
    const int local_page_y = DecodeVirtualResidentPageKeyGridY(resident_key)
        - LoadPackedInt(pass_constants.clip_grid_origin_y_packed, clip_index);
    if (local_page_x < 0 || local_page_y < 0
        || local_page_x >= int(pass_constants.pages_per_axis)
        || local_page_y >= int(pass_constants.pages_per_axis)) {
        metadata.resident_key = 0ull;
        metadata.page_flags = 0u;
        metadata.packed_atlas_tile_coords = packed_tile_coords;
        physical_page_metadata_uav[thread_index] = metadata;
        uint available_index = 0u;
        InterlockedAdd(resolve_stats[0].available_page_list_count, 1u, available_index);
        if (available_index < pass_constants.physical_page_capacity) {
            physical_page_lists_uav[available_list_start + available_index] =
                MakePhysicalPageListEntry(0ull, thread_index, 0u);
        }
        return;
    }

    const uint local_page_index =
        uint(local_page_y) * pass_constants.pages_per_axis + uint(local_page_x);
    const uint global_page_index = clip_index * pass_constants.pages_per_level + local_page_index;
    if (global_page_index >= pass_constants.total_page_count) {
        return;
    }

    uint mark_flags = 0u;
    if (has_page_mark_flags) {
        StructuredBuffer<uint> page_mark_flags =
            ResourceDescriptorHeap[pass_constants.page_mark_flags_srv_index];
        mark_flags = page_mark_flags[global_page_index];
    }
    const bool dynamic_uncached =
        dirty_page_flags[thread_index + pass_constants.physical_page_capacity] != 0u;
    const bool static_uncached =
        dirty_page_flags[thread_index + 2u * pass_constants.physical_page_capacity] != 0u;
    const bool detail_geometry =
        VirtualShadowPageHasFlag(mark_flags, OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY);
    const bool used_this_frame =
        VirtualShadowPageHasFlag(mark_flags, OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME);
    const uint published_flags = OXYGEN_VSM_PAGE_FLAG_ALLOCATED
        | (dynamic_uncached ? OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED : 0u)
        | (static_uncached ? OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED : 0u)
        | (detail_geometry ? OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY : 0u)
        | (used_this_frame ? OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME : 0u);
    page_table[global_page_index] = PackVirtualShadowPageTableEntry(
        DecodePackedTileX(packed_tile_coords),
        DecodePackedTileY(packed_tile_coords),
        0u,
        true,
        true,
        used_this_frame);
    page_flags[global_page_index] = published_flags;
    metadata.page_flags = published_flags;
    metadata.packed_atlas_tile_coords = packed_tile_coords;
    physical_page_metadata_uav[thread_index] = metadata;

    uint list_index = 0u;
    uint list_start = clean_list_start;
    if (used_this_frame) {
        InterlockedAdd(resolve_stats[0].requested_page_list_count, 1u, list_index);
        list_start = requested_list_start;
    } else if (dynamic_uncached || static_uncached) {
        InterlockedAdd(resolve_stats[0].dirty_page_list_count, 1u, list_index);
        list_start = dirty_list_start;
    } else {
        InterlockedAdd(resolve_stats[0].clean_page_list_count, 1u, list_index);
        list_start = clean_list_start;
    }
    if (list_index < pass_constants.physical_page_capacity) {
        physical_page_lists_uav[list_start + list_index] =
            MakePhysicalPageListEntry(resident_key, thread_index, published_flags);
    }
}

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
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_metadata_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.resolve_stats_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_uav_index];
    RWStructuredBuffer<uint> page_flags =
        ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    RWStructuredBuffer<uint> dirty_page_flags =
        ResourceDescriptorHeap[pass_constants.dirty_page_flags_uav_index];
    RWStructuredBuffer<VirtualShadowPhysicalPageMetadata> physical_page_metadata_uav =
        ResourceDescriptorHeap[pass_constants.physical_page_metadata_uav_index];
    RWStructuredBuffer<VirtualShadowResolveStats> resolve_stats =
        ResourceDescriptorHeap[pass_constants.resolve_stats_uav_index];

    const uint thread_index = dispatch_thread_id.x;
    if (thread_index < pass_constants.total_page_count) {
        page_table[thread_index] = 0u;
        page_flags[thread_index] = 0u;
    }
    if (thread_index < pass_constants.physical_page_capacity * 3u) {
        dirty_page_flags[thread_index] = 0u;
    }
    if (pass_constants.reset_page_management_state != 0u
        && thread_index < pass_constants.physical_page_capacity) {
        const uint packed_tile_coords =
            PackAtlasTileCoordsFromPhysicalPageIndex(
                pass_constants.atlas_tiles_per_axis,
                thread_index);
        physical_page_metadata_uav[thread_index].resident_key = 0ull;
        physical_page_metadata_uav[thread_index].page_flags = 0u;
        physical_page_metadata_uav[thread_index].packed_atlas_tile_coords =
            packed_tile_coords;
    }
    if (thread_index == 0u) {
        resolve_stats[0].scheduled_raster_page_count = 0u;
        resolve_stats[0].allocated_page_count = 0u;
        resolve_stats[0].requested_page_count = 0u;
        resolve_stats[0].resident_dirty_page_count = 0u;
        resolve_stats[0].resident_clean_page_count = 0u;
        resolve_stats[0].pages_requiring_schedule_count = 0u;
        resolve_stats[0].available_page_list_count = 0u;
        resolve_stats[0].rasterized_page_count = 0u;
        resolve_stats[0].cached_page_transition_count = 0u;
        resolve_stats[0]._pad0 = 0u;
        resolve_stats[0]._pad1 = 0u;
        resolve_stats[0]._pad2 = 0u;
    }
}

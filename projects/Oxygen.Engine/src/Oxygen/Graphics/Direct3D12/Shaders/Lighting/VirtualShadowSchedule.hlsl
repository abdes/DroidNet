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
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_lookup_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_count_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.resolve_stats_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_uav_index];
    RWStructuredBuffer<uint> page_flags =
        ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    RWStructuredBuffer<uint4> schedule =
        ResourceDescriptorHeap[pass_constants.schedule_uav_index];
    RWStructuredBuffer<uint> schedule_lookup =
        ResourceDescriptorHeap[pass_constants.schedule_lookup_uav_index];
    RWStructuredBuffer<uint> schedule_count =
        ResourceDescriptorHeap[pass_constants.schedule_count_uav_index];
    RWStructuredBuffer<VirtualShadowResolveStats> resolve_stats =
        ResourceDescriptorHeap[pass_constants.resolve_stats_uav_index];

    const uint thread_index = dispatch_thread_id.x;
    if (thread_index >= pass_constants.total_page_count) {
        return;
    }

    schedule_lookup[thread_index] = 0xFFFFFFFFu;

    const uint packed_entry = page_table[thread_index];
    const uint published_flags = page_flags[thread_index];
    const uint uncached_flags =
        OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED | OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED;
    if ((packed_entry & OXYGEN_VSM_PAGE_TABLE_CURRENT_LOD_VALID_BIT) != 0u
        && (published_flags & uncached_flags) != 0u) {
        uint output_index = 0u;
        InterlockedAdd(resolve_stats[0].scheduled_raster_page_count, 1u, output_index);
        InterlockedAdd(schedule_count[0], 1u, output_index);
        if (output_index < pass_constants.schedule_capacity) {
            const uint tile_x = packed_entry & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK;
            const uint tile_y =
                (packed_entry >> OXYGEN_VSM_PAGE_TABLE_TILE_Y_SHIFT)
                & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK;
            schedule[output_index] = uint4(thread_index, packed_entry, tile_x, tile_y);
            schedule_lookup[thread_index] = output_index;
        }
    }
}

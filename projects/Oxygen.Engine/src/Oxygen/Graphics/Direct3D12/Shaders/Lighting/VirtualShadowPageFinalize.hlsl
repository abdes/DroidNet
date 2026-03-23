//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Lighting/VirtualShadowPassCommon.hlsli"

struct VirtualShadowPageFinalizePassConstants
{
    uint schedule_srv_index;
    uint schedule_count_srv_index;
    uint page_flags_uav_index;
    uint physical_page_metadata_uav_index;
    uint resolve_stats_uav_index;
    uint atlas_tiles_per_axis;
    uint schedule_capacity;
    uint _pad0;
};

[shader("compute")]
[numthreads(64, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VirtualShadowPageFinalizePassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IN_GLOBAL_SRV(pass_constants.schedule_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_count_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.page_flags_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_metadata_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.resolve_stats_uav_index)
        || pass_constants.atlas_tiles_per_axis == 0u) {
        return;
    }

    StructuredBuffer<uint4> schedule =
        ResourceDescriptorHeap[pass_constants.schedule_srv_index];
    StructuredBuffer<uint> schedule_count =
        ResourceDescriptorHeap[pass_constants.schedule_count_srv_index];
    RWStructuredBuffer<uint> page_flags =
        ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    RWStructuredBuffer<VirtualShadowPhysicalPageMetadata> physical_page_metadata_uav =
        ResourceDescriptorHeap[pass_constants.physical_page_metadata_uav_index];
    RWStructuredBuffer<VirtualShadowResolveStats> resolve_stats =
        ResourceDescriptorHeap[pass_constants.resolve_stats_uav_index];

    const uint scheduled_page_count =
        min(schedule_count[0], pass_constants.schedule_capacity);
    const uint thread_index = dispatch_thread_id.x;
    if (thread_index >= scheduled_page_count) {
        return;
    }

    const uint4 scheduled_page = schedule[thread_index];
    const uint virtual_page_index = scheduled_page.x;
    const uint tile_x = scheduled_page.z;
    const uint tile_y = scheduled_page.w;
    const uint physical_page_index = tile_y * pass_constants.atlas_tiles_per_axis + tile_x;
    const uint uncached_mask =
        OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED | OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED;

    const uint previous_page_flags = page_flags[virtual_page_index];
    page_flags[virtual_page_index] = previous_page_flags & ~uncached_mask;

    VirtualShadowPhysicalPageMetadata metadata = physical_page_metadata_uav[physical_page_index];
    const bool transitioned_to_cached = (metadata.page_flags & uncached_mask) != 0u;
    metadata.page_flags &= ~uncached_mask;
    physical_page_metadata_uav[physical_page_index] = metadata;

    uint ignored = 0u;
    InterlockedAdd(resolve_stats[0].rasterized_page_count, 1u, ignored);
    if (transitioned_to_cached) {
        InterlockedAdd(resolve_stats[0].cached_page_transition_count, 1u, ignored);
    }
}

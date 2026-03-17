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
        || !BX_IN_GLOBAL_SRV(pass_constants.page_flags_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_uav_index];
    RWStructuredBuffer<uint> page_flags =
        ResourceDescriptorHeap[pass_constants.page_flags_uav_index];

    const uint thread_index = dispatch_thread_id.x;
    if (thread_index >= pass_constants.pages_per_level
        || pass_constants.pages_per_level == 0u
        || pass_constants.pages_per_axis == 0u) {
        return;
    }

    const uint fine_clip = pass_constants.target_clip_index;
    if (fine_clip + 1u >= pass_constants.clip_level_count) {
        return;
    }

    const uint fine_page_x = thread_index % pass_constants.pages_per_axis;
    const uint fine_page_y = thread_index / pass_constants.pages_per_axis;
    const uint fine_global_page_index =
        fine_clip * pass_constants.pages_per_level + thread_index;
    if (fine_global_page_index >= pass_constants.total_page_count) {
        return;
    }

    const uint fine_flags = page_flags[fine_global_page_index];
    if (fine_flags == 0u) {
        return;
    }

    const uint parent_clip = fine_clip + 1u;
    const float fine_page_world =
        LoadPackedFloat(pass_constants.clip_page_world_packed, fine_clip);
    const float fine_origin_x =
        LoadPackedFloat(pass_constants.clip_origin_x_packed, fine_clip);
    const float fine_origin_y =
        LoadPackedFloat(pass_constants.clip_origin_y_packed, fine_clip);
    const float parent_page_world =
        LoadPackedFloat(pass_constants.clip_page_world_packed, parent_clip);
    const float parent_origin_x =
        LoadPackedFloat(pass_constants.clip_origin_x_packed, parent_clip);
    const float parent_origin_y =
        LoadPackedFloat(pass_constants.clip_origin_y_packed, parent_clip);

    const float page_center_x =
        fine_origin_x + (float(fine_page_x) + 0.5f) * fine_page_world;
    const float page_center_y =
        fine_origin_y + (float(fine_page_y) + 0.5f) * fine_page_world;
    const float parent_page_x_f = (page_center_x - parent_origin_x) / parent_page_world;
    const float parent_page_y_f = (page_center_y - parent_origin_y) / parent_page_world;
    if (parent_page_x_f < 0.0f || parent_page_y_f < 0.0f
        || parent_page_x_f >= float(pass_constants.pages_per_axis)
        || parent_page_y_f >= float(pass_constants.pages_per_axis)) {
        return;
    }

    const uint parent_page_x = uint(floor(parent_page_x_f));
    const uint parent_page_y = uint(floor(parent_page_y_f));
    const uint parent_global_page_index =
        parent_clip * pass_constants.pages_per_level
        + parent_page_y * pass_constants.pages_per_axis
        + parent_page_x;
    if (parent_global_page_index >= pass_constants.total_page_count) {
        return;
    }

    const VirtualShadowPageTableEntry parent_entry =
        DecodeVirtualShadowPageTableEntry(page_table[parent_global_page_index]);
    if (!VirtualShadowPageTableEntryHasCurrentLod(parent_entry)) {
        return;
    }

    InterlockedOr(
        page_flags[parent_global_page_index],
        MakeVirtualShadowHierarchyFlags(fine_flags));
}

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
    if (!BX_IN_GLOBAL_SRV(pass_constants.page_table_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_uav_index];

    const uint thread_index = dispatch_thread_id.x;
    if (thread_index >= pass_constants.pages_per_level
        || pass_constants.pages_per_level == 0u
        || pass_constants.pages_per_axis == 0u) {
        return;
    }

    const uint clip_index = pass_constants.target_clip_index;
    if (clip_index + 1u >= pass_constants.clip_level_count) {
        return;
    }

    const uint global_page_index = clip_index * pass_constants.pages_per_level + thread_index;
    if (global_page_index >= pass_constants.total_page_count) {
        return;
    }

    const uint current_packed_entry = page_table[global_page_index];
    if (VirtualShadowPageTableEntryHasCurrentLod(
            DecodeVirtualShadowPageTableEntry(current_packed_entry))) {
        return;
    }

    const uint page_x = thread_index % pass_constants.pages_per_axis;
    const uint page_y = thread_index / pass_constants.pages_per_axis;
    const float clip_page_world =
        LoadPackedFloat(pass_constants.clip_page_world_packed, clip_index);
    const float clip_origin_x =
        LoadPackedFloat(pass_constants.clip_origin_x_packed, clip_index);
    const float clip_origin_y =
        LoadPackedFloat(pass_constants.clip_origin_y_packed, clip_index);
    const float page_center_x =
        clip_origin_x + (float(page_x) + 0.5f) * clip_page_world;
    const float page_center_y =
        clip_origin_y + (float(page_y) + 0.5f) * clip_page_world;

    for (uint candidate_clip = clip_index + 1u;
         candidate_clip < pass_constants.clip_level_count;
         ++candidate_clip) {
        const float candidate_page_world =
            LoadPackedFloat(pass_constants.clip_page_world_packed, candidate_clip);
        const float candidate_page_x_f =
            (page_center_x
                - LoadPackedFloat(pass_constants.clip_origin_x_packed, candidate_clip))
            / candidate_page_world;
        const float candidate_page_y_f =
            (page_center_y
                - LoadPackedFloat(pass_constants.clip_origin_y_packed, candidate_clip))
            / candidate_page_world;
        if (candidate_page_x_f < 0.0f || candidate_page_y_f < 0.0f
            || candidate_page_x_f >= float(pass_constants.pages_per_axis)
            || candidate_page_y_f >= float(pass_constants.pages_per_axis)) {
            continue;
        }

        const uint candidate_page_x = min(
            pass_constants.pages_per_axis - 1u,
            uint(max(0.0f, floor(candidate_page_x_f))));
        const uint candidate_page_y = min(
            pass_constants.pages_per_axis - 1u,
            uint(max(0.0f, floor(candidate_page_y_f))));
        const uint candidate_global_page_index =
            candidate_clip * pass_constants.pages_per_level
            + candidate_page_y * pass_constants.pages_per_axis
            + candidate_page_x;
        if (candidate_global_page_index >= pass_constants.total_page_count) {
            continue;
        }

        const uint candidate_packed_entry = page_table[candidate_global_page_index];
        const VirtualShadowPageTableEntry candidate_entry =
            DecodeVirtualShadowPageTableEntry(candidate_packed_entry);
        if (!VirtualShadowPageTableEntryHasAnyLod(candidate_entry)) {
            continue;
        }

        const uint resolved_fallback_clip = ResolveVirtualShadowFallbackClipIndex(
            candidate_clip, pass_constants.clip_level_count, candidate_entry);
        if (resolved_fallback_clip <= clip_index
            || resolved_fallback_clip >= pass_constants.clip_level_count) {
            continue;
        }
        if (!VirtualShadowPageTableEntryHasCurrentLod(candidate_entry)) {
            continue;
        }

        page_table[global_page_index] = PackVirtualShadowPageTableEntry(
            candidate_entry.tile_x,
            candidate_entry.tile_y,
            resolved_fallback_clip - clip_index,
            false,
            true,
            false);
        return;
    }
}

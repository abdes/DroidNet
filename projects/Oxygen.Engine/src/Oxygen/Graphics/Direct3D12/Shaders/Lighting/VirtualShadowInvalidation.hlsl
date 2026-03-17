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
    if (!BX_IN_GLOBAL_SRV(pass_constants.dirty_page_flags_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_metadata_srv_index)) {
        return;
    }

    RWStructuredBuffer<uint> dirty_page_flags =
        ResourceDescriptorHeap[pass_constants.dirty_page_flags_uav_index];
    StructuredBuffer<VirtualShadowPhysicalPageMetadata> physical_page_metadata =
        ResourceDescriptorHeap[pass_constants.physical_page_metadata_srv_index];

    const uint thread_index = dispatch_thread_id.x;
    if (thread_index >= pass_constants.physical_page_capacity) {
        return;
    }

    if (pass_constants.global_dirty_resident_contents != 0u) {
        dirty_page_flags[thread_index] = 1u;
        dirty_page_flags[thread_index + pass_constants.physical_page_capacity] = 1u;
        dirty_page_flags[thread_index + 2u * pass_constants.physical_page_capacity] = 1u;
        return;
    }

    const bool has_shadow_caster_bounds =
        pass_constants.shadow_caster_bound_count > 0u
        && BX_IN_GLOBAL_SRV(pass_constants.previous_shadow_caster_bounds_srv_index)
        && BX_IN_GLOBAL_SRV(pass_constants.current_shadow_caster_bounds_srv_index);
    if (!has_shadow_caster_bounds) {
        return;
    }

    const VirtualShadowPhysicalPageMetadata metadata =
        physical_page_metadata[thread_index];
    if (!VirtualShadowPageHasFlag(
            metadata.page_flags, OXYGEN_VSM_PAGE_FLAG_ALLOCATED)) {
        return;
    }

    const uint64_t resident_key = metadata.resident_key;
    const uint clip_index = DecodeVirtualResidentPageKeyClipLevel(resident_key);
    const int grid_x = DecodeVirtualResidentPageKeyGridX(resident_key);
    const int grid_y = DecodeVirtualResidentPageKeyGridY(resident_key);
    StructuredBuffer<float4> previous_shadow_caster_bounds =
        ResourceDescriptorHeap[pass_constants.previous_shadow_caster_bounds_srv_index];
    StructuredBuffer<float4> current_shadow_caster_bounds =
        ResourceDescriptorHeap[pass_constants.current_shadow_caster_bounds_srv_index];

    for (uint bound_index = 0u;
         bound_index < pass_constants.shadow_caster_bound_count;
         ++bound_index) {
        const float4 previous_bound = previous_shadow_caster_bounds[bound_index];
        const float4 current_bound = current_shadow_caster_bounds[bound_index];
        if (all(previous_bound == current_bound)) {
            continue;
        }

        const bool overlaps_previous =
            ShadowCasterBoundOverlapsResidentPage(
                pass_constants,
                previous_bound,
                pass_constants.previous_light_view_matrix,
                clip_index,
                grid_x,
                grid_y);
        const bool overlaps_current =
            ShadowCasterBoundOverlapsResidentPage(
                pass_constants,
                current_bound,
                pass_constants.current_light_view_matrix,
                clip_index,
                grid_x,
                grid_y);
        if (!overlaps_previous && !overlaps_current) {
            continue;
        }

        dirty_page_flags[thread_index] = 1u;
        dirty_page_flags[thread_index + pass_constants.physical_page_capacity] = 1u;
        dirty_page_flags[thread_index + 2u * pass_constants.physical_page_capacity] = 1u;
        break;
    }
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Stage 7 available-page packing.
//
// After stage 6 has republished all reusable mappings, every physical page that
// still reports `is_allocated == 0` is available for fresh current-frame
// allocation. This pass compacts those page indices into a contiguous stack.
//
// The stack is written in ascending physical-page order on purpose so the GPU
// allocation path consumes the same deterministic page order chosen by the CPU
// planner.

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/Vsm/VsmPhysicalPageMeta.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VsmPackAvailablePagesPassConstants
{
    uint metadata_uav_index;
    uint available_pages_uav_index;
    uint available_page_count_uav_index;
    uint physical_page_count;
};

[shader("compute")]
[numthreads(1, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmPackAvailablePagesPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x != 0u || dispatch_thread_id.y != 0u || dispatch_thread_id.z != 0u) {
        return;
    }

    if (!BX_IsValidSlot(pass_constants.metadata_uav_index)
        || !BX_IsValidSlot(pass_constants.available_pages_uav_index)
        || !BX_IsValidSlot(pass_constants.available_page_count_uav_index)) {
        return;
    }

    RWStructuredBuffer<VsmPhysicalPageMeta> physical_meta
        = ResourceDescriptorHeap[pass_constants.metadata_uav_index];
    RWStructuredBuffer<uint> available_pages
        = ResourceDescriptorHeap[pass_constants.available_pages_uav_index];
    RWStructuredBuffer<uint> available_page_count
        = ResourceDescriptorHeap[pass_constants.available_page_count_uav_index];

    uint compact_index = 0u;
    for (uint physical_page_index = 0u;
         physical_page_index < pass_constants.physical_page_count;
         ++physical_page_index) {
        if (physical_meta[physical_page_index].is_allocated != 0u) {
            continue;
        }

        available_pages[compact_index] = physical_page_index;
        ++compact_index;
    }

    available_page_count[0] = compact_index;
}

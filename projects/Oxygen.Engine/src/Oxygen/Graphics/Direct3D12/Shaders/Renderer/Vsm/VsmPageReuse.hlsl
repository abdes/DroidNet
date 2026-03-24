//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Stage 6 physical-page reuse.
//
// The current-frame page table is rebuilt from scratch every frame. This pass
// therefore does not mutate the previous page table in place; it applies only
// the CPU-authoritative reuse records into the current-frame working set.

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/Vsm/VsmPageManagementDecisions.hlsli"
#include "Renderer/Vsm/VsmPageTable.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_PAGE_REUSE_THREAD_GROUP_SIZE = 64u;

struct VsmPageReusePassConstants
{
    uint reuse_decision_buffer_index;
    uint page_table_uav_index;
    uint page_flags_uav_index;
    uint metadata_uav_index;
    uint reuse_decision_count;
    uint virtual_page_count;
    uint physical_page_count;
    uint _pad0;
};

[shader("compute")]
[numthreads(VSM_PAGE_REUSE_THREAD_GROUP_SIZE, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmPageReusePassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const uint decision_index = dispatch_thread_id.x;
    if (decision_index >= pass_constants.reuse_decision_count) {
        return;
    }

    if (!BX_IsValidSlot(pass_constants.reuse_decision_buffer_index)
        || !BX_IsValidSlot(pass_constants.page_table_uav_index)
        || !BX_IsValidSlot(pass_constants.page_flags_uav_index)
        || !BX_IsValidSlot(pass_constants.metadata_uav_index)) {
        return;
    }

    StructuredBuffer<VsmShaderPageReuseDecision> reuse_decisions
        = ResourceDescriptorHeap[pass_constants.reuse_decision_buffer_index];
    RWStructuredBuffer<VsmShaderPageTableEntry> page_table
        = ResourceDescriptorHeap[pass_constants.page_table_uav_index];
    RWStructuredBuffer<VsmShaderPageFlags> page_flags
        = ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    RWStructuredBuffer<VsmPhysicalPageMeta> physical_meta
        = ResourceDescriptorHeap[pass_constants.metadata_uav_index];

    const VsmShaderPageReuseDecision decision = reuse_decisions[decision_index];
    if (decision.page_table_index >= pass_constants.virtual_page_count
        || decision.physical_page_index >= pass_constants.physical_page_count) {
        return;
    }

    page_table[decision.page_table_index]
        = VsmMakeMappedPageTableEntry(decision.physical_page_index);
    page_flags[decision.page_table_index] = decision.page_flags;
    physical_meta[decision.physical_page_index] = decision.physical_meta;
}

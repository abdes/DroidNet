//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Stage 8 fresh page allocation.
//
// The CPU planner remains authoritative for fresh allocation targets. Stage 7
// still produces the compact GPU-side available-page stack for diagnostics and
// validation, but stage 8 applies the planned physical-page mapping directly.

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/Draw/DrawMetadata.hlsli"
#include "Vortex/Contracts/Draw/MaterialShadingConstants.hlsli"
#include "Vortex/Services/Shadows/Vsm/VsmPageManagementDecisions.hlsli"
#include "Vortex/Services/Shadows/Vsm/VsmPageTable.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_ALLOCATE_NEW_PAGES_THREAD_GROUP_SIZE = 64u;

struct VsmAllocateNewPagesPassConstants
{
    uint allocation_decision_buffer_index;
    uint available_pages_srv_index;
    uint available_page_count_srv_index;
    uint metadata_seed_srv_index;
    uint page_table_uav_index;
    uint page_flags_uav_index;
    uint metadata_uav_index;
    uint allocation_decision_count;
    uint virtual_page_count;
    uint physical_page_count;
    uint _pad1;
    uint _pad2;
};

[shader("compute")]
[numthreads(VSM_ALLOCATE_NEW_PAGES_THREAD_GROUP_SIZE, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmAllocateNewPagesPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const uint decision_index = dispatch_thread_id.x;
    if (decision_index >= pass_constants.allocation_decision_count) {
        return;
    }

    if (!BX_IsValidSlot(pass_constants.allocation_decision_buffer_index)
        || !BX_IsValidSlot(pass_constants.available_pages_srv_index)
        || !BX_IsValidSlot(pass_constants.available_page_count_srv_index)
        || !BX_IsValidSlot(pass_constants.page_table_uav_index)
        || !BX_IsValidSlot(pass_constants.page_flags_uav_index)
        || !BX_IsValidSlot(pass_constants.metadata_uav_index)) {
        return;
    }

    StructuredBuffer<VsmShaderPageAllocationDecision> allocation_decisions
        = ResourceDescriptorHeap[pass_constants.allocation_decision_buffer_index];
    StructuredBuffer<uint> available_pages
        = ResourceDescriptorHeap[pass_constants.available_pages_srv_index];
    StructuredBuffer<uint> available_page_count
        = ResourceDescriptorHeap[pass_constants.available_page_count_srv_index];
    RWStructuredBuffer<VsmShaderPageTableEntry> page_table
        = ResourceDescriptorHeap[pass_constants.page_table_uav_index];
    RWStructuredBuffer<VsmShaderPageFlags> page_flags
        = ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    RWStructuredBuffer<VsmPhysicalPageMeta> physical_meta
        = ResourceDescriptorHeap[pass_constants.metadata_uav_index];

    const VsmShaderPageAllocationDecision decision
        = allocation_decisions[decision_index];
    if (decision.page_table_index >= pass_constants.virtual_page_count
        || decision.physical_page_index >= pass_constants.physical_page_count) {
        return;
    }

    const uint physical_page_index = decision.physical_page_index;

    VsmPhysicalPageMeta merged_meta = decision.physical_meta;
    if (BX_IsValidSlot(pass_constants.metadata_seed_srv_index)) {
        StructuredBuffer<VsmPhysicalPageMeta> metadata_seed
            = ResourceDescriptorHeap[pass_constants.metadata_seed_srv_index];
        merged_meta = VsmMergeSeedInvalidationBits(
            merged_meta, metadata_seed[physical_page_index]);
    }

    page_table[decision.page_table_index]
        = VsmMakeMappedPageTableEntry(physical_page_index);
    page_flags[decision.page_table_index] = decision.page_flags;
    physical_meta[physical_page_index] = merged_meta;
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Stage 10 mapped-mip propagation.
//
// Coarser levels need to know whether any finer descendant page is mapped even
// when the coarser level itself is not directly allocated. This pass records
// that as VSM_PAGE_FLAG_MAPPED_DESCENDANT.

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/Vsm/VsmPageFlags.hlsli"
#include "Renderer/Vsm/VsmPageHierarchyDispatch.hlsli"
#include "Renderer/Vsm/VsmPageTable.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_PROPAGATE_MAPPED_MIPS_GROUP_SIZE_X = 8u;
static const uint VSM_PROPAGATE_MAPPED_MIPS_GROUP_SIZE_Y = 8u;

struct VsmPropagateMappedMipsPassConstants
{
    uint dispatch_buffer_index;
    uint page_flags_uav_index;
    uint page_table_srv_index;
    uint _pad0;
};

[shader("compute")]
[numthreads(VSM_PROPAGATE_MAPPED_MIPS_GROUP_SIZE_X, VSM_PROPAGATE_MAPPED_MIPS_GROUP_SIZE_Y, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmPropagateMappedMipsPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IsValidSlot(pass_constants.dispatch_buffer_index)
        || !BX_IsValidSlot(pass_constants.page_flags_uav_index)
        || !BX_IsValidSlot(pass_constants.page_table_srv_index)) {
        return;
    }

    StructuredBuffer<VsmShaderPageHierarchyDispatch> dispatches
        = ResourceDescriptorHeap[pass_constants.dispatch_buffer_index];
    RWStructuredBuffer<VsmShaderPageFlags> page_flags
        = ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    StructuredBuffer<VsmShaderPageTableEntry> page_table
        = ResourceDescriptorHeap[pass_constants.page_table_srv_index];

    const VsmShaderPageHierarchyDispatch dispatch_item = dispatches[g_DrawIndex];
    if (dispatch_thread_id.x >= dispatch_item.source_pages_x
        || dispatch_thread_id.y >= dispatch_item.source_pages_y) {
        return;
    }

    const uint child_x = dispatch_thread_id.x;
    const uint child_y = dispatch_thread_id.y;
    const uint parent_x = child_x >> 1u;
    const uint parent_y = child_y >> 1u;
    const uint child_index = dispatch_item.first_page_table_entry
        + dispatch_item.source_level * dispatch_item.pages_per_level
        + child_y * dispatch_item.level0_pages_x + child_x;
    const uint parent_index = dispatch_item.first_page_table_entry
        + dispatch_item.target_level * dispatch_item.pages_per_level
        + parent_y * dispatch_item.level0_pages_x + parent_x;

    const bool child_mapped = VsmIsPageTableEntryMapped(page_table[child_index])
        || VsmHasAnyPageFlag(page_flags[child_index], VSM_PAGE_FLAG_MAPPED_DESCENDANT);
    if (child_mapped) {
        InterlockedOr(page_flags[parent_index].bits, VSM_PAGE_FLAG_MAPPED_DESCENDANT);
    }
}

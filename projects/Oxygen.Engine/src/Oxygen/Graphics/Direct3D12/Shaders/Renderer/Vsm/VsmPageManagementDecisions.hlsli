//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEMANAGEMENTDECISIONS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEMANAGEMENTDECISIONS_HLSLI

#include "Renderer/Vsm/VsmPageFlags.hlsli"
#include "Renderer/Vsm/VsmPhysicalPageMeta.hlsli"

// Stage 6 GPU payload.
//
// Each record rebuilds one reused current-frame mapping directly into the
// current page table and persistent physical-page metadata buffer.
struct VsmShaderPageReuseDecision
{
    uint page_table_index;
    uint physical_page_index;
    VsmShaderPageFlags page_flags;
    uint _pad0;
    VsmPhysicalPageMeta physical_meta;
};

// Stage 8 GPU payload.
//
// The CPU planner already chose the deterministic allocation order. Stage 7
// compacts the available pages into a contiguous stack, and this record tells
// stage 8 which stack slot to consume for each new virtual mapping.
struct VsmShaderPageAllocationDecision
{
    uint page_table_index;
    uint available_page_list_index;
    VsmShaderPageFlags page_flags;
    uint _pad0;
    VsmPhysicalPageMeta physical_meta;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEMANAGEMENTDECISIONS_HLSLI

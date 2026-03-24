//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/Vsm/VsmCommon.hlsli"
#include "Renderer/Vsm/VsmPageTable.hlsli"
#include "Renderer/Vsm/VsmPageFlags.hlsli"
#include "Renderer/Vsm/VsmPhysicalPageMeta.hlsli"
#include "Renderer/Vsm/VsmProjectionData.hlsli"

// Compile-only ABI contract shader.
//
// This shader exists to force DXC to parse the VSM contract headers together as
// one coherent unit during shader bake. It is not a renderer pass and should
// not grow real VSM logic. Keep it tiny and policy-free.

RWByteAddressBuffer g_contract_output : register(u0);

[numthreads(1, 1, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    VsmShaderPageTableEntry entry = VsmMakeMappedPageTableEntry(dispatch_thread_id.x + 1u);
    VsmShaderPageFlags flags;
    flags.bits = VSM_PAGE_FLAG_ALLOCATED | VSM_PAGE_FLAG_DYNAMIC_UNCACHED;

    const uint mapped_page = VsmIsPageTableEntryMapped(entry)
        ? VsmDecodePageTablePhysicalPage(entry)
        : 0u;
    const uint flag_mask = VsmHasAnyPageFlag(flags, VSM_PAGE_FLAG_ALLOCATED)
        ? flags.bits
        : 0u;

    g_contract_output.Store(0u, mapped_page + flag_mask + VSM_MAX_MIP_LEVELS
        + VSM_PAGE_SIZE_TEXELS);
}

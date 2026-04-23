//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Services/Shadows/Vsm/VsmCommon.hlsli"
#include "Vortex/Services/Shadows/Vsm/VsmPageTable.hlsli"
#include "Vortex/Services/Shadows/Vsm/VsmPageFlags.hlsli"
#include "Vortex/Services/Shadows/Vsm/VsmPhysicalPageMeta.hlsli"
#include "Vortex/Services/Shadows/Vsm/VsmProjectionData.hlsli"

// Compile-only ABI contract shader.
//
// This shader exists to force DXC to parse the VSM contract headers together as
// one coherent unit during shader bake. It is not a renderer pass and should
// not grow real VSM logic. Keep it tiny and policy-free.

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

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
    const uint abi_signature = mapped_page + flag_mask + VSM_MAX_MIP_LEVELS
        + VSM_PAGE_SIZE_TEXELS + g_DrawIndex + g_PassConstantsIndex;
    if (abi_signature == 0xffffffffu) {
        return;
    }
}

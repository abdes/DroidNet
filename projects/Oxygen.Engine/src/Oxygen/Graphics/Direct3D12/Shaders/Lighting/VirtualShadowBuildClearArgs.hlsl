//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Lighting/VirtualShadowPassCommon.hlsli"

[shader("compute")]
[numthreads(1, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VirtualShadowPassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IN_GLOBAL_SRV(pass_constants.schedule_count_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.clear_args_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.draw_page_counter_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> schedule_count =
        ResourceDescriptorHeap[pass_constants.schedule_count_uav_index];
    RWStructuredBuffer<DrawIndirectArgs> clear_args =
        ResourceDescriptorHeap[pass_constants.clear_args_uav_index];
    RWStructuredBuffer<uint> draw_page_counter =
        ResourceDescriptorHeap[pass_constants.draw_page_counter_uav_index];

    if (dispatch_thread_id.x != 0u) {
        return;
    }

    draw_page_counter[0] = 0u;
    clear_args[0] = MakeDrawIndirectArgs(6u, schedule_count[0]);
}

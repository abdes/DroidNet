//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Resets a view's GPU debug counters before its producers run: no lines, two
// vertices per line instance, and no invalid accesses.

#include "Vortex/Contracts/Diagnostics/GpuDebugBindings.hlsli"

// g_PassConstantsIndex is the view's GpuDebugBindings record.
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

[shader("compute")]
[numthreads(GPU_DEBUG_COUNTER_COUNT, 1, 1)]
void VortexGpuDebugClearCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    const GpuDebugBindings debug = LoadGpuDebugBindings(g_PassConstantsIndex);
    if (debug.counter_buffer_uav_slot == K_INVALID_BINDLESS_INDEX) {
        return;
    }
    RWStructuredBuffer<uint> counters
        = ResourceDescriptorHeap[debug.counter_buffer_uav_slot];
    counters[dispatch_id.x]
        = dispatch_id.x == GPU_DEBUG_COUNTER_VERTEX_COUNT ? 2u : 0u;
}

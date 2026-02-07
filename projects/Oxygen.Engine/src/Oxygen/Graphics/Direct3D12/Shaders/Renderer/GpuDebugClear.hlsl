//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/SceneConstants.hlsli"

[shader("compute")]
[numthreads(1, 1, 1)]
void CS(uint3 position : SV_DispatchThreadID)
{
    if (bindless_gpu_debug_counter_slot == 0) return;

    RWByteAddressBuffer counterBuffer = ResourceDescriptorHeap[bindless_gpu_debug_counter_slot];

    // D3D12_DRAW_ARGUMENTS:
    // UINT VertexCountPerInstance; -> Offset 0
    // UINT InstanceCount;          -> Offset 4
    // UINT StartVertexLocation;    -> Offset 8
    // UINT StartInstanceLocation;  -> Offset 12

    counterBuffer.Store(0, 2); // 2 vertices per line
    counterBuffer.Store(4, 0); // 0 instances to start
    counterBuffer.Store(8, 0);
    counterBuffer.Store(12, 0);
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DEBUGFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DEBUGFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

struct DebugFrameBindings
{
    uint line_buffer_srv_slot;
    uint line_buffer_uav_slot;
    uint counter_buffer_uav_slot;
    uint _pad_to_16_0;
};

static DebugFrameBindings LoadDebugFrameBindings(uint slot)
{
    DebugFrameBindings invalid_bindings;
    invalid_bindings.line_buffer_srv_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.line_buffer_uav_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.counter_buffer_uav_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings._pad_to_16_0 = 0u;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<DebugFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_DEBUGFRAMEBINDINGS_HLSLI

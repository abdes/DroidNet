//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"

struct EnvironmentFrameBindings
{
    uint environment_static_slot;
    uint environment_view_slot;
    uint _pad_to_16_0;
    uint _pad_to_16_1;
};

static EnvironmentFrameBindings LoadEnvironmentFrameBindings(uint slot)
{
    EnvironmentFrameBindings invalid_bindings = (EnvironmentFrameBindings)0;
    invalid_bindings.environment_static_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.environment_view_slot = K_INVALID_BINDLESS_INDEX;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<EnvironmentFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTFRAMEBINDINGS_HLSLI

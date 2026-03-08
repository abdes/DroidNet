//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_SHADOWFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_SHADOWFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"

struct ShadowFrameBindings
{
    uint directional_shadow_metadata_slot;
    uint _pad_to_16_0;
    uint _pad_to_16_1;
    uint _pad_to_16_2;
};

static ShadowFrameBindings LoadShadowFrameBindings(uint slot)
{
    ShadowFrameBindings invalid_bindings;
    invalid_bindings.directional_shadow_metadata_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings._pad_to_16_0 = 0u;
    invalid_bindings._pad_to_16_1 = 0u;
    invalid_bindings._pad_to_16_2 = 0u;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<ShadowFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_SHADOWFRAMEBINDINGS_HLSLI

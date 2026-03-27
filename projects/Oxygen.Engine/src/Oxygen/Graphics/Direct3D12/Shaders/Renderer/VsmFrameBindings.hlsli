//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSMFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSMFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"

struct VsmFrameBindings
{
    uint directional_shadow_mask_slot;
    uint screen_shadow_mask_slot;
    uint _pad_to_16_0;
    uint _pad_to_16_1;
};

static VsmFrameBindings LoadVsmFrameBindings(uint slot)
{
    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        VsmFrameBindings invalid_bindings;
        invalid_bindings.directional_shadow_mask_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.screen_shadow_mask_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings._pad_to_16_0 = 0u;
        invalid_bindings._pad_to_16_1 = 0u;
        return invalid_bindings;
    }

    StructuredBuffer<VsmFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VSMFRAMEBINDINGS_HLSLI

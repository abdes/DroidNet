//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_DRAWFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_DRAWFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"

struct DrawFrameBindings
{
    uint draw_metadata_slot;
    uint transforms_slot;
    uint normal_matrices_slot;
    uint material_constants_slot;
    uint instance_data_slot;
    uint _pad_to_16_0;
    uint _pad_to_16_1;
    uint _pad_to_16_2;
};

static DrawFrameBindings LoadDrawFrameBindings(uint slot)
{
    DrawFrameBindings invalid_bindings;
    invalid_bindings.draw_metadata_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.transforms_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.normal_matrices_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.material_constants_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.instance_data_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings._pad_to_16_0 = 0u;
    invalid_bindings._pad_to_16_1 = 0u;
    invalid_bindings._pad_to_16_2 = 0u;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<DrawFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_DRAWFRAMEBINDINGS_HLSLI

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
    uint shadow_instance_metadata_slot;
    uint directional_shadow_metadata_slot;
    uint directional_shadow_texture_slot;
    uint virtual_shadow_page_table_slot;
    uint virtual_shadow_physical_pool_slot;
    uint virtual_directional_shadow_metadata_slot;
    uint sun_shadow_index;
    uint _reserved0;
};

static ShadowFrameBindings LoadShadowFrameBindings(uint slot)
{
    ShadowFrameBindings invalid_bindings;
    invalid_bindings.shadow_instance_metadata_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.directional_shadow_metadata_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.directional_shadow_texture_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.virtual_shadow_page_table_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.virtual_shadow_physical_pool_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.virtual_directional_shadow_metadata_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.sun_shadow_index = K_INVALID_BINDLESS_INDEX;
    invalid_bindings._reserved0 = 0u;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<ShadowFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_SHADOWFRAMEBINDINGS_HLSLI

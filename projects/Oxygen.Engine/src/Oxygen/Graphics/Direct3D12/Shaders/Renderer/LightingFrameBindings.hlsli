//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/LightCullingConfig.hlsli"
#include "Renderer/SyntheticSunData.hlsli"

struct LightingFrameBindings
{
    uint directional_lights_slot;
    uint positional_lights_slot;
    uint _pad0;
    uint _pad1;
    LightCullingConfig light_culling;
    SyntheticSunData sun;
};

static LightingFrameBindings LoadLightingFrameBindings(uint slot)
{
    LightingFrameBindings invalid_bindings = (LightingFrameBindings)0;
    invalid_bindings.directional_lights_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.positional_lights_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.light_culling.bindless_cluster_grid_slot = K_INVALID_BINDLESS_INDEX;
    invalid_bindings.light_culling.bindless_cluster_index_list_slot = K_INVALID_BINDLESS_INDEX;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_bindings;
    }

    StructuredBuffer<LightingFrameBindings> bindings_buffer = ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_LIGHTINGFRAMEBINDINGS_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_VIEWFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_VIEWFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Contracts/Scene/SceneTextureBindings.hlsli"

struct ViewFrameBindings
{
    uint draw_frame_slot;
    uint lighting_frame_slot;
    uint environment_frame_slot;
    uint view_color_frame_slot;
    uint scene_texture_frame_slot;
    uint scene_depth_slot;
    uint screen_hzb_frame_slot;
    uint shadow_frame_slot;
    uint virtual_shadow_frame_slot;
    uint post_process_frame_slot;
    uint debug_frame_slot;
    uint history_frame_slot;
    uint ray_tracing_frame_slot;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

static inline ViewFrameBindings MakeInvalidViewFrameBindings()
{
    ViewFrameBindings bindings;
    bindings.draw_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.lighting_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.environment_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.view_color_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.scene_texture_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.scene_depth_slot = K_INVALID_BINDLESS_INDEX;
    bindings.screen_hzb_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.shadow_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.virtual_shadow_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.post_process_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.debug_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.history_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings.ray_tracing_frame_slot = K_INVALID_BINDLESS_INDEX;
    bindings._pad0 = 0u;
    bindings._pad1 = 0u;
    bindings._pad2 = 0u;
    return bindings;
}

static inline ViewFrameBindings LoadViewFrameBindings(uint slot)
{
    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return MakeInvalidViewFrameBindings();
    }

    StructuredBuffer<ViewFrameBindings> bindings_buffer = ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

static inline SceneTextureBindingData LoadSceneTextureBindings(uint view_frame_bindings_slot)
{
    const ViewFrameBindings view_bindings = LoadViewFrameBindings(view_frame_bindings_slot);
    if (view_bindings.scene_texture_frame_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(view_bindings.scene_texture_frame_slot)) {
        return MakeInvalidSceneTextureBindings();
    }

    StructuredBuffer<SceneTextureBindingData> bindings_buffer =
        ResourceDescriptorHeap[view_bindings.scene_texture_frame_slot];
    return bindings_buffer[0];
}

#define ViewFrameBindingsData ViewFrameBindings
#define MakeInvalidVortexViewFrameBindings MakeInvalidViewFrameBindings
#define LoadVortexViewFrameBindings LoadViewFrameBindings

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_VIEWFRAMEBINDINGS_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VIEWFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VIEWFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

struct ViewFrameBindings
{
    uint draw_frame_slot;
    uint lighting_frame_slot;
    uint environment_frame_slot;
    uint view_color_frame_slot;
    uint scene_depth_slot;
    uint shadow_frame_slot;
    uint virtual_shadow_frame_slot;
    uint post_process_frame_slot;
    uint debug_frame_slot;
    uint history_frame_slot;
    uint ray_tracing_frame_slot;
    uint _pad_to_16_0;
};

static ViewFrameBindings LoadViewFrameBindings(uint slot)
{
    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        ViewFrameBindings invalid_bindings;
        invalid_bindings.draw_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.lighting_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.environment_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.view_color_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.scene_depth_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.shadow_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.virtual_shadow_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.post_process_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.debug_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.history_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings.ray_tracing_frame_slot = K_INVALID_BINDLESS_INDEX;
        invalid_bindings._pad_to_16_0 = 0u;
        return invalid_bindings;
    }

    StructuredBuffer<ViewFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VIEWFRAMEBINDINGS_HLSLI

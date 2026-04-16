//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VIEWHISTORYFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VIEWHISTORYFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Renderer/ViewConstants.hlsli"

static const uint VIEW_HISTORY_FLAG_PREVIOUS_VIEW_VALID = (1u << 0u);

struct ViewHistoryFrameBindings
{
    float4x4 current_view_matrix;
    float4x4 current_projection_matrix;
    float4x4 current_stable_projection_matrix;
    float4x4 current_inverse_view_projection_matrix;
    float4x4 previous_view_matrix;
    float4x4 previous_projection_matrix;
    float4x4 previous_stable_projection_matrix;
    float4x4 previous_inverse_view_projection_matrix;
    float2 current_pixel_jitter;
    float2 previous_pixel_jitter;
    uint validity_flags;
    uint _pad_to_16_0;
};

static ViewHistoryFrameBindings LoadViewHistoryFrameBindings(uint slot)
{
    ViewHistoryFrameBindings history = (ViewHistoryFrameBindings)0;
    history.current_view_matrix = view_matrix;
    history.current_projection_matrix = projection_matrix;
    history.current_stable_projection_matrix = projection_matrix;
    history.current_inverse_view_projection_matrix = inverse_view_projection_matrix;
    history.previous_view_matrix = view_matrix;
    history.previous_projection_matrix = projection_matrix;
    history.previous_stable_projection_matrix = projection_matrix;
    history.previous_inverse_view_projection_matrix = inverse_view_projection_matrix;

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return history;
    }

    StructuredBuffer<ViewHistoryFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    return bindings_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VIEWHISTORYFRAMEBINDINGS_HLSLI

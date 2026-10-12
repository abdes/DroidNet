//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VIEWHISTORYFRAMEBINDINGS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VIEWHISTORYFRAMEBINDINGS_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

static const uint VIEW_HISTORY_FLAG_PREVIOUS_VIEW_VALID = (1u << 0u);

// The current view and projection matrices live in the view constants; this
// record adds the unjittered current projection and the previous view state.
struct ViewHistoryFrameBindings
{
    float4x4 current_stable_projection_matrix;
    float4x4 previous_view_matrix;
    float4x4 previous_projection_matrix;
    float4x4 previous_stable_projection_matrix;
    float4x4 previous_inverse_view_projection_matrix;
    float2 current_pixel_jitter;
    float2 previous_pixel_jitter;
    float4 current_view_rect_min_and_size;
    float4 previous_view_rect_min_and_size;
    uint validity_flags;
    uint _pad_to_16_0;
    uint _pad_to_16_1;
    uint _pad_to_16_2;
};

// Load a view's history record; false, with zeroed history, when the slot is
// missing or outside the global SRV domain. The renderer publishes history for
// every view with a resolved camera, so a missing record is an error, never
// "history equals current".
static bool TryLoadViewHistoryFrameBindings(
    uint slot, out ViewHistoryFrameBindings history)
{
    history = (ViewHistoryFrameBindings)0;
    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return false;
    }

    StructuredBuffer<ViewHistoryFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[slot];
    history = bindings_buffer[0];
    return true;
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VIEWHISTORYFRAMEBINDINGS_HLSLI

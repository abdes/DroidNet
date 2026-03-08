//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VIEWCOLORDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VIEWCOLORDATA_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"

struct ViewColorData
{
    float exposure;
    float3 _pad_to_16;
};

static ViewColorData LoadViewColorData(uint slot)
{
    ViewColorData invalid_data;
    invalid_data.exposure = 1.0f;
    invalid_data._pad_to_16 = float3(0.0f, 0.0f, 0.0f);

    if (slot == K_INVALID_BINDLESS_INDEX || !BX_IN_GLOBAL_SRV(slot)) {
        return invalid_data;
    }

    StructuredBuffer<ViewColorData> data_buffer = ResourceDescriptorHeap[slot];
    return data_buffer[0];
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VIEWCOLORDATA_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"

struct CompositingVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Root constants b2 (shared root param index with engine)
// ABI layout:
//   g_DrawIndex          : unused for draw
//   g_PassConstantsIndex : heap index of a CBV holding pass constants
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Pass constants for compositing.
// Layout must match C++ CompositingPassConstants.
struct CompositingPassConstants {
    uint source_texture_index;
    uint sampler_index;
    float alpha;
    float pad0;
};

float4 PS(CompositingVSOutput input) : SV_TARGET
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    ConstantBuffer<CompositingPassConstants> pass
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    if (pass.source_texture_index == K_INVALID_BINDLESS_INDEX) {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    const uint sampler_index = pass.sampler_index != K_INVALID_BINDLESS_INDEX
        ? pass.sampler_index
        : 0u;

    Texture2D<float4> src_tex
        = ResourceDescriptorHeap[pass.source_texture_index];
    SamplerState samp = SamplerDescriptorHeap[sampler_index];
    float4 color = src_tex.Sample(samp, input.uv);
    color.a *= pass.alpha;
    return color;
}

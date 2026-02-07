//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/FullscreenTriangle.hlsli"
#include "Core/Bindless/Generated.BindlessLayout.hlsl"

struct ToneMapVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

ToneMapVSOutput VS(uint vertex_id : SV_VertexID)
{
    ToneMapVSOutput output;
    GenerateFullscreenTriangle(vertex_id, output.position, output.uv);
    return output;
}

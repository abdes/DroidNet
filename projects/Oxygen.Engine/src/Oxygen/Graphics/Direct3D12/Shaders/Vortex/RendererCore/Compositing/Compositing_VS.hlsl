//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Shared/FullscreenTriangle.hlsli"
#include "Core/Bindless/Generated.BindlessAbi.hlsl"

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

CompositingVSOutput VS(uint vertex_id : SV_VertexID)
{
    const VortexFullscreenTriangleOutput fullscreen =
        GenerateVortexFullscreenTriangle(vertex_id);

    CompositingVSOutput output;
    output.position = fullscreen.position;
    output.uv = fullscreen.uv;
    return output;
}

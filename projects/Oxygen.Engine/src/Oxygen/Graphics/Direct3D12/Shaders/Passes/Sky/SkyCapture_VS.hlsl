//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/FullscreenTriangle.hlsli"

#include "Core/Bindless/Generated.BindlessLayout.hlsl"

struct SkyCaptureVSOutput
{
    float4 position : SV_POSITION;
    float3 view_dir : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

// Root constants b2 (shared root param index with engine)
// ABI layout:
//   g_DrawIndex          : unused for dispatch
//   g_PassConstantsIndex : heap index of a CBV holding pass constants
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Pass constants for sky capture.
// This CBV is fetched via ResourceDescriptorHeap[g_PassConstantsIndex].
struct SkyCapturePassConstants {
    float4x4 face_rotation;
};

SkyCaptureVSOutput VS(uint vertex_id : SV_VertexID)
{
    SkyCaptureVSOutput output;
    float4 clip_pos;
    float2 uv;
    GenerateFullscreenTriangle(vertex_id, clip_pos, uv);

    output.position = clip_pos;
    output.uv = uv;

    // View space ray for 90 degree FOV cube face.
    float3 ray_vs = float3(clip_pos.x, clip_pos.y, -1.0);

    // Rotate to world space using the per-face rotation matrix.
    // If pass constants aren't bound, fall back to an arbitrary direction.
    output.view_dir = float3(0.0, 0.0, 1.0);
    if (g_PassConstantsIndex != K_INVALID_BINDLESS_INDEX
        && BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        ConstantBuffer<SkyCapturePassConstants> pass
            = ResourceDescriptorHeap[g_PassConstantsIndex];
        output.view_dir = mul((float3x3)pass.face_rotation, ray_vs);
    }

    return output;
}

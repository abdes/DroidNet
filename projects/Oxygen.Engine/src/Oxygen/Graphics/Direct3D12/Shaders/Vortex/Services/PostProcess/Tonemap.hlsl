//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_SceneSignalSrv;
    float g_ExposureValue;
}

static float3 ACESFilm(float3 x)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

[shader("vertex")]
VortexFullscreenTriangleOutput VortexTonemapVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 VortexTonemapPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    if (g_SceneSignalSrv == INVALID_BINDLESS_INDEX) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    Texture2D<float4> scene_signal = ResourceDescriptorHeap[g_SceneSignalSrv];
    uint width = 0u;
    uint height = 0u;
    scene_signal.GetDimensions(width, height);

    const uint2 pixel = min(
        uint2(input.uv * float2(width, height)),
        uint2(max(width, 1u) - 1u, max(height, 1u) - 1u));
    const float3 hdr
        = scene_signal.Load(int3(pixel, 0)).rgb * max(g_ExposureValue, 1.0e-4f);
    const float3 ldr = ACESFilm(hdr);
    return float4(ldr, 1.0f);
}

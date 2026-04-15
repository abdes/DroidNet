//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/GBufferHelpers.hlsli"
#include "Vortex/Contracts/ViewFrameBindings.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_ViewFrameBindingsSlot;
    uint g_UnusedPadding;
}

static float3 EvaluateBasePassDebugView(
    float2 uv, SceneTextureBindingData bindings)
{
    const GBufferData data = ReadGBuffer(uv, bindings);

#if defined(DEBUG_BASE_COLOR)
    return saturate(data.base_color);
#elif defined(DEBUG_WORLD_NORMALS)
    return saturate(data.world_normal * 0.5f + 0.5f);
#elif defined(DEBUG_ROUGHNESS)
    return saturate(data.roughness.xxx);
#elif defined(DEBUG_METALNESS)
    return saturate(data.metallic.xxx);
#else
    return float3(1.0f, 0.0f, 1.0f);
#endif
}

[shader("vertex")]
VortexFullscreenTriangleOutput BasePassDebugViewVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 BasePassDebugViewPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    const SceneTextureBindingData bindings
        = LoadSceneTextureBindings(g_ViewFrameBindingsSlot);
    if (!IsSceneTextureValid(bindings, SCENE_TEXTURE_FLAG_GBUFFERS)) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    return float4(EvaluateBasePassDebugView(input.uv, bindings), 1.0f);
}

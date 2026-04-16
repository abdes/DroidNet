//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Services/Lighting/DeferredLightingCommon.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

[shader("vertex")]
VortexFullscreenTriangleOutput DeferredLightDirectionalVS(uint vertex_id : SV_VertexID)
{
    return GenerateVortexFullscreenTriangle(vertex_id);
}

[shader("pixel")]
float4 DeferredLightDirectionalPS(VortexFullscreenTriangleOutput input) : SV_Target0
{
    if (g_PassConstantsIndex == INVALID_BINDLESS_INDEX) {
        return 0.0f.xxxx;
    }

    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    const SceneTextureBindingData bindings = LoadBindingsFromCurrentView();
    const float3 lighting = EvaluateDeferredLight(
        input.uv,
        VortexSafeNormalize(light_constants.light_direction_and_falloff.xyz),
        LoadDeferredLightColor(light_constants.light_color_and_intensity),
        1.0f,
        camera_position,
        bindings);
    return float4(lighting, 0.0f);
}

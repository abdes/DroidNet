//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Services/Lighting/DeferredLightingCommon.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

[shader("vertex")]
DeferredLightVolumeVSOutput DeferredLightPointVS(float3 local_position : POSITION)
{
    DeferredLightVolumeVSOutput output = (DeferredLightVolumeVSOutput)0;
    if (g_PassConstantsIndex == INVALID_BINDLESS_INDEX) {
        return output;
    }

    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    return GenerateDeferredLightVolume(
        local_position, light_constants.light_world_matrix);
}

[shader("pixel")]
float4 DeferredLightPointPS(DeferredLightVolumeVSOutput input) : SV_Target0
{
    if (g_PassConstantsIndex == INVALID_BINDLESS_INDEX) {
        return 0.0f.xxxx;
    }

    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    const SceneTextureBindingData bindings = LoadBindingsFromCurrentView();
    if (!HasDeferredLightingInputs(bindings)) {
        return 0.0f.xxxx;
    }

    const float3 world_position = ReconstructWorldPositionFromViewMatrices(
        input.screen_uv, SampleSceneDepth(input.screen_uv, bindings),
        view_matrix, projection_matrix, camera_position);
    const float3 light_vector
        = light_constants.light_position_and_radius.xyz - world_position;
    const float attenuation = ComputeLocalLightDistanceAttenuation(
        light_vector, light_constants.light_position_and_radius.w);
    const float3 lighting = EvaluateDeferredLight(
        input.screen_uv,
        VortexSafeNormalize(light_vector),
        LoadDeferredLightColor(light_constants.light_color_and_intensity),
        attenuation,
        view_matrix,
        projection_matrix,
        camera_position,
        bindings);
    return float4(lighting, 0.0f);
}

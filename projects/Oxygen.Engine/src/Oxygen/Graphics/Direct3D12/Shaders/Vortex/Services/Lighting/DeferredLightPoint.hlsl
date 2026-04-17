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
DeferredLightVolumeVSOutput DeferredLightPointVS(uint vertex_id : SV_VertexID)
{
    DeferredLightVolumeVSOutput output = (DeferredLightVolumeVSOutput)0;
    if (g_PassConstantsIndex == INVALID_BINDLESS_INDEX) {
        return output;
    }

    ConstantBuffer<DeferredLightConstants> light_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    return GenerateDeferredLightVolume(
        LoadDeferredLightGeometryVertex(
            light_constants.light_geometry_vertices_srv,
            vertex_id),
        light_constants.light_world_matrix);
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

    const float2 screen_uv = ResolveDeferredLightScreenUv(input.screen_position);
    const float scene_depth = SampleSceneDepth(screen_uv, bindings);
    const float3 world_position
        = ReconstructDeferredWorldPosition(screen_uv, scene_depth);
    const float3 light_vector
        = light_constants.light_position_and_radius.xyz - world_position;
    const float attenuation = ComputeLocalLightDistanceAttenuation(
        light_vector, light_constants.light_position_and_radius.w);
    const float3 lighting = EvaluateDeferredLightAtWorldPosition(
        screen_uv,
        scene_depth,
        world_position,
        VortexSafeNormalize(light_vector),
        LoadDeferredLightColor(light_constants.light_color_and_intensity),
        attenuation,
        camera_position,
        bindings);
    return float4(lighting, 0.0f);
}

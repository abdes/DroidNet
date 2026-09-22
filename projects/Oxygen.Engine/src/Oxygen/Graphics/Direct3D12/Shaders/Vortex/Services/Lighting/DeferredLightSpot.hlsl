//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Vortex/Services/Lighting/DeferredLightingCommon.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

[shader("vertex")]
DeferredLightVolumeVSOutput DeferredLightSpotVS(uint vertex_id : SV_VertexID)
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
float4 DeferredLightSpotPS(DeferredLightVolumeVSOutput input) : SV_Target0
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
    const LightingFrameBindings lighting_bindings = LoadResolvedLightingFrameBindings();
    ForwardLocalLightRecord light;
    if (!TryLoadLocalLight(lighting_bindings, light_constants.selection_index, light)) return 0.0f.xxxx;
    const LightShadowReference shadow_reference = LoadLightShadowReference(
        lighting_bindings.local_shadow_map_srv, light.selection_index);
    const float3 light_vector
        = light.position_ws - world_position;
    const float base_attenuation = ComputeLocalLightDistanceAttenuation(
        light_vector, light.range_m);
    const float spot_attenuation = ComputeSpotLightAngularAttenuation(
        VortexSafeNormalize(light_vector),
        light.emitted_direction_ws,
        light.inner_cone_sin_half_squared,
        light.outer_cone_sin_half_squared);
    float shadow_visibility = 1.0f;
    if (shadow_reference.record_index != INVALID_BINDLESS_INDEX) {
        const VortexShadowFrameBindings shadow_bindings =
            LoadVortexShadowFrameBindings();
        const GBufferData gbuffer = ReadGBuffer(screen_uv, bindings);
        shadow_visibility = ComputeSpotShadowVisibility(
            shadow_bindings,
            shadow_reference.record_index,
            world_position,
            gbuffer.world_normal,
            VortexSafeNormalize(light_vector));
    }
    const float3 lighting = EvaluateDeferredLightAtWorldPosition(
        screen_uv,
        scene_depth,
        world_position,
        VortexSafeNormalize(light_vector),
        light.intensity_rgb_cd,
        base_attenuation * spot_attenuation * shadow_visibility,
        camera_position,
        bindings);
    RecordHdrSceneSource(lighting, 2u);
    return float4(lighting * GetPreExposure(), 0.0f);
}

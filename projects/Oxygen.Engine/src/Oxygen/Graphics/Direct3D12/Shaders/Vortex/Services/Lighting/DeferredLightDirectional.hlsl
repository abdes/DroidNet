//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Services/Lighting/DeferredLightingCommon.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"
#include "Vortex/Services/Lighting/AtmosphereDirectionalLightShared.hlsli"
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
    const float scene_depth = SampleSceneDepth(input.uv, bindings);
    const float3 world_position =
        ReconstructDeferredWorldPosition(input.uv, scene_depth);
    const float3 light_dir =
        VortexSafeNormalize(light_constants.light_direction_and_falloff.xyz);
    const DeferredLightingSurfaceData surface = LoadDeferredLightingSurface(
        input.uv, world_position, camera_position, bindings);
    float light_attenuation = 1.0f;
    if (light_constants.shadow_info.x > 0u) {
        light_attenuation = ComputeDirectionalShadowVisibility(
            world_position,
            surface.world_normal,
            light_dir);
    }
    const float3 deferred_light_radiance = ResolveDirectionalLightAtmosphereRadiance(
        world_position,
        light_dir,
        light_constants.atmosphere_transmittance_and_padding.xyz,
        light_constants.shadow_info.w,
        LoadDeferredLightColor(light_constants.light_color_and_intensity));
#if defined(DEBUG_DIRECT_LIGHTING_ONLY)
    const float NoL = saturate(dot(surface.world_normal, light_dir));
    return float4(surface.base_color * deferred_light_radiance * NoL, 0.0f);
#elif defined(DEBUG_DIRECT_LIGHT_GATES)
    const float transmittance_luma = dot(
        saturate(light_constants.atmosphere_transmittance_and_padding.xyz),
        float3(0.2126f, 0.7152f, 0.0722f));
    return float4(saturate(light_attenuation), saturate(transmittance_luma), 0.0f, 0.0f);
#elif defined(DEBUG_DIRECT_BRDF_CORE)
    return float4(EvaluateCookTorranceLighting(surface, light_dir, 1.0f.xxx), 0.0f);
#endif
    const float3 lighting = EvaluateDeferredLightAtWorldPosition(
        input.uv,
        scene_depth,
        world_position,
        light_dir,
        deferred_light_radiance,
        light_attenuation,
        camera_position,
        bindings);
    return float4(lighting, 0.0f);
}

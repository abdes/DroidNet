//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Services/Lighting/DeferredLightingCommon.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"
#include "Renderer/AtmosphereLightingHelpers.hlsli"
#include "Vortex/Shared/FullscreenTriangle.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint kDirectionalLightFlagEnvContribution = 1u << 3u;
static const uint kDirectionalLightFlagSunLight = 1u << 4u;
static const uint kDirectionalLightFlagPerPixelAtmosphereTransmittance = 1u << 5u;

static float3 ResolveAtmosphereDirectionalLightColor(
    float3 world_position,
    uint light_flags,
    float3 light_direction_ws,
    float3 deferred_light_color)
{
    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (!LoadEnvironmentStaticData(env_data))
    {
        return deferred_light_color;
    }

    if (env_data.atmosphere.enabled == 0u)
    {
        return deferred_light_color;
    }

    const bool is_sun = (light_flags & kDirectionalLightFlagSunLight) != 0u;
    const bool env_contribution = (light_flags & kDirectionalLightFlagEnvContribution) != 0u;
    const bool per_pixel_transmittance
        = (light_flags & kDirectionalLightFlagPerPixelAtmosphereTransmittance) != 0u;
    if ((!is_sun && !env_contribution) || per_pixel_transmittance)
    {
        return deferred_light_color;
    }

    const float3 transmittance = saturate(
        ComputeSunTransmittance(
            world_position,
            env_data.atmosphere,
            VortexSafeNormalize(light_direction_ws)));
    return deferred_light_color * transmittance;
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
    float light_attenuation = 1.0f;
    if (light_constants.shadow_info.x > 0u) {
        const DeferredLightingSurfaceData surface = LoadDeferredLightingSurface(
            input.uv, world_position, camera_position, bindings);
        light_attenuation = ComputeDirectionalShadowVisibility(
            world_position,
            surface.world_normal,
            VortexSafeNormalize(light_constants.light_direction_and_falloff.xyz));
    }
    const float3 lighting = EvaluateDeferredLightAtWorldPosition(
        input.uv,
        scene_depth,
        world_position,
        VortexSafeNormalize(light_constants.light_direction_and_falloff.xyz),
        ResolveAtmosphereDirectionalLightColor(
            world_position,
            light_constants.shadow_info.y,
            light_constants.light_direction_and_falloff.xyz,
            LoadDeferredLightColor(light_constants.light_color_and_intensity)),
        light_attenuation,
        camera_position,
        bindings);
    return float4(lighting, 0.0f);
}

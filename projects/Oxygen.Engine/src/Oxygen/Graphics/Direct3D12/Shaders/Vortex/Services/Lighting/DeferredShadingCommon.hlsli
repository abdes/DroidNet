//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDSHADINGCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDSHADINGCOMMON_HLSLI

#include "Vortex/Shared/Math.hlsli"

#include "Vortex/Contracts/Scene/GBufferHelpers.hlsli"
#include "Vortex/Contracts/Scene/SceneTextures.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Shared/BRDFCommon.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

static const float kVortexDeferredMinRoughness = 0.045f;

struct DeferredLightingSurfaceData
{
    float3 world_position;
    float3 world_normal;
    float3 base_color;
    float3 view_direction;
    float3 specular_f0;
    float metallic;
    float specular;
    float roughness;
    float ambient_occlusion;
};

static inline bool HasDeferredLightingInputs(SceneTextureBindingData bindings)
{
    return IsSceneTextureValid(bindings, SCENE_TEXTURE_FLAG_SCENE_DEPTH)
        && IsSceneTextureValid(bindings, SCENE_TEXTURE_FLAG_GBUFFERS);
}

static inline float3 ReconstructDeferredWorldPosition(
    float2 screen_uv, float device_depth)
{
    return ReconstructWorldPosition(
        screen_uv, device_depth, inverse_view_projection_matrix);
}

static inline DeferredLightingSurfaceData LoadDeferredLightingSurface(
    float2 uv,
    float3 world_position,
    float3 camera_position_ws,
    SceneTextureBindingData bindings)
{
    DeferredLightingSurfaceData surface = (DeferredLightingSurfaceData)0;
    surface.world_position = world_position;

    const GBufferData gbuffer = ReadGBuffer(uv, bindings);
    surface.world_normal = VortexSafeNormalize(gbuffer.world_normal);
    surface.base_color = max(gbuffer.base_color, 0.0f.xxx);
    surface.view_direction
        = VortexSafeNormalize(camera_position_ws - surface.world_position);
    surface.metallic = saturate(gbuffer.metallic);
    surface.specular = saturate(gbuffer.specular);
    surface.roughness = max(saturate(gbuffer.roughness), kVortexDeferredMinRoughness);
    surface.ambient_occlusion = saturate(gbuffer.ambient_occlusion);
    surface.specular_f0 = ComputeMetallicF0(
        surface.base_color, surface.metallic, surface.specular);
    return surface;
}

static inline float3 EvaluateCookTorranceLighting(
    DeferredLightingSurfaceData surface,
    float3 light_direction_to_source,
    float3 light_radiance, LightingFrameBindings lighting)
{
    const float3 L = normalize(light_direction_to_source);
    return EvaluateGgxDirectResponse(surface.world_normal, surface.view_direction, L,
        surface.specular_f0, surface.base_color * (1.0 - surface.metallic),
        surface.roughness, lighting) * light_radiance;
}

static inline float3 EvaluateDeferredLightAtWorldPosition(
    float2 uv,
    float scene_depth,
    float3 world_position,
    float3 light_direction_to_source,
    float3 light_radiance,
    float light_attenuation,
    float3 camera_position_ws,
    SceneTextureBindingData bindings)
{
    if (!HasDeferredLightingInputs(bindings) || light_attenuation <= 0.0f) {
        return 0.0f.xxx;
    }

    if (scene_depth >= 1.0f) {
        return 0.0f.xxx;
    }

    const DeferredLightingSurfaceData surface = LoadDeferredLightingSurface(
        uv, world_position, camera_position_ws, bindings);
    return EvaluateCookTorranceLighting(
               surface, light_direction_to_source, light_radiance, LoadResolvedLightingFrameBindings())
        * light_attenuation;
}

static inline float3 EvaluateDeferredStaticSkyLightDiffuse(
    DeferredLightingSurfaceData surface)
{
    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (!LoadEnvironmentStaticData(env_data)
        || env_data.sky_light.enabled == 0u) {
        return 0.0f.xxx;
    }

    const float3 sky_diffuse = EvaluateStaticSkyLightDiffuseSh(
                                   env_data, surface.world_normal)
        * env_data.sky_light.tint_rgb
        * env_data.sky_light.radiance_scale
        * env_data.sky_light.diffuse_intensity;
    return sky_diffuse * surface.base_color * (1.0f - surface.metallic);
}

static inline float3 EvaluateDeferredLight(
    float2 uv,
    float3 light_direction_to_source,
    float3 light_radiance,
    float light_attenuation,
    float3 camera_position_ws,
    SceneTextureBindingData bindings)
{
    if (!HasDeferredLightingInputs(bindings) || light_attenuation <= 0.0f) {
        return 0.0f.xxx;
    }

    const float scene_depth = SampleSceneDepth(uv, bindings);
    const float3 world_position = ReconstructDeferredWorldPosition(uv, scene_depth);
    return EvaluateDeferredLightAtWorldPosition(uv, scene_depth, world_position,
        light_direction_to_source, light_radiance, light_attenuation,
        camera_position_ws, bindings);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_DEFERREDSHADINGCOMMON_HLSLI

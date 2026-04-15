//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SHARED_DEFERREDSHADINGCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SHARED_DEFERREDSHADINGCOMMON_HLSLI

#include "Common/Math.hlsli"

#include "Vortex/Contracts/GBufferHelpers.hlsli"
#include "Vortex/Contracts/SceneTextures.hlsli"
#include "Vortex/Shared/BRDFCommon.hlsli"
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

static inline float3 ReconstructWorldPositionFromViewMatrices(
    float2 screen_uv,
    float device_depth,
    float4x4 world_to_view_matrix,
    float4x4 view_to_clip_matrix,
    float3 camera_position_ws)
{
    const float2 ndc_xy = float2(
        mad(screen_uv.x, 2.0f, -1.0f),
        mad(1.0f - screen_uv.y, 2.0f, -1.0f));
    const float view_depth = max(device_depth, 1.0e-4f);
    const float proj_x = abs(view_to_clip_matrix[0][0]) > 1.0e-6f
        ? view_to_clip_matrix[0][0]
        : 1.0f;
    const float proj_y = abs(view_to_clip_matrix[1][1]) > 1.0e-6f
        ? view_to_clip_matrix[1][1]
        : 1.0f;
    const float3 view_position = float3(
        ndc_xy.x * view_depth / proj_x,
        ndc_xy.y * view_depth / proj_y,
        -view_depth);
    const float3x3 view_rotation = (float3x3)world_to_view_matrix;
    return mul(transpose(view_rotation), view_position) + camera_position_ws;
}

static inline float3 FresnelSchlick(float cos_theta, float3 f0)
{
    const float one_minus_cos = saturate(1.0f - cos_theta);
    const float fresnel = pow(one_minus_cos, 5.0f);
    return f0 + (1.0f.xxx - f0) * fresnel;
}

static inline float DistributionGGX(float NoH, float roughness)
{
    const float alpha = max(roughness * roughness, kVortexDeferredMinRoughness);
    const float alpha_sq = alpha * alpha;
    const float denom = NoH * NoH * (alpha_sq - 1.0f) + 1.0f;
    return alpha_sq / max(PI * denom * denom, EPSILON);
}

static inline float GeometrySchlickGGX(float NoX, float roughness)
{
    const float k = pow(max(roughness + 1.0f, kVortexDeferredMinRoughness), 2.0f)
        * 0.125f;
    return NoX / max(NoX * (1.0f - k) + k, EPSILON);
}

static inline float GeometrySmith(float NoV, float NoL, float roughness)
{
    return GeometrySchlickGGX(NoV, roughness)
        * GeometrySchlickGGX(NoL, roughness);
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
    float3 light_radiance)
{
    const float3 light_direction = VortexSafeNormalize(light_direction_to_source);
    const float3 half_vector = VortexSafeNormalize(
        surface.view_direction + light_direction);

    const float NoL = saturate(dot(surface.world_normal, light_direction));
    const float NoV = saturate(dot(surface.world_normal, surface.view_direction));
    const float NoH = saturate(dot(surface.world_normal, half_vector));
    const float VoH = saturate(dot(surface.view_direction, half_vector));

    if (NoL <= EPSILON || NoV <= EPSILON) {
        return 0.0f.xxx;
    }

    const float3 fresnel = FresnelSchlick(VoH, surface.specular_f0);
    const float distribution = DistributionGGX(NoH, surface.roughness);
    const float geometry = GeometrySmith(NoV, NoL, surface.roughness);

    const float3 numerator = distribution * geometry * fresnel;
    const float denominator = max(4.0f * NoV * NoL, 1.0e-4f);
    const float3 specular = numerator / denominator;

    const float3 diffuse_color
        = surface.base_color * (1.0f.xxx - fresnel) * (1.0f - surface.metallic);
    const float3 diffuse = diffuse_color * INV_PI;

    return (diffuse + specular) * light_radiance * NoL
        * surface.ambient_occlusion;
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
               surface, light_direction_to_source, light_radiance)
        * light_attenuation;
}

static inline float3 EvaluateDeferredLight(
    float2 uv,
    float3 light_direction_to_source,
    float3 light_radiance,
    float light_attenuation,
    float4x4 world_to_view_matrix,
    float4x4 view_to_clip_matrix,
    float3 camera_position_ws,
    SceneTextureBindingData bindings)
{
    if (!HasDeferredLightingInputs(bindings) || light_attenuation <= 0.0f) {
        return 0.0f.xxx;
    }

    const float scene_depth = SampleSceneDepth(uv, bindings);
    const float3 world_position = ReconstructWorldPositionFromViewMatrices(
        uv, scene_depth, world_to_view_matrix, view_to_clip_matrix,
        camera_position_ws);
    return EvaluateDeferredLightAtWorldPosition(uv, scene_depth, world_position,
        light_direction_to_source, light_radiance, light_attenuation,
        camera_position_ws, bindings);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SHARED_DEFERREDSHADINGCOMMON_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_SKY_RADIANCE_HLSLI
#define OXYGEN_VORTEX_SKY_RADIANCE_HLSLI

#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentViewHelpers.hlsli"
#include "Vortex/Services/Environment/AtmosphereParityCommon.hlsli"
#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"

static inline float2 ResolveSkyViewUvFromLocalDirection(
    EnvironmentStaticData env_data,
    EnvironmentViewData environment_view,
    float3 view_direction_local,
    out float view_height,
    out float bottom_radius,
    out float top_radius)
{
    view_height = environment_view.sky_planet_translated_world_center_km_and_view_height_km.w;
    bottom_radius = env_data.atmosphere.planet_radius_km;
    top_radius = env_data.atmosphere.planet_radius_km + env_data.atmosphere.atmosphere_height_km;

    const float view_zenith_cos_angle = view_direction_local.z;
    const bool intersect_ground = RaySphereIntersectNearest(
        float3(0.0f, 0.0f, view_height),
        view_direction_local,
        bottom_radius) >= 0.0f;
    const float2 sky_view_lut_inv_size = float2(
        env_data.atmosphere.sky_view_lut_width > 0.0f
            ? rcp(env_data.atmosphere.sky_view_lut_width)
            : 0.0f,
        env_data.atmosphere.sky_view_lut_height > 0.0f
            ? rcp(env_data.atmosphere.sky_view_lut_height)
            : 0.0f);
    const float2 sky_view_lut_size = float2(
        env_data.atmosphere.sky_view_lut_width,
        env_data.atmosphere.sky_view_lut_height);
    return SkyViewLutParamsToUv(
        intersect_ground,
        view_zenith_cos_angle,
        view_direction_local,
        view_height,
        bottom_radius,
        sky_view_lut_size,
        sky_view_lut_inv_size);
}

static inline float2 ResolveSkyViewUv(
    EnvironmentStaticData env_data,
    EnvironmentViewData environment_view,
    float3 view_direction,
    out float3 view_direction_local,
    out float view_height,
    out float bottom_radius,
    out float top_radius)
{
    view_direction_local = ApplySkyViewLutReferential(environment_view, view_direction);
    return ResolveSkyViewUvFromLocalDirection(
        env_data,
        environment_view,
        view_direction_local,
        view_height,
        bottom_radius,
        top_radius);
}

// The LUT carries its producer's radiance domain. Capture uses a dedicated
// unit-exposure LUT; ordinary views use their published pre-exposed LUT.
static float4 SampleSkyViewRadiance(EnvironmentStaticData environment,
    EnvironmentViewData view, float3 direction)
{
    if (environment.atmosphere.enabled == 0u
        || !BX_IN_TEXTURES(environment.atmosphere.sky_view_lut_slot)) return 0.0.xxxx;
    float3 local_direction;
    float height, bottom, top;
    float2 uv = ResolveSkyViewUv(environment, view, direction, local_direction, height, bottom, top);
    Texture2D<float4> sky = ResourceDescriptorHeap[environment.atmosphere.sky_view_lut_slot];
    SamplerState linear_clamp = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
    float4 sample_value = sky.SampleLevel(linear_clamp, uv, 0.0);
    sample_value.rgb *= view.sky_luminance_factor_height_fog_contribution.xyz;
    return sample_value;
}

#endif

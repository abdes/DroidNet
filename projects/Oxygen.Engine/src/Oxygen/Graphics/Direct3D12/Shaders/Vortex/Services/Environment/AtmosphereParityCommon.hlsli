//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREPARITYCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREPARITYCOMMON_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/EnvironmentViewData.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Math.hlsli"

static const float kVortexPi = 3.14159265359f;

static float3 VortexSafeNormalize(float3 value)
{
    const float length_sq = dot(value, value);
    if (length_sq <= 1.0e-8f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    return value * rsqrt(length_sq);
}

static GpuSkyAtmosphereParams BuildVortexAtmosphereParams(
    float planet_radius_m,
    float atmosphere_height_m,
    float multi_scattering_factor,
    float rayleigh_scale_height_m,
    float mie_scale_height_m,
    float mie_anisotropy,
    float3 ground_albedo_rgb,
    float3 rayleigh_scattering_rgb,
    float3 mie_scattering_rgb,
    float3 mie_absorption_rgb,
    float3 ozone_absorption_rgb,
    AtmosphereDensityProfile ozone_density_profile,
    uint transmittance_lut_srv,
    float transmittance_width,
    float transmittance_height,
    uint multi_scattering_lut_srv)
{
    GpuSkyAtmosphereParams atmosphere = (GpuSkyAtmosphereParams)0;
    atmosphere.planet_radius_m = planet_radius_m;
    atmosphere.atmosphere_height_m = atmosphere_height_m;
    atmosphere.multi_scattering_factor = multi_scattering_factor;
    atmosphere.aerial_perspective_distance_scale = 1.0f;
    atmosphere.ground_albedo_rgb = ground_albedo_rgb;
    atmosphere.sun_disk_angular_radius_radians = 0.0f;
    atmosphere.rayleigh_scattering_rgb = rayleigh_scattering_rgb;
    atmosphere.rayleigh_scale_height_m = rayleigh_scale_height_m;
    atmosphere.mie_scattering_rgb = mie_scattering_rgb;
    atmosphere.mie_scale_height_m = mie_scale_height_m;
    atmosphere.mie_extinction_rgb = mie_scattering_rgb + mie_absorption_rgb;
    atmosphere.mie_g = mie_anisotropy;
    atmosphere.absorption_rgb = ozone_absorption_rgb;
    atmosphere.absorption_density = ozone_density_profile;
    atmosphere.sun_disk_enabled = 0u;
    atmosphere.enabled = 1u;
    atmosphere.transmittance_lut_slot = transmittance_lut_srv;
    atmosphere.multi_scat_lut_slot = multi_scattering_lut_srv;
    atmosphere.transmittance_lut_width = transmittance_width;
    atmosphere.transmittance_lut_height = transmittance_height;
    return atmosphere;
}

static float3x3 BuildSkyLocalReferential(float3 planet_up_ws)
{
    const float3 local_z = VortexSafeNormalize(planet_up_ws);

    float3 reference_y = float3(0.0f, 1.0f, 0.0f);
    if (abs(dot(reference_y, local_z)) > 0.999f)
    {
        reference_y = float3(1.0f, 0.0f, 0.0f);
    }

    const float3 local_x = VortexSafeNormalize(cross(reference_y, local_z));
    const float3 local_y = VortexSafeNormalize(cross(local_z, local_x));
    return float3x3(local_x, local_y, local_z);
}

static float3 ApplySkyViewLutReferential(EnvironmentViewData view_data, float3 world_direction)
{
    return float3(
        dot(view_data.sky_view_lut_referential_row0.xyz, world_direction),
        dot(view_data.sky_view_lut_referential_row1.xyz, world_direction),
        dot(view_data.sky_view_lut_referential_row2.xyz, world_direction));
}

static float3 GetSkyTranslatedCameraPlanetPos(EnvironmentViewData view_data)
{
    return view_data.sky_camera_translated_world_origin_pad.xyz
        - view_data.sky_planet_translated_world_center_and_view_height.xyz;
}

static bool MoveToTopAtmosphere(
    inout float3 world_position,
    float3 world_direction,
    float top_radius)
{
    const float view_height = length(world_position);
    if (view_height <= top_radius)
    {
        return true;
    }

    const float top_intersection = RaySphereIntersectNearest(
        world_position,
        world_direction,
        top_radius);
    if (top_intersection < 0.0f)
    {
        return false;
    }

    world_position += world_direction * top_intersection;
    return true;
}

static void UvToSkyViewLutParams(
    out float3 view_direction,
    float view_height,
    float planet_radius,
    float2 uv)
{
    const float horizon_distance = sqrt(max(
        view_height * view_height - planet_radius * planet_radius,
        0.0f));
    const float cos_beta = horizon_distance / max(view_height, 1.0f);
    const float beta = acos(cos_beta);
    const float zenith_horizon_angle = kVortexPi - beta;

    float view_zenith_angle = 0.0f;
    if (uv.y < 0.5f)
    {
        float coord = 2.0f * uv.y;
        coord = 1.0f - coord;
        coord *= coord;
        coord = 1.0f - coord;
        view_zenith_angle = zenith_horizon_angle * coord;
    }
    else
    {
        float coord = uv.y * 2.0f - 1.0f;
        coord *= coord;
        view_zenith_angle = zenith_horizon_angle + beta * coord;
    }

    const float cos_view_zenith = cos(view_zenith_angle);
    const float sin_view_zenith = sqrt(max(1.0f - cos_view_zenith * cos_view_zenith, 0.0f));
    const float longitude = uv.x * 2.0f * kVortexPi;
    const float cos_longitude = cos(longitude);
    const float sin_longitude = sin(longitude);

    view_direction = float3(
        sin_view_zenith * cos_longitude,
        sin_view_zenith * sin_longitude,
        cos_view_zenith);
}

static float2 SkyViewLutParamsToUv(
    bool intersect_ground,
    float view_zenith_cos_angle,
    float3 view_direction,
    float view_height,
    float bottom_radius,
    float2 lut_inv_size)
{
    const float horizon_distance = sqrt(max(
        view_height * view_height - bottom_radius * bottom_radius,
        0.0f));
    const float cos_beta = horizon_distance / max(view_height, 1.0f);
    const float beta = acos(cos_beta);
    const float zenith_horizon_angle = kVortexPi - beta;
    const float view_zenith_angle = acos(clamp(view_zenith_cos_angle, -1.0f, 1.0f));

    float2 uv;
    if (!intersect_ground)
    {
        float coord = view_zenith_angle / max(zenith_horizon_angle, 1.0e-4f);
        coord = 1.0f - coord;
        coord = sqrt(max(coord, 0.0f));
        coord = 1.0f - coord;
        uv.y = coord * 0.5f;
    }
    else
    {
        float coord = (view_zenith_angle - zenith_horizon_angle) / max(beta, 1.0e-4f);
        coord = sqrt(max(coord, 0.0f));
        uv.y = coord * 0.5f + 0.5f;
    }

    uv.x = (atan2(-view_direction.y, -view_direction.x) + kVortexPi)
        / (2.0f * kVortexPi);
    uv = saturate(uv);
    uv = (uv * (1.0f - lut_inv_size)) + lut_inv_size * 0.5f;
    return uv;
}

static float4 SampleVortexCameraAerialPerspective(
    uint camera_aerial_srv,
    float2 screen_uv,
    float view_distance_m,
    float start_depth_m,
    float depth_resolution,
    float depth_resolution_inv,
    float depth_slice_length_km,
    float depth_slice_length_km_inv)
{
    if (camera_aerial_srv == K_INVALID_BINDLESS_INDEX)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float t_depth_km = max(0.0f, (view_distance_m - start_depth_m) / 1000.0f);
    const float linear_slice = t_depth_km * depth_slice_length_km_inv;
    const float linear_w = linear_slice * depth_resolution_inv;
    const float non_linear_w = sqrt(saturate(linear_w));
    const float non_linear_slice = non_linear_w * depth_resolution;
    const float half_slice_depth = 0.70710678118654752440f;

    float weight = 1.0f;
    if (non_linear_slice < half_slice_depth)
    {
        weight = saturate(non_linear_slice * non_linear_slice * 2.0f);
    }
    weight *= saturate(t_depth_km * 100000.0f);

    Texture3D<float4> aerial_volume = ResourceDescriptorHeap[camera_aerial_srv];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    float4 aerial = aerial_volume.SampleLevel(
        linear_sampler,
        float3(screen_uv, non_linear_w),
        0.0f);
    aerial.rgb *= weight;
    aerial.a = 1.0f - (weight * (1.0f - aerial.a));
    return aerial;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREPARITYCOMMON_HLSLI

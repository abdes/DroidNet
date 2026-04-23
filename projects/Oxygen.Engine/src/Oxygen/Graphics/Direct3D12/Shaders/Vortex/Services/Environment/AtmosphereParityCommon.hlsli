//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREPARITYCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREPARITYCOMMON_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Contracts/EnvironmentStaticData.hlsli"
#include "Vortex/Contracts/EnvironmentViewData.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Math.hlsli"
#include "Vortex/Services/Environment/AtmosphereConstants.hlsli"

static const float kVortexPi = 3.14159265359f;

static float2 VortexFromUnitToSubUvs(float2 uv, float2 size, float2 inv_size)
{
    return (uv + 0.5f * inv_size) * (size / max(size + 1.0f.xx, 1.0f.xx));
}

static float2 VortexFromSubUvsToUnit(float2 uv, float2 size, float2 inv_size)
{
    return (uv - 0.5f * inv_size) * (size / max(size - 1.0f.xx, 1.0f.xx));
}

static float3 VortexSafeNormalize(float3 value)
{
    const float length_sq = dot(value, value);
    if (length_sq <= 1.0e-8f)
    {
        return float3(0.0f, 0.0f, 1.0f);
    }
    return value * rsqrt(length_sq);
}

static float AtmosphereMetersToSkyUnit(float meters)
{
    return meters * 1.0e-3f;
}

static float3 AtmosphereMetersToSkyUnit(float3 meters)
{
    return meters * 1.0e-3f;
}

static GpuSkyAtmosphereParams BuildVortexAtmosphereParams(
    float planet_radius_km,
    float atmosphere_height_km,
    float multi_scattering_factor,
    float aerial_perspective_distance_scale,
    float rayleigh_scale_height_km,
    float mie_scale_height_km,
    float mie_anisotropy,
    float3 ground_albedo_rgb,
    float sun_disk_angular_radius_radians,
    float3 rayleigh_scattering_per_km_rgb,
    float3 mie_scattering_per_km_rgb,
    float3 mie_absorption_per_km_rgb,
    float3 ozone_absorption_per_km_rgb,
    AtmosphereDensityProfile ozone_density_profile,
    uint sun_disk_enabled,
    uint transmittance_lut_srv,
    float transmittance_width,
    float transmittance_height,
    uint multi_scattering_lut_srv)
{
    GpuSkyAtmosphereParams atmosphere = (GpuSkyAtmosphereParams)0;
    atmosphere.planet_radius_km = planet_radius_km;
    atmosphere.atmosphere_height_km = atmosphere_height_km;
    atmosphere.multi_scattering_factor = multi_scattering_factor;
    atmosphere.aerial_perspective_distance_scale = aerial_perspective_distance_scale;
    atmosphere.ground_albedo_rgb = ground_albedo_rgb;
    atmosphere.sun_disk_angular_radius_radians = sun_disk_angular_radius_radians;
    atmosphere.sun_disk_luminance_scale_rgb = 1.0f.xxx;
    atmosphere.rayleigh_scattering_per_km_rgb = rayleigh_scattering_per_km_rgb;
    atmosphere.rayleigh_scale_height_km = rayleigh_scale_height_km;
    atmosphere.mie_scattering_per_km_rgb = mie_scattering_per_km_rgb;
    atmosphere.mie_scale_height_km = mie_scale_height_km;
    atmosphere.mie_extinction_per_km_rgb
        = mie_scattering_per_km_rgb + mie_absorption_per_km_rgb;
    atmosphere.mie_g = mie_anisotropy;
    atmosphere.absorption_per_km_rgb = ozone_absorption_per_km_rgb;
    atmosphere.absorption_density = ozone_density_profile;
    atmosphere.sun_disk_enabled = sun_disk_enabled;
    atmosphere.enabled = 1u;
    atmosphere.transmittance_lut_slot = transmittance_lut_srv;
    atmosphere.multi_scat_lut_slot = multi_scattering_lut_srv;
    atmosphere.transmittance_lut_width = transmittance_width;
    atmosphere.transmittance_lut_height = transmittance_height;
    return atmosphere;
}

//! Projects a world-space direction into the shared sky-view local frame.
//!
//! Contract with C++ publisher:
//! - row0 = local +X = forward
//! - row1 = local +Y = right
//! - row2 = local +Z = up
//!
//! This basis is shared by the sky-view LUT producer and consumer. Keep the
//! handedness consistent with Oxygen world space (right-handed, Z-up,
//! -Y-forward). Do not reinterpret row1 as "left" here.
static float3 ApplySkyViewLutReferential(EnvironmentViewData view_data, float3 world_direction)
{
    return float3(
        dot(view_data.sky_view_lut_referential_row0.xyz, world_direction),
        dot(view_data.sky_view_lut_referential_row1.xyz, world_direction),
        dot(view_data.sky_view_lut_referential_row2.xyz, world_direction));
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

    // Mirrors UE5.7 SkyAtmosphere.usf::MoveToTopAtmosphere. The small inward
    // offset after the top-shell intersection avoids precision edge cases when
    // later sphere tests run exactly on the boundary.
    const float3 up_vector = world_position / view_height;
    const float3 up_offset = up_vector * -PLANET_RADIUS_OFFSET;
    world_position = world_position + world_direction * top_intersection + up_offset;
    return true;
}

static void UvToSkyViewLutParams(
    out float3 view_direction,
    float view_height,
    float planet_radius,
    float2 uv,
    float2 lut_size,
    float2 lut_inv_size)
{
    // Mirrors UE5.7 SkyAtmosphere.usf::UvToSkyViewLutParams (line 217):
    //   UV = FromSubUvsToUnit(UV, SkyViewLutSizeAndInvSize);
    // applied internally so callers pass the raw (PixPos+0.5)*InvSize UV,
    // matching RenderSkyViewLutCS (SkyAtmosphere.usf:1328-1345). This keeps
    // gen and sample symmetric inverses, since the sample-side
    // SkyViewLutParamsToUv ends with FromUnitToSubUvs
    // (SkyAtmosphereCommon.ush:224).
    uv = VortexFromSubUvsToUnit(uv, lut_size, lut_inv_size);

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
    float2 lut_size,
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

    // U uses the atmosphere sky-view azimuth convention around the shared
    // local frame. The sign here belongs to the LUT parameterization itself;
    // it is independent from the handedness of the referential rows above.
    uv.x = (atan2(-view_direction.y, -view_direction.x) + kVortexPi)
        / (2.0f * kVortexPi);
    uv = saturate(uv);
    return VortexFromUnitToSubUvs(uv, lut_size, lut_inv_size);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREPARITYCOMMON_HLSLI

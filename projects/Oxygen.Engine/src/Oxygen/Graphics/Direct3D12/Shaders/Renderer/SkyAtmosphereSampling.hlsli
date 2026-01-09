//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Sky Atmosphere LUT Sampling Helpers
//!
//! Provides functions to sample precomputed transmittance and sky-view LUTs
//! for physically-based atmospheric scattering.
//!
//! === LUT UV Parameterizations ===
//! Transmittance LUT:
//!   u = (cos_zenith + 0.15) / 1.15  (avoids horizon singularity)
//!   v = sqrt(altitude / atmosphere_height)
//!
//! Sky-View LUT:
//!   u = azimuth / (2 * PI)
//!   v = (cos_zenith + 1) / 2

#ifndef SKY_ATMOSPHERE_SAMPLING_HLSLI
#define SKY_ATMOSPHERE_SAMPLING_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"

// Mathematical constants
static const float SKY_PI = 3.14159265359;
static const float SKY_TWO_PI = 6.28318530718;

//! Computes transmittance LUT UV from altitude and cos_zenith.
//!
//! @param cos_zenith Cosine of zenith angle (view direction dot up).
//! @param altitude_m Height above planet surface in meters.
//! @param atmosphere_height_m Total atmosphere thickness in meters.
//! @return UV coordinates for transmittance LUT sampling.
float2 GetTransmittanceLutUv(float cos_zenith, float altitude_m, float atmosphere_height_m)
{
    // Offset avoids singularity near horizon (cos_zenith = 0).
    float u = saturate((cos_zenith + 0.15) / 1.15);

    // Square-root parameterization gives better sampling at low altitudes.
    float v = saturate(sqrt(max(0.0, altitude_m / atmosphere_height_m)));

    return float2(u, v);
}

//! Samples the transmittance LUT.
//!
//! @param lut_slot Bindless SRV index for the transmittance LUT.
//! @param lut_width LUT texture width.
//! @param lut_height LUT texture height.
//! @param cos_zenith Cosine of zenith angle.
//! @param altitude_m Height above planet surface in meters.
//! @param atmosphere_height_m Total atmosphere thickness in meters.
//! @return RGB transmittance.
float3 SampleTransmittanceOpticalDepthLut(
    uint lut_slot,
    float lut_width,
    float lut_height,
    float cos_zenith,
    float altitude_m,
    float atmosphere_height_m)
{
    if (lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        // No LUT available, return zero optical depth.
        return float3(0.0, 0.0, 0.0);
    }

    float2 uv = GetTransmittanceLutUv(cos_zenith, altitude_m, atmosphere_height_m);

    // Apply half-texel offset for proper filtering.
    uv = uv * float2((lut_width - 1.0) / lut_width, (lut_height - 1.0) / lut_height);
    uv += float2(0.5 / lut_width, 0.5 / lut_height);

    // RGB stores optical depth integrals for Rayleigh/Mie/Absorption.
    Texture2D<float4> lut = ResourceDescriptorHeap[lut_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    return lut.SampleLevel(linear_sampler, uv, 0).rgb;
}

float3 TransmittanceFromOpticalDepth(float3 optical_depth, GpuSkyAtmosphereParams atmo)
{
    float3 beta_rayleigh = atmo.rayleigh_scattering_rgb;
    float3 beta_mie_ext = atmo.mie_scattering_rgb / 0.9;
    float3 beta_abs = atmo.absorption_rgb;

    float3 tau = beta_rayleigh * optical_depth.x
               + beta_mie_ext * optical_depth.y
               + beta_abs * optical_depth.z;
    return exp(-tau);
}

float3 SampleTransmittanceLut(
    GpuSkyAtmosphereParams atmo,
    uint lut_slot,
    float lut_width,
    float lut_height,
    float cos_zenith,
    float altitude_m,
    float atmosphere_height_m)
{
    if (lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        return float3(1.0, 1.0, 1.0);
    }

    float3 od = SampleTransmittanceOpticalDepthLut(
        lut_slot,
        lut_width,
        lut_height,
        cos_zenith,
        altitude_m,
        atmosphere_height_m);

    return TransmittanceFromOpticalDepth(od, atmo);
}

//! Computes sky-view LUT UV from view direction with sun-relative parameterization.
//!
//! This must match the UvToViewDirection function in SkyViewLut_CS.hlsl exactly.
//! The LUT is parameterized with sun at azimuth=0 (U=0.5), so we compute the
//! view direction's azimuth relative to the sun's azimuth.
//!
//! @param view_dir Normalized world-space view direction.
//! @param sun_dir Normalized world-space sun direction.
//! @param planet_radius Planet radius in meters.
//! @param camera_altitude Camera altitude above surface in meters.
//! @return UV coordinates for sky-view LUT sampling.
float2 GetSkyViewLutUv(float3 view_dir, float3 sun_dir, float planet_radius, float camera_altitude)
{
    // cos_zenith from Z component (Z is up).
    float cos_zenith = view_dir.z;
    float sin_zenith = sqrt(max(0.0, 1.0 - cos_zenith * cos_zenith));

    // Compute view-relative azimuth in the XY plane (Z-up). Note that azimuth is
    // ill-defined at zenith (sin_zenith -> 0), so we later blend U toward 0.5
    // smoothly to avoid any discontinuity.
    float view_azimuth = atan2(view_dir.y, view_dir.x);
    float sun_azimuth = atan2(sun_dir.y, sun_dir.x);
    float relative_azimuth = view_azimuth - sun_azimuth;

    // Normalize to [0, 2π)
    if (relative_azimuth < 0.0)
        relative_azimuth += SKY_TWO_PI;
    if (relative_azimuth >= SKY_TWO_PI)
        relative_azimuth -= SKY_TWO_PI;

    // Compute horizon angle for this altitude.
    float r = planet_radius + camera_altitude;
    float rho = planet_radius / r;
    float cos_horizon = -sqrt(max(0.0, 1.0 - rho * rho));

    // Inverse of the non-linear V mapping used in LUT generation.
    float v;
    if (cos_zenith < cos_horizon)
    {
        // Below horizon: map cos_zenith in [-1, cos_horizon] to V in [0, 0.5]
        float t = (cos_zenith - (-1.0)) / (cos_horizon - (-1.0));
        v = t * 0.5;
    }
    else
    {
        // Above horizon: map cos_zenith in [cos_horizon, 1] to V in [0.5, 1]
        float t = (cos_zenith - cos_horizon) / (1.0 - cos_horizon);
        v = 0.5 + t * 0.5;
    }

    // U=0.5 corresponds to looking at sun (relative_azimuth=0).
    // Map relative_azimuth from [0, 2π] to U in [0.5, 1.5] then wrap to [0, 1].
    float u = relative_azimuth / SKY_TWO_PI + 0.5;
    if (u >= 1.0)
        u -= 1.0;

    return float2(u, v);
}

static inline float2 ApplyHalfTexelOffset(float2 uv, float lut_width, float lut_height)
{
    uv = uv * float2((lut_width - 1.0) / lut_width, (lut_height - 1.0) / lut_height);
    uv += float2(0.5 / lut_width, 0.5 / lut_height);
    return uv;
}

//! Samples the sky-view LUT.
//!
//! @param lut_slot Bindless SRV index for the sky-view LUT.
//! @param lut_width LUT texture width.
//! @param lut_height LUT texture height.
//! @param view_dir Normalized world-space view direction.
//! @param sun_dir Normalized world-space sun direction.
//! @param planet_radius Planet radius in meters.
//! @param camera_altitude Camera altitude above surface in meters.
//! @return float4(inscattered_radiance.rgb, transmittance).
float4 SampleSkyViewLut(
    uint lut_slot,
    float lut_width,
    float lut_height,
    float3 view_dir,
    float3 sun_dir,
    float planet_radius,
    float camera_altitude)
{
    if (lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        // No LUT available, return zero inscatter, full transmittance.
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float2 uv_base = GetSkyViewLutUv(view_dir, sun_dir, planet_radius, camera_altitude);
    float2 uv = ApplyHalfTexelOffset(uv_base, lut_width, lut_height);

    Texture2D<float4> lut = ResourceDescriptorHeap[lut_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    float4 base_sample = lut.SampleLevel(linear_sampler, uv, 0);

    // Near zenith, azimuth becomes ill-defined (view_dir.xy ~ 0). Even tiny
    // numerical noise in view_dir can swing atan2() a lot, which makes the LUT
    // lookup flicker between different U columns. Instead of snapping U (which
    // creates a seam), we azimuth-average a few wrapped samples only near zenith.
    const float cos_zenith = view_dir.z;
    const float sin_zenith = sqrt(max(0.0, 1.0 - cos_zenith * cos_zenith));
    const float kZenithFilterEnd = 0.05; // ~2.9 degrees
    const float zenith_weight = saturate(1.0 - (sin_zenith / kZenithFilterEnd));

    if (zenith_weight > 0.0)
    {
        float4 acc = float4(0.0, 0.0, 0.0, 0.0);
        acc += lut.SampleLevel(linear_sampler,
            ApplyHalfTexelOffset(float2(frac(uv_base.x + 0.00), uv_base.y), lut_width, lut_height), 0);
        acc += lut.SampleLevel(linear_sampler,
            ApplyHalfTexelOffset(float2(frac(uv_base.x + 0.25), uv_base.y), lut_width, lut_height), 0);
        acc += lut.SampleLevel(linear_sampler,
            ApplyHalfTexelOffset(float2(frac(uv_base.x + 0.50), uv_base.y), lut_width, lut_height), 0);
        acc += lut.SampleLevel(linear_sampler,
            ApplyHalfTexelOffset(float2(frac(uv_base.x + 0.75), uv_base.y), lut_width, lut_height), 0);

        float4 zenith_filtered = acc * 0.25;
        return lerp(base_sample, zenith_filtered, zenith_weight);
    }

    return base_sample;
}

//! Computes the sun disk contribution with proper horizon clipping.
//!
//! The sun disk is rendered based on where the view ray intersects the sun.
//! When the sun is partially below the horizon, only the portion of the disk
//! visible above the horizon is rendered - the view direction must be above
//! the horizon to see any sun disk.
//!
//! @param view_dir Normalized world-space view direction.
//! @param sun_dir Normalized direction toward the sun.
//! @param angular_radius_radians Angular radius of the sun disk.
//! @param sun_luminance Sun luminance (color * intensity).
//! @param planet_radius Planet radius in meters.
//! @param camera_altitude Camera altitude above surface in meters.
//! @return Sun disk radiance contribution.
float3 ComputeSunDisk(
    float3 view_dir,
    float3 sun_dir,
    float angular_radius_radians,
    float3 sun_luminance,
    float planet_radius,
    float camera_altitude)
{
    // Compute the geometric horizon angle from camera altitude.
    float r = planet_radius + camera_altitude;
    float rho = planet_radius / r;
    float cos_horizon = -sqrt(max(0.0, 1.0 - rho * rho));

    // View direction must be above horizon to see anything
    float view_cos_zenith = view_dir.z;
    if (view_cos_zenith < cos_horizon)
    {
        return float3(0.0, 0.0, 0.0);
    }

    // Cosine of angle between view and sun direction.
    float cos_angle = dot(view_dir, sun_dir);
    float cos_sun_radius = cos(angular_radius_radians);

    // Early out if not looking at sun disk
    if (cos_angle < cos_sun_radius - 0.01)
    {
        return float3(0.0, 0.0, 0.0);
    }

    // Sun disk with soft edge (anti-aliasing).
    float edge_softness = 0.002;
    float disk_factor = smoothstep(
        cos_sun_radius - edge_softness,
        cos_sun_radius + edge_softness,
        cos_angle);

    // Now handle partial visibility: if view is above horizon but looking
    // toward a sun that's partially below, we need to fade based on how
    // much of the sun disk is above the horizon line.
    //
    // The "effective" sun position for this pixel is where the view ray
    // would hit the sun disk. Since we're looking at the disk, the relevant
    // question is: does the view direction point above the horizon?
    // We already checked view_cos_zenith >= cos_horizon above.
    //
    // Additionally, fade out the disk as the sun center approaches/crosses
    // the horizon to avoid a harsh cut.
    float sun_cos_zenith = sun_dir.z;

    // How far is sun center above horizon? (positive = above, negative = below)
    float sun_above_horizon = sun_cos_zenith - cos_horizon;

    // Fade based on sun center position relative to horizon.
    // Full brightness when sun center is 1 angular radius above horizon.
    // Zero when sun center is 1 angular radius below horizon.
    float horizon_fade = saturate((sun_above_horizon + angular_radius_radians)
                                   / (2.0 * angular_radius_radians));

    return sun_luminance * disk_factor * horizon_fade;
}

//! Computes full atmospheric sky color from LUTs.
//!
//! @param atmo Atmosphere parameters from EnvironmentStaticData.
//! @param view_dir Normalized world-space view direction.
//! @param sun_dir Normalized direction toward the sun.
//! @param sun_luminance Sun luminance (color * intensity).
//! @param planet_radius Planet radius in meters.
//! @param camera_altitude Camera altitude above surface in meters.
//! @return Final sky radiance.
float3 ComputeAtmosphereSkyColor(
    GpuSkyAtmosphereParams atmo,
    float3 view_dir,
    float3 sun_dir,
    float3 sun_luminance,
    float planet_radius,
    float camera_altitude)
{
    // Sample sky-view LUT for inscattered radiance.
    // The LUT already includes twilight attenuation baked in during generation.
    float4 sky_sample = SampleSkyViewLut(
        atmo.sky_view_lut_slot,
        atmo.sky_view_lut_width,
        atmo.sky_view_lut_height,
        view_dir,
        sun_dir,
        planet_radius,
        camera_altitude);

    float3 inscatter = sky_sample.rgb;
    float transmittance = sky_sample.a;

    // Sky-view LUT stores inscatter per unit sun radiance.
    inscatter *= sun_luminance;

    // Optionally add sun disk.
    float3 sun_contribution = float3(0.0, 0.0, 0.0);
    if (atmo.sun_disk_enabled)
    {
        // Sample transmittance toward sun to attenuate sun disk.
        float cos_sun_zenith = sun_dir.z; // Z is up

        float3 sun_transmittance = SampleTransmittanceLut(
            atmo,
            atmo.transmittance_lut_slot,
            atmo.transmittance_lut_width,
            atmo.transmittance_lut_height,
            cos_sun_zenith,
            camera_altitude,
            atmo.atmosphere_height_m);

        float3 attenuated_sun = sun_luminance * sun_transmittance;

        sun_contribution = ComputeSunDisk(
            view_dir,
            sun_dir,
            atmo.sun_disk_angular_radius_radians,
            attenuated_sun,
            planet_radius,
            camera_altitude);

        // Sun disk is seen through the atmosphere transmittance along view.
        sun_contribution *= transmittance;
    }

    return inscatter + sun_contribution;
}

#endif // SKY_ATMOSPHERE_SAMPLING_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Sky-View LUT Compute Shader
//!
//! Precomputes inscattered radiance for all view directions at a given altitude.
//! Output: RGBA16F texture where:
//!   RGB = inscattered radiance (per unit sun radiance)
//!   A   = view-path transmittance (luminance proxy)
//!
//! UV Parameterization (horizon-aware):
//!   u = azimuth / 2π (angle around planet up vector)
//!   v = horizon-aware zenith mapping:
//!       v < 0.5: below horizon (ground), maps to cos_zenith in [-1, cos_horizon]
//!       v > 0.5: above horizon (sky), maps to cos_zenith in [cos_horizon, 1]
//!   where cos_horizon = -sqrt(1 - (R/(R+h))²), R=planet radius, h=altitude
//!
//! This parameterization places the horizon at v=0.5, providing better LUT
//! resolution near the horizon where atmospheric color changes are most visible.
//!
//! === Bindless Discipline ===
//! - All resources accessed via SM 6.6 descriptor heaps
//! - SceneConstants at b1, RootConstants at b2, EnvironmentDynamicData at b3

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/SceneConstants.hlsli"

// Root constants (b2, space0)
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Pass constants for sky-view LUT generation
struct SkyViewLutPassConstants
{
    uint output_uav_index;          // UAV index for output RWTexture2D<float4>
    uint transmittance_srv_index;   // SRV index for transmittance LUT
    uint output_width;              // LUT width
    uint output_height;             // LUT height

    uint transmittance_width;       // Transmittance LUT width
    uint transmittance_height;      // Transmittance LUT height
    float camera_altitude_m;        // Camera altitude above ground
    float sun_cos_zenith;           // Cosine of sun zenith angle (sun_dir.z)
};

// Thread group size: 8x8 threads per group
#define THREAD_GROUP_SIZE_X 8
#define THREAD_GROUP_SIZE_Y 8

// Number of raymarch samples - higher values reduce banding but cost more.
static const uint MIN_SCATTERING_SAMPLES = 96;
static const uint MAX_SCATTERING_SAMPLES = 256;

// Mathematical constants
static const float PI = 3.14159265359;
static const float TWO_PI = 6.28318530718;



//! Converts UV coordinates to view direction with sun-relative parameterization.
//!
//! This uses a sun-relative azimuth parameterization where U represents the
//! azimuth relative to the sun direction (U=0.5 is the sun azimuth). This
//! allows the LUT to remain valid as the sun rotates around the zenith axis,
//! only requiring regeneration when sun elevation changes.
//!
//! The V coordinate uses horizon-aware mapping with V=0.5 at the horizon.
//!
//! @param uv Normalized texture coordinates [0, 1].
//! @param planet_radius Planet radius in meters.
//! @param camera_altitude Camera altitude above surface in meters.
//! @param sun_cos_zenith Cosine of sun zenith angle (sun_dir.z).
//! @return Normalized view direction in sun-relative space (Z-up, sun at +X horizon).
float3 UvToViewDirection(float2 uv, float planet_radius, float camera_altitude, float sun_cos_zenith)
{
    // u = relative azimuth / 2π, where 0.5 = sun direction
    // Shift so U=0.5 corresponds to azimuth=0 (sun direction)
    float relative_azimuth = (uv.x - 0.5) * TWO_PI;

    // Compute horizon angle for this altitude.
    float r = planet_radius + camera_altitude;
    float rho = planet_radius / r;
    float cos_horizon = -sqrt(max(0.0, 1.0 - rho * rho));

    // Non-linear V mapping: V=0.5 is horizon.
    float cos_zenith;
    if (uv.y < 0.5)
    {
        float t = uv.y * 2.0;
        cos_zenith = lerp(-1.0, cos_horizon, t);
    }
    else
    {
        float t = (uv.y - 0.5) * 2.0;
        cos_zenith = lerp(cos_horizon, 1.0, t);
    }

    float sin_zenith = sqrt(max(0.0, 1.0 - cos_zenith * cos_zenith));

    // View direction in sun-relative coordinates:
    // X-axis points toward sun's horizontal projection
    // Z-axis is up (zenith)
    return float3(
        sin_zenith * cos(relative_azimuth),
        sin_zenith * sin(relative_azimuth),
        cos_zenith);
}

//! Computes atmospheric density at a given altitude.
float GetAtmosphereDensity(float altitude, float scale_height)
{
    return exp(-altitude / scale_height);
}

//! Ray-sphere intersection.
float RaySphereIntersect(float3 origin, float3 dir, float radius)
{
    float b = dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float discriminant = b * b - c;

    if (discriminant < 0.0)
    {
        return -1.0;
    }

    float sqrt_disc = sqrt(discriminant);
    float t0 = -b - sqrt_disc;
    float t1 = -b + sqrt_disc;

    if (t0 > 0.0)
    {
        return t0;
    }
    if (t1 > 0.0)
    {
        return t1;
    }
    return -1.0;
}

//! Samples the transmittance LUT.
//!
//! @param altitude Altitude above ground in meters.
//! @param cos_zenith Cosine of zenith angle.
//! @param atmo Atmosphere parameters.
//! @param transmittance_lut Transmittance LUT texture.
//! @param lut_size LUT dimensions.
//! @return (Rayleigh transmittance, Mie transmittance).
float3 SampleTransmittanceLutOpticalDepth(
    float altitude,
    float cos_zenith,
    GpuSkyAtmosphereParams atmo,
    Texture2D<float4> transmittance_lut,
    SamplerState linear_sampler,
    uint2 lut_size)
{
    // Convert to UV
    float u = (cos_zenith + 0.15) / 1.15;
    float v = sqrt(altitude / atmo.atmosphere_height_m);

    // Clamp to valid range
    u = clamp(u, 0.0, 1.0);
    v = clamp(v, 0.0, 1.0);

    // Sample with bilinear filtering.
    // RGB stores optical depth integrals for Rayleigh/Mie/Absorption.
    return transmittance_lut.SampleLevel(linear_sampler, float2(u, v), 0).rgb;
}

//! Converts optical depth (Rayleigh/Mie/Absorption) into RGB transmittance.
float3 TransmittanceFromOpticalDepth(float3 optical_depth, GpuSkyAtmosphereParams atmo)
{
    // Extinction = Rayleigh scattering + Mie extinction + absorption.
    // For Mie, we approximate extinction by assuming a constant single-scattering
    // albedo (w ~= 0.9). This keeps tuning stable while remaining plausible.
    float3 beta_rayleigh = atmo.rayleigh_scattering_rgb;
    float3 beta_mie_ext = atmo.mie_scattering_rgb / 0.9;
    float3 beta_abs = atmo.absorption_rgb;

    float3 tau = beta_rayleigh * optical_depth.x
               + beta_mie_ext * optical_depth.y
               + beta_abs * optical_depth.z;

    return exp(-tau);
}

//! Simple ozone-like absorption density profile.
float GetAbsorptionDensity(float altitude, float absorption_center_m)
{
    altitude = max(altitude, 0.0);

    float center = max(1.0, absorption_center_m);
    float width = max(1000.0, center * 0.6);
    float t = 1.0 - abs(altitude - center) / width;
    return saturate(t);
}

float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

//! Rayleigh phase function.
float RayleighPhase(float cos_theta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

//! Henyey-Greenstein phase function for Mie scattering.
float HenyeyGreensteinPhase(float cos_theta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 / (4.0 * PI)) * (1.0 - g2) / (denom * sqrt(denom));
}

//! Computes single-scattering inscatter along a view ray.
//!
//! @param origin Ray origin (position relative to planet center).
//! @param view_dir View direction (normalized).
//! @param sun_dir Sun direction (toward sun, normalized).
//! @param ray_length Distance to raymarch.
//! @param hits_ground True if the ray terminates at ground level.
//! @param atmo Atmosphere parameters.
//! @param transmittance_lut Optical-depth LUT (Rayleigh/Mie/Abs).
//! @param linear_sampler Linear sampler.
//! @param lut_size Transmittance LUT size.
//! @return (inscattered_radiance.rgb, total_transmittance).
float4 ComputeSingleScattering(
    float3 origin,
    float3 view_dir,
    float3 sun_dir,
    float ray_length,
    bool hits_ground,
    GpuSkyAtmosphereParams atmo,
    Texture2D<float4> transmittance_lut,
    SamplerState linear_sampler,
    uint2 lut_size)
{
    float3 inscatter = float3(0.0, 0.0, 0.0);

    // Adaptive step count: more steps near the horizon/twilight.
    // Oxygen is strictly Z-up.
    const float horizon_factor_view = saturate(1.0 - abs(view_dir.z));
    const float twilight_factor = saturate(1.0 - abs(sun_dir.z));
    const float horizon_factor = max(horizon_factor_view, twilight_factor);
    const uint num_steps = (uint)lerp((float)MIN_SCATTERING_SAMPLES,
                                     (float)MAX_SCATTERING_SAMPLES,
                                     horizon_factor);

    float step_size = ray_length / float(max(1u, num_steps));

    // Phase function evaluation
    float cos_theta = dot(view_dir, sun_dir);
    float rayleigh_phase = RayleighPhase(cos_theta);

    // Sun elevation: sun_dir.z = sin(elevation) for Z-up coordinate system
    // Positive = above horizon, negative = below horizon
    float sun_sin_elevation = sun_dir.z;

    // Horizon-linked Mie anisotropy:
    // The Mie phase function creates an intense forward-scattering aureole that
    // becomes problematic at low sun angles where long atmospheric paths amplify
    // the effect. We progressively reduce anisotropy as sun approaches horizon.
    //
    // - Above 30° elevation: full user-specified mie_g
    // - Between 30° and 0°: linear interpolation to minimum anisotropy
    // - At/below horizon: use minimum anisotropy
    static const float kMinMieG = 0.1;          // Nearly isotropic at horizon
    static const float kFullMieElevation = 0.5; // sin(30°)

    // Use max(0, ...) so below-horizon doesn't affect mie_g further
    float elevation_blend = saturate(max(0.0, sun_sin_elevation) / kFullMieElevation);
    float effective_mie_g = lerp(kMinMieG, atmo.mie_g, elevation_blend);
    float mie_phase = HenyeyGreensteinPhase(cos_theta, effective_mie_g);

    // Sun intensity factor for twilight:
    // - Above horizon (elevation >= 0): full intensity
    // - At horizon (0°): drop to ~0.3 immediately
    // - From 0° to -10°: gradual fade to 0
    // - Below -10°: night (0)
    //
    // sin(-10°) ≈ -0.17
    static const float kTwilightEnd = -0.17; // sin(-10°)

    float sun_intensity_factor;
    if (sun_sin_elevation >= 0.0)
    {
        // Sun above horizon: full intensity
        sun_intensity_factor = 1.0;
    }
    else if (sun_sin_elevation >= kTwilightEnd)
    {
        // Twilight: 0° to -10°, fade from 0.3 to 0
        // Use smoothstep for non-linear fade (faster at start, slower at end)
        float t = sun_sin_elevation / kTwilightEnd; // 0 at horizon, 1 at -10°
        sun_intensity_factor = 0.3 * (1.0 - smoothstep(0.0, 1.0, t));
    }
    else
    {
        // Night (below -10°)
        sun_intensity_factor = 0.0;
    }

        // Multi-scattering approximation: boost single scattering.
        // The slider remains 0..1, but maps to a wider and more useful range.
        float multi_scatter_boost
            = lerp(1.0, 3.0, saturate(atmo.multi_scattering_factor));

    float3 accumulated_optical_depth = float3(0.0, 0.0, 0.0);

    for (uint i = 0; i < num_steps; ++i)
    {
        // Mid-point sampling (stable). Higher LUT resolution + higher sample
        // counts reduce visible striping without introducing coherent noise.
        float t = (float(i) + 0.5) * step_size;
        float3 sample_pos = origin + view_dir * t;

        float altitude = length(sample_pos) - atmo.planet_radius_m;
        altitude = max(altitude, 0.0);

        // Skip samples outside atmosphere
        if (altitude > atmo.atmosphere_height_m)
        {
            continue;
        }

        // Density at sample point
        float density_rayleigh = GetAtmosphereDensity(altitude, atmo.rayleigh_scale_height_m);
        float density_mie = GetAtmosphereDensity(altitude, atmo.mie_scale_height_m);
        float density_abs = GetAbsorptionDensity(altitude, atmo.absorption_scale_height_m);

        // Transmittance from sample to atmosphere top (toward sun)
        float3 sample_dir = normalize(sample_pos);
        float cos_sun_zenith = dot(sample_dir, sun_dir);
        float3 sun_od = SampleTransmittanceLutOpticalDepth(
            altitude, cos_sun_zenith, atmo, transmittance_lut, linear_sampler, lut_size);
        float3 sun_transmittance = TransmittanceFromOpticalDepth(sun_od, atmo);

        // Transmittance from camera to this sample (mid-point approximation).
        float3 od_step = float3(density_rayleigh, density_mie, density_abs) * step_size;
        float3 od_mid = accumulated_optical_depth + od_step * 0.5;
        float3 view_transmittance = TransmittanceFromOpticalDepth(od_mid, atmo);

        // Scattering coefficient (per meter) at the sample.
        float3 sigma_s = atmo.rayleigh_scattering_rgb * density_rayleigh * rayleigh_phase
                       + atmo.mie_scattering_rgb * density_mie * mie_phase;

        // Inscattered radiance contribution (per unit sun radiance).
        // Apply sun_intensity_factor to simulate sunset dimming.
        inscatter += sun_transmittance * view_transmittance * sigma_s
                   * step_size * multi_scatter_boost * sun_intensity_factor;

        accumulated_optical_depth += od_step;
    }

    // Ground albedo contribution: if the ray hits the ground, add reflected light
    if (hits_ground)
    {
        // Ground position is at end of ray
        float3 ground_pos = origin + view_dir * ray_length;
        float3 ground_normal = normalize(ground_pos);

        // Sun illumination on ground (Lambertian)
        float ground_ndotl = max(0.0, dot(ground_normal, sun_dir));

        // Transmittance from sun to ground
        float3 ground_sun_od = SampleTransmittanceLutOpticalDepth(
            0.0, dot(ground_normal, sun_dir), atmo, transmittance_lut, linear_sampler, lut_size);
        float3 ground_sun_transmittance = TransmittanceFromOpticalDepth(ground_sun_od, atmo);

                // Ground reflected radiance (Lambertian BRDF = albedo / PI)
                float3 ground_reflected = atmo.ground_albedo_rgb * (1.0 / PI)
                                                                * ground_ndotl
                                                                * ground_sun_transmittance;

                float3 view_transmittance_to_ground
                    = TransmittanceFromOpticalDepth(accumulated_optical_depth, atmo);

                inscatter += ground_reflected * view_transmittance_to_ground
                                     * atmo.multi_scattering_factor;
    }

        // View-path transmittance (luminance proxy).
        float3 view_transmittance_end
            = TransmittanceFromOpticalDepth(accumulated_optical_depth, atmo);
        float total_transmittance = saturate(Luminance(view_transmittance_end));

        return float4(inscatter, total_transmittance);
}

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    // Load pass constants
    ConstantBuffer<SkyViewLutPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    // Bounds check
    if (dispatch_thread_id.x >= pass_constants.output_width
        || dispatch_thread_id.y >= pass_constants.output_height)
    {
        return;
    }

    // Load environment static data for atmosphere parameters
    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        // Fallback: write neutral sky
        RWTexture2D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
        output[dispatch_thread_id.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    GpuSkyAtmosphereParams atmo = env_data.atmosphere;

    // Sun direction in sun-relative space: sun is always at azimuth=0 (along +X).
    // The sun_cos_zenith from pass constants defines the elevation.
    float sun_cos_zenith = pass_constants.sun_cos_zenith;
    float sun_sin_zenith = sqrt(max(0.0, 1.0 - sun_cos_zenith * sun_cos_zenith));
    float3 sun_dir = float3(sun_sin_zenith, 0.0, sun_cos_zenith);

    // Compute UV from texel center
    float2 uv = (float2(dispatch_thread_id.xy) + 0.5)
              / float2(pass_constants.output_width, pass_constants.output_height);

    // Convert UV to view direction using sun-relative parameterization.
    // In this space, U=0.5 corresponds to looking toward the sun's horizontal direction.
    float altitude = pass_constants.camera_altitude_m;
    float3 view_dir = UvToViewDirection(uv, atmo.planet_radius_m, altitude, sun_cos_zenith);

    // Camera position (at altitude above planet surface, on Z-axis)
    float r = atmo.planet_radius_m + altitude;
    float3 origin = float3(0.0, 0.0, r);

    // Compute ray length to atmosphere boundary
    float atmosphere_radius = atmo.planet_radius_m + atmo.atmosphere_height_m;
    float ray_length = RaySphereIntersect(origin, view_dir, atmosphere_radius);

    float4 result = float4(0.0, 0.0, 0.0, 1.0);

    if (ray_length > 0.0)
    {
        // Check if ray hits ground
        bool hits_ground = false;
        float ground_dist = RaySphereIntersect(origin, view_dir, atmo.planet_radius_m);
        if (ground_dist > 0.0 && ground_dist < ray_length)
        {
            ray_length = ground_dist;
            hits_ground = true;
        }

        // Load transmittance LUT
        Texture2D<float4> transmittance_lut
            = ResourceDescriptorHeap[pass_constants.transmittance_srv_index];
        SamplerState linear_sampler = SamplerDescriptorHeap[0]; // Assume sampler 0 is linear

        uint2 lut_size = uint2(pass_constants.transmittance_width,
                               pass_constants.transmittance_height);

        // Compute single scattering with ground bounce contribution
        result = ComputeSingleScattering(
            origin, view_dir, sun_dir, ray_length, hits_ground, atmo,
            transmittance_lut, linear_sampler, lut_size);
    }

    // Write result
    RWTexture2D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
    output[dispatch_thread_id.xy] = result;
}

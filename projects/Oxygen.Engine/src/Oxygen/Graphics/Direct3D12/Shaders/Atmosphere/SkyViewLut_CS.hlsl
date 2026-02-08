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
#include "Renderer/SceneConstants.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Atmosphere/AtmosphereMedium.hlsli"
#include "Common/Math.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Coordinates.hlsli"
#include "Common/Lighting.hlsli"

// Root constants (b2, space0)
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Pass constants for sky-view LUT generation.
// Layout must exactly match C++ SkyViewLutPassConstants [P6].
struct SkyViewLutPassConstants
{
    uint output_uav_index;          // UAV index for output RWTexture2DArray<float4>
    uint transmittance_srv_index;   // SRV index for transmittance LUT
    uint multi_scat_srv_index;      // SRV index for MultiScat LUT
    uint output_width;              // LUT width (per slice)

    uint output_height;             // LUT height (per slice)
    uint transmittance_width;       // Transmittance LUT width
    uint transmittance_height;      // Transmittance LUT height
    uint slice_count;               // Number of altitude slices

    float sun_cos_zenith;           // Cosine of sun zenith angle (sun_dir.z)
    uint atmosphere_flags;          // Debug/feature flags (kUseAmbientTerm, etc.)
    uint alt_mapping_mode;          // 0 = linear, 1 = log
    float atmosphere_height_m;      // Total atmosphere height in meters

    float planet_radius_m;          // Planet radius in meters
    uint _pad0;                     // Padding for 16-byte alignment
    uint _pad1;
    uint _pad2;
};

// Thread group size: 8x8 threads per group
#define THREAD_GROUP_SIZE_X 8
#define THREAD_GROUP_SIZE_Y 8

// Number of raymarch samples - higher values reduce banding but cost more.
static const uint MIN_SCATTERING_SAMPLES = 96;
static const uint MAX_SCATTERING_SAMPLES = 256;

// Atmosphere feature flag bits (matches C++ AtmosphereFlags enum)


//! Computes the altitude in meters for a given slice index.
//!
//! Uses centered-bin mapping so each slice represents the center of its
//! altitude range, not the boundary. Supports two mapping modes:
//!   0 = linear:  h(t) = H * t
//!   1 = log:     h(t) = H * (2^t - 1)
//! where t = (slice + 0.5) / slice_count is the normalized center of the bin,
//! and H = atmosphere_height_m.
//!
//! @param slice_index     Integer slice index (dispatch_thread_id.z).
//! @param slice_count     Total number of altitude slices.
//! @param atmosphere_h    Atmosphere height in meters (H).
//! @param mapping_mode    0 = linear, 1 = log.
//! @return Altitude above ground in meters.
float GetSliceAltitudeM(uint slice_index, uint slice_count,
                        float atmosphere_h, uint mapping_mode)
{
    // Centered-bin: t is the center of the bin, not its edge.
    float t = (float(slice_index) + 0.5) / float(slice_count);

    if (mapping_mode == 1)
    {
        // Log mapping: h = H * (2^t - 1). Gives higher density near ground
        // where atmosphere density changes most rapidly.
        return atmosphere_h * (exp2(t) - 1.0);
    }

    // Linear mapping: h = H * t.
    return atmosphere_h * t;
}




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

    // Compute horizon angle (from zenith) for this altitude.
    // cos_horizon is -sqrt(1 - (R/r)^2).
    float r = planet_radius + camera_altitude;
    float rho = planet_radius / r;
    float cos_horizon = -sqrt(max(0.0, 1.0 - rho * rho));

    float zenith_horizon_angle = acos(cos_horizon);
    float beta = PI - zenith_horizon_angle; // Angle from horizon to nadir

    // NON-LINEAR V mapping (Reference): Map angles, not cosines.
    // V=0.5 is horizon.
    float view_zenith_angle;
    if (uv.y < 0.5)
    {
        // Below horizon (Sky): map [0, 0.5] -> [0, ZenithHorizonAngle]
        float coord = uv.y * 2.0;
        coord = 1.0 - coord;
        coord *= coord; // SQUARED distribution near horizon
        coord = 1.0 - coord;
        view_zenith_angle = zenith_horizon_angle * coord;
    }
    else
    {
        // Above horizon (Ground): map [0.5, 1] -> [ZenithHorizonAngle, PI]
        float coord = uv.y * 2.0 - 1.0;
        coord *= coord; // SQUARED distribution near horizon
        view_zenith_angle = zenith_horizon_angle + beta * coord;
    }

    float cos_zenith = cos(view_zenith_angle);
    float sin_zenith = sqrt(max(0.0, 1.0 - cos_zenith * cos_zenith));

    // View direction in sun-relative coordinates:
    return float3(
        sin_zenith * cos(relative_azimuth),
        sin_zenith * sin(relative_azimuth),
        cos_zenith);
}


// Atmosphere-specific functions - these will move to AtmosphereMath.hlsli
float GetAtmosphereDensity(float altitude, float scale_height)
{
    return exp(-altitude / scale_height);
}

float GetAbsorptionDensity(float altitude, float absorption_center_m)
{
    altitude = max(altitude, 0.0);

    float center = max(1.0, absorption_center_m);
    float width = max(1000.0, center * 0.6);
    float t = 1.0 - abs(altitude - center) / width;
    return saturate(t);
}

//! Samples the transmittance LUT.
//!
//! @param altitude Altitude above ground in meters.
//! @param cos_zenith Cosine of zenith angle (toward sun).
//! @param atmo Atmosphere parameters.
//! @param transmittance_lut Transmittance LUT texture.
//! @param lut_size LUT dimensions.
//! @return Optical depth (Rayleigh, Mie, Absorption).
float3 SampleTransmittanceLutOpticalDepth(
    float altitude,
    float cos_zenith,
    GpuSkyAtmosphereParams atmo,
    Texture2D<float4> transmittance_lut,
    SamplerState linear_sampler,
    uint2 lut_size)
{
    // Compute the local horizon angle at this sample's altitude using common helper
    float cos_horizon = HorizonCosineFromAltitude(atmo.planet_radius_m, altitude);

    // Hard cutoff: if sun is below local horizon, planet blocks it completely.
    if (cos_zenith < cos_horizon)
    {
        // Sun blocked by planet - zero transmittance (infinite optical depth)
        return float3(1e6, 1e6, 1e6);
    }

    float view_height = atmo.planet_radius_m + altitude;
    float top_radius = atmo.planet_radius_m + atmo.atmosphere_height_m;
    float H = SafeSqrt(top_radius * top_radius - atmo.planet_radius_m * atmo.planet_radius_m);
    float rho = SafeSqrt(view_height * view_height - atmo.planet_radius_m * atmo.planet_radius_m);

    float discriminant = view_height * view_height
        * (cos_zenith * cos_zenith - 1.0)
        + top_radius * top_radius;
    float d = max(0.0, (-view_height * cos_zenith + SafeSqrt(discriminant)));

    float d_min = top_radius - view_height;
    float d_max = rho + H;
    float u = (d - d_min) / (d_max - d_min);
    float v = rho / H;
    u = clamp(u, 0.0, 1.0);
    v = clamp(v, 0.0, 1.0);

    return transmittance_lut.SampleLevel(linear_sampler, float2(u, v), 0).rgb;
}

//! Converts optical depth (Rayleigh/Mie/Absorption) into RGB transmittance.
float3 TransmittanceFromOpticalDepth(float3 optical_depth, GpuSkyAtmosphereParams atmo)
{
    // Extinction = Rayleigh scattering + Mie extinction + absorption.
    // Mie extinction = Mie scattering + Mie absorption (UE5 style).
    float3 beta_rayleigh = atmo.rayleigh_scattering_rgb;
    float3 beta_mie_ext = atmo.mie_scattering_rgb + atmo.mie_absorption_rgb;
    float3 beta_abs = atmo.absorption_rgb;

    float3 tau = beta_rayleigh * optical_depth.x
               + beta_mie_ext * optical_depth.y
               + beta_abs * optical_depth.z;

    return exp(-tau);
}

//! Rayleigh phase function.
float RayleighPhase(float cos_theta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

//! Cornette-Shanks Phase Function (Matches Unreal Engine 5 Reference)
//! Used for Mie scattering. Physically more accurate than standard HG.
float CornetteShanksMiePhaseFunction(float g, float cos_theta)
{
    float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
    // Note: Denominator uses +2*g*cos_theta because forward scatter is usually aligned.
    // However, Unreal's hgPhase reference implementation uses:
    // pow(1 + g^2 + 2*g*cosTheta, 1.5)
    // We use the same here.
    float denom = 1.0 + g * g - 2.0 * g * cos_theta;
    // Wait, standard HG is (1+g^2 - 2g*cos).
    // If cos=1, denom=(1-g)^2.
    // Unreal uses -cosTheta in call site, so effective cosTheta is -1.
    // ...
    // Let's stick to the correct HG formula for forward scatter (cos=1):
    // denom = 1 + g^2 - 2g*cos

    // Safety clamp for denom to prevent division by zero at singularity
    denom = max(denom, 1e-5);

    return k * (1.0 + cos_theta * cos_theta) / pow(denom, 1.5);
}

// Replaced HenyeyGreensteinPhase with CornetteShanks
float HenyeyGreensteinPhase(float cos_theta, float g)
{
    // Call the improved function
    // Clamp result to prevent FP16 overflow (Inf)
    float result = CornetteShanksMiePhaseFunction(g, cos_theta);
    return min(result, 60000.0);
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
//! @param atmosphere_flags Bitfield of AtmosphereFlags for debug options.
//! @return (inscattered_radiance.rgb, total_transmittance).
float4 ComputeSingleScattering(
    float3 origin,
    float3 view_dir,
    float3 sun_dir,
    float ray_length,
    bool hits_ground,
    GpuSkyAtmosphereParams atmo,
    Texture2D<float4> transmittance_lut,
    Texture2D<float4> multi_scat_lut,
    SamplerState linear_sampler,
    uint2 lut_size,
    uint atmosphere_flags)
{
    float3 inscatter = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);

    // Adaptive step count: more steps near the horizon/twilight.
    const float horizon_factor_view = saturate(1.0 - abs(view_dir.z));

    // Adaptive step count: more steps for long paths (horizon/sunset) to reduce banding.
    // Reference suggests 32, but for high quality we can go higher.
    const float min_steps = 32.0;
    const float max_steps = 64.0;

    // 100km path length starts to need more samples.
    // 600km+ is typical horizon path.
    float step_factor = saturate(ray_length / 200000.0);
    const uint num_steps = (uint)lerp(min_steps, max_steps, step_factor);

    float cos_theta = dot(view_dir, sun_dir);
    float rayleigh_phase = RayleighPhase(cos_theta);
    float mie_phase = HenyeyGreensteinPhase(cos_theta, atmo.mie_g);

    float ms_factor = atmo.multi_scattering_factor;

    // Precompute coefficients for extinction reconstruction
    float3 beta_rayleigh = atmo.rayleigh_scattering_rgb;
    float3 beta_mie_ext = atmo.mie_scattering_rgb + atmo.mie_absorption_rgb;
    float3 beta_abs = atmo.absorption_rgb;

    for (uint i = 0; i < num_steps; ++i)
    {
        // UE5 Reference: Quadratic distribution of samples (t^2)
        float t0 = float(i) / float(num_steps);
        float t1 = float(i + 1) / float(num_steps);

        t0 = t0 * t0;
        t1 = t1 * t1;

        t0 = t0 * ray_length;
        t1 = t1 * ray_length;

        float dt = t1 - t0;
        // UE5 Reference: Fixed 0.3 offset within the segment (SampleSegmentT)
        // No jitter/noise is used in the reference implementation for SkyViewLut.
        float t = t0 + dt * 0.3;

        float3 sample_pos = origin + view_dir * t;
        float altitude = length(sample_pos) - atmo.planet_radius_m;
        altitude = max(altitude, 0.0);

        if (altitude > atmo.atmosphere_height_m) continue;

        float d_r = AtmosphereExponentialDensity(altitude, atmo.rayleigh_scale_height_m);
        float d_m = AtmosphereExponentialDensity(altitude, atmo.mie_scale_height_m);
        float d_a = OzoneAbsorptionDensity(altitude, atmo.absorption_layer_width_m, atmo.absorption_term_below, atmo.absorption_term_above);

        // Reconstruction of extinction at this point
        float3 extinction = beta_rayleigh * d_r + beta_mie_ext * d_m + beta_abs * d_a;
        float3 sample_optical_depth = extinction * dt;
        float3 sample_transmittance = exp(-sample_optical_depth);

        // Sun Transmittance
        float3 sample_dir = normalize(sample_pos);
        float cos_sun_zenith = dot(sample_dir, sun_dir);
        float3 sun_od = SampleTransmittanceLutOpticalDepth(
            altitude, cos_sun_zenith, atmo, transmittance_lut, linear_sampler, lut_size);
        float3 sun_transmittance = TransmittanceFromOpticalDepth(sun_od, atmo);

        // Retrieve Sun Radiance (Color * Illuminance)
        // We must apply the sun's physical intensity to get correct sky brightness.
        float3 sun_radiance = GetSunColorRGB() * GetSunIlluminance();

        // === Combined Scattering with Multi-Scat ===
        // Single scattering: light from sun -> scatter once -> camera
        // S = L_sun * T_sun * (beta_R * phase_R + beta_M * phase_M)
        float3 sigma_s_single = (atmo.rayleigh_scattering_rgb * d_r * rayleigh_phase
                              + atmo.mie_scattering_rgb * d_m * mie_phase)
                              * sun_transmittance * sun_radiance;

        // Multiple scattering
        float u_ms = (cos_sun_zenith + 1.0) / 2.0;
        float v_ms = altitude / atmo.atmosphere_height_m;
        float4 ms_sample = multi_scat_lut.SampleLevel(linear_sampler, float2(u_ms, v_ms), 0);

        float3 multi_scat_radiance = ms_sample.rgb;
        float f_ms = ms_sample.a;
        float3 energy_compensation = 1.0 / max(1.0 - f_ms, 1e-4);

        // UE5 style for multi-scat source logic:
        // S_ms = (beta_R + beta_M) * MultiScatRadiance * EnergyComp
        // We also apply sun_radiance here because MultiScatLUT is typically normalized (unit sun).
        float3 sigma_s_multi = (atmo.rayleigh_scattering_rgb * d_r + atmo.mie_scattering_rgb * d_m)
                             * multi_scat_radiance * energy_compensation * ms_factor * sun_radiance;

        // Total Source Function (S)
        float3 S = sigma_s_single + sigma_s_multi;

        // Frostbite / UE5 Analytic Integration:
        // L += throughput * (S - S * T_step) / extinction
        // (S - S*T_step) / ext -> S * step_size (limit as ext -> 0)

        float3 Sint;
        // Check for small extinction to avoid div-by-zero
        // Using a check similar to UE5 implicit behaviors or explicit limit
        if (all(extinction < 1e-6))
        {
            Sint = S * dt;
        }
        else
        {
            Sint = (S - S * sample_transmittance) / max(extinction, 1e-6);
        }

        // Accumulate
        inscatter += throughput * Sint;
        inscatter = min(inscatter, float3(65000.0, 65000.0, 65000.0)); // Aggregate safety clamp

        throughput *= sample_transmittance;
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

        // Add ground reflection, attenuated by the total transmittance of the path
        inscatter += ground_reflected * throughput;
    }

    // View-path transmittance is just the final throughput
    float total_transmittance = Saturate(Luminance(throughput));

    return float4(inscatter, total_transmittance);
}

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    // Load pass constants
    ConstantBuffer<SkyViewLutPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    // Bounds check — Z maps directly to slice index because numthreads.z == 1 [P3].
    if (dispatch_thread_id.x >= pass_constants.output_width
        || dispatch_thread_id.y >= pass_constants.output_height
        || dispatch_thread_id.z >= pass_constants.slice_count)
    {
        return;
    }

    // Load environment static data for atmosphere parameters
    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        // Fallback: write neutral sky
        RWTexture2DArray<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
        output[uint3(dispatch_thread_id.xy, dispatch_thread_id.z)] = float4(0.0, 0.0, 0.0, 1.0);
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

    // Reference: Remap UVs to [0,1] parameter space (Top-left texel center should map to 0,0 parameter).
    // This allows exact sampling of extrema (Zenith, Horizon, Sun).
    uv.x = (uv.x - 0.5 / pass_constants.output_width) * (pass_constants.output_width / (pass_constants.output_width - 1.0));
    uv.y = (uv.y - 0.5 / pass_constants.output_height) * (pass_constants.output_height / (pass_constants.output_height - 1.0));

    // Clamp to [0,1] to handle precision issues
    uv = saturate(uv);

    // Derive per-slice camera altitude from slice index using the selected
    // mapping function. Each slice represents a different altitude band,
    // replacing the old single camera_altitude_m pass constant.
    float altitude = GetSliceAltitudeM(
        dispatch_thread_id.z,
        pass_constants.slice_count,
        pass_constants.atmosphere_height_m,
        pass_constants.alt_mapping_mode);

    // Convert UV to view direction using sun-relative parameterization.
    // Reference Azimuth Mapping: 0..1 -> 0..PI (Symmetric around sun).
    // Squared distribution concentrates samples near the sun.

    // 1. Azimuth (uv.x)
    float azimuth_coord = uv.x;
    azimuth_coord *= azimuth_coord; // Squared

    // map [0,1] -> [1, -1] (Cosine of angle 0 to PI)
    float cos_relative_azimuth = -(azimuth_coord * 2.0 - 1.0);
    float sin_relative_azimuth = sqrt(saturate(1.0 - cos_relative_azimuth * cos_relative_azimuth));

    // 2. Zenith (uv.y) - Reference Angle Logic
    // Compute horizon angle (from zenith) for this altitude.
    float r = pass_constants.planet_radius_m + altitude;
    float rho = pass_constants.planet_radius_m / r;
    float cos_horizon = -sqrt(max(0.0, 1.0 - rho * rho));

    float zenith_horizon_angle = acos(cos_horizon);
    float beta = PI - zenith_horizon_angle;

    float view_zenith_angle;
    if (uv.y < 0.5)
    {
        // Sky
        float coord = uv.y * 2.0;
        coord = 1.0 - coord;
        coord *= coord; // Squared
        coord = 1.0 - coord;
        view_zenith_angle = zenith_horizon_angle * coord;
    }
    else
    {
        // Ground
        float coord = uv.y * 2.0 - 1.0;
        coord *= coord; // Squared
        view_zenith_angle = zenith_horizon_angle + beta * coord;
    }

    float cos_zenith = cos(view_zenith_angle);
    float sin_zenith = sqrt(max(0.0, 1.0 - cos_zenith * cos_zenith));

    // View direction construction (Z-up)
    // Azimuth 0 (Sun) -> +X. Azimuth PI -> -X.
    // Symmetry implies we only render 0..PI hemisphere (Y >= 0).
    float3 view_dir = float3(
        sin_zenith * cos_relative_azimuth,
        sin_zenith * sin_relative_azimuth,
        cos_zenith);

    // Camera position (at altitude above planet surface, on Z-axis)
    float3 origin = float3(0.0, 0.0, r);

    // Compute ray length to atmosphere boundary
    float atmosphere_radius = pass_constants.planet_radius_m + pass_constants.atmosphere_height_m;
    float ray_length = RaySphereIntersectNearest(origin, view_dir, atmosphere_radius);

    float4 result = float4(0.0, 0.0, 0.0, 1.0);

    if (ray_length > 0.0)
    {
        // Check if ray hits ground
        bool hits_ground = false;
        float ground_dist = RaySphereIntersectNearest(origin, view_dir, pass_constants.planet_radius_m);
        if (ground_dist > 0.0 && ground_dist < ray_length)
        {
            ray_length = ground_dist;
            hits_ground = true;
        }

        // Load transmittance LUT
        Texture2D<float4> transmittance_lut
            = ResourceDescriptorHeap[pass_constants.transmittance_srv_index];
        Texture2D<float4> multi_scat_lut
            = ResourceDescriptorHeap[pass_constants.multi_scat_srv_index];
        SamplerState linear_sampler = SamplerDescriptorHeap[0]; // Assume sampler 0 is linear

        uint2 lut_size = uint2(pass_constants.transmittance_width,
                               pass_constants.transmittance_height);

        // Get atmosphere flags from pass constants (set by LUT manager)
        uint atmosphere_flags = pass_constants.atmosphere_flags;

        result = ComputeSingleScattering(
            origin, view_dir, sun_dir, ray_length, hits_ground, atmo,
            transmittance_lut, multi_scat_lut, linear_sampler, lut_size,
            atmosphere_flags);
    }

    // Safety: prevent NaNs/Infs from polluting the Sky View LUT.
    // These NaNs can propagate to Sky Capture -> IBL -> Lighting.
    // We try to recover by clamping to max-value if Inf, or fallback to simple scattering if NaN.
    if (any(isnan(result)) || any(isinf(result)))
    {
        // Now that we clamped the accumulation loop, we shouldn't hit this often.
        // But if we do (NaN), return Black (0.0) which is safer than White for standard compositing.
        // (White creates a flat disk, Black usually blends better with surrounding sky if it's just a pixel).
        result = float4(0.0, 0.0, 0.0, 1.0);
    }

    // Write result to the correct array slice [P4].
    RWTexture2DArray<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
    output[uint3(dispatch_thread_id.xy, dispatch_thread_id.z)] = result;
}

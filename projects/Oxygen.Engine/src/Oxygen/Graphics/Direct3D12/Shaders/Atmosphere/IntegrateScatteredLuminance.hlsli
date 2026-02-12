//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Shared Raymarch Integration for Atmosphere Scattering
//!
//! Provides the common analytic segment integration logic used by SkyViewLut
//! and CameraVolumeLut compute shaders. Based on the Frostbite/UE5 approach.
//!
//! Usage:
//!   1. Define INTEGRATE_MODE_* before including this file
//!   2. Call IntegrateScatteredLuminance() with appropriate parameters
//!
//! Integration Modes (compile-time specialization):
//!   INTEGRATE_MODE_SKY_VIEW    - Quadratic sample distribution, directional phase
//!   INTEGRATE_MODE_CAMERA_VOL  - Uniform distribution, directional phase

#ifndef OXYGEN_GRAPHICS_SHADERS_INTEGRATE_SCATTERED_LUMINANCE_HLSLI
#define OXYGEN_GRAPHICS_SHADERS_INTEGRATE_SCATTERED_LUMINANCE_HLSLI

#include "Common/Math.hlsli"
#include "Atmosphere/AtmosphereConstants.hlsli"
#include "Atmosphere/AtmosphereMedium.hlsli"
#include "Atmosphere/AtmospherePhase.hlsli"
#include "Atmosphere/AtmosphereSampling.hlsli"
#include "Renderer/EnvironmentStaticData.hlsli"

//------------------------------------------------------------------------------
// Integration Step Result
//------------------------------------------------------------------------------

//! Result of a single integration step.
struct IntegrationStepResult
{
    float3 inscatter;           //!< Accumulated inscattered radiance
    float3 throughput;          //!< Remaining view transmittance
};

//------------------------------------------------------------------------------
// Single Integration Step (Frostbite Analytic Form)
//------------------------------------------------------------------------------

//! Performs a single step of the Frostbite/UE analytic scattering integration.
//!
//! Computes: Sint = (S - S * T_step) / extinction
//! Accumulates: L += throughput * Sint; throughput *= T_step
//!
//! @param sample_pos         World-space sample position.
//! @param dt                 Step size in meters.
//! @param atmo               Atmosphere parameters.
//! @param sun_dir            Normalized sun direction.
//! @param sun_illuminance    Sun illuminance (RGB).
//! @param rayleigh_phase     Precomputed Rayleigh phase value.
//! @param mie_phase          Precomputed Mie phase value.
//! @param transmittance_srv  Transmittance LUT SRV index.
//! @param transmittance_w    Transmittance LUT width.
//! @param transmittance_h    Transmittance LUT height.
//! @param multi_scat_lut     Multi-scatter LUT texture.
//! @param linear_sampler     Linear sampler for LUT access.
//! @param prev_inscatter     Previous accumulated inscatter.
//! @param prev_throughput    Previous throughput.
//! @return Updated inscatter and throughput.
IntegrationStepResult IntegrateSingleStep(
    float3 sample_pos,
    float dt,
    GpuSkyAtmosphereParams atmo,
    float3 sun_dir,
    float3 sun_illuminance,
    float rayleigh_phase,
    float mie_phase,
    uint transmittance_srv,
    float transmittance_w,
    float transmittance_h,
    Texture2D<float4> multi_scat_lut,
    SamplerState linear_sampler,
    float3 prev_inscatter,
    float3 prev_throughput)
{
    IntegrationStepResult result;
    result.inscatter = prev_inscatter;
    result.throughput = prev_throughput;

    float altitude_m = length(sample_pos) - atmo.planet_radius_m;
    altitude_m = max(altitude_m, 0.0);

    // Skip if outside atmosphere
    if (altitude_m > atmo.atmosphere_height_m)
    {
        return result;
    }

    // Density at sample point
    float d_r = AtmosphereExponentialDensity(altitude_m, atmo.rayleigh_scale_height_m);
    float d_m = AtmosphereExponentialDensity(altitude_m, atmo.mie_scale_height_m);
    float d_a = OzoneAbsorptionDensity(altitude_m, atmo.absorption_density);

    // Extinction and transmittance for this step
    float3 beta_rayleigh = atmo.rayleigh_scattering_rgb;
    float3 beta_mie_ext = atmo.mie_extinction_rgb;
    float3 beta_abs = atmo.absorption_rgb;

    float3 extinction = beta_rayleigh * d_r + beta_mie_ext * d_m + beta_abs * d_a;
    float3 sample_optical_depth = extinction * dt;
    float3 sample_transmittance = exp(-sample_optical_depth);

    // Sun transmittance from sample point
    float3 sample_dir = normalize(sample_pos);
    float cos_sun_zenith = dot(sample_dir, sun_dir);

    // Planet shadowing: if the sun is below the geometric horizon at this
    // altitude, direct sunlight is occluded by the planet. Because this
    // atmosphere model is sun-driven, we also suppress the multi-scattering
    // source term when the sun is occluded (prevents unphysical night-time
    // energy injection).
    const float cos_horizon = HorizonCosineFromAltitude(
        atmo.planet_radius_m,
        altitude_m);
    const bool sun_visible = (cos_sun_zenith >= cos_horizon);
    float3 sun_transmittance = float3(0.0, 0.0, 0.0);
    if (sun_visible)
    {
        float3 sun_od = SampleTransmittanceOpticalDepthLut(
            transmittance_srv,
            transmittance_w,
            transmittance_h,
            cos_sun_zenith, altitude_m,
            atmo.planet_radius_m,
            atmo.atmosphere_height_m);
        sun_transmittance = TransmittanceFromOpticalDepth(sun_od, atmo);
    }

    // Single scattering: L_sun * T_sun * (beta_R * phase_R + beta_M * phase_M)
    float3 sigma_s_single = float3(0.0, 0.0, 0.0);
    if (sun_visible)
    {
        sigma_s_single = (atmo.rayleigh_scattering_rgb * d_r * rayleigh_phase
                       + atmo.mie_scattering_rgb * d_m * mie_phase)
                       * sun_transmittance * sun_illuminance;
    }

    // Multi-scattering contribution
    float3 sigma_s_multi = float3(0.0, 0.0, 0.0);
    if (sun_visible)
    {
        float u_ms = (cos_sun_zenith + 1.0) / 2.0;
        float v_ms = altitude_m / atmo.atmosphere_height_m;
        float4 ms_sample
            = multi_scat_lut.SampleLevel(linear_sampler, float2(u_ms, v_ms), 0);

        float3 multi_scat_radiance = ms_sample.rgb;
        float f_ms = ms_sample.a;
        float3 energy_compensation = 1.0 / max(1.0 - f_ms, 1e-4);

        sigma_s_multi = (atmo.rayleigh_scattering_rgb * d_r
                      + atmo.mie_scattering_rgb * d_m)
                      * multi_scat_radiance * energy_compensation
                      * atmo.multi_scattering_factor * sun_illuminance;
    }

    // Total source function
    float3 S = sigma_s_single + sigma_s_multi;

    // Frostbite analytic integration:
    // L += throughput * (S - S * T_step) / extinction
    float3 Sint;
    if (all(extinction < kAtmosphereEpsilon))
    {
        Sint = S * dt;
    }
    else
    {
        Sint = (S - S * sample_transmittance) / max(extinction, kAtmosphereEpsilon);
    }

    result.inscatter += result.throughput * Sint;
    result.inscatter = min(result.inscatter, float3(kFP16SafeMax, kFP16SafeMax, kFP16SafeMax));
    result.throughput *= sample_transmittance;

    return result;
}

//------------------------------------------------------------------------------
// Full Integration Loop (Sky-View Mode)
//------------------------------------------------------------------------------

//! Integrates scattered luminance along a view ray using quadratic sample distribution.
//!
//! This is the main entry point for SkyViewLut integration. Uses fixed 0.3 offset
//! within each segment (UE5 SampleSegmentT) and quadratic sample distribution for
//! better horizon convergence.
//!
//! @param origin             Ray origin in planet-centered coordinates.
//! @param view_dir           Normalized view direction.
//! @param ray_length         Total ray length in meters.
//! @param num_steps          Number of integration steps.
//! @param atmo               Atmosphere parameters.
//! @param sun_dir            Normalized sun direction.
//! @param sun_illuminance    Sun illuminance (RGB).
//! @param transmittance_srv  Transmittance LUT SRV index.
//! @param transmittance_w    Transmittance LUT width.
//! @param transmittance_h    Transmittance LUT height.
//! @param multi_scat_lut     Multi-scatter LUT texture.
//! @param linear_sampler     Linear sampler for LUT access.
//! @param out_throughput     [out] Final view transmittance.
//! @return Accumulated inscattered radiance.
float3 IntegrateScatteredLuminanceQuadratic(
    float3 origin,
    float3 view_dir,
    float ray_length,
    uint num_steps,
    GpuSkyAtmosphereParams atmo,
    float3 sun_dir,
    float3 sun_illuminance,
    uint transmittance_srv,
    float transmittance_w,
    float transmittance_h,
    Texture2D<float4> multi_scat_lut,
    SamplerState linear_sampler,
    float segment_sample_offset,
    out float3 out_throughput)
{
    float3 inscatter = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);

    float cos_theta = dot(view_dir, sun_dir);
    float rayleigh_phase = RayleighPhase(cos_theta);
    float mie_phase = CornetteShanksMiePhase(cos_theta, atmo.mie_g);

    for (uint i = 0; i < num_steps; ++i)
    {
        // Quadratic sample distribution (UE5 reference)
        float t0 = float(i) / float(num_steps);
        float t1 = float(i + 1) / float(num_steps);
        t0 = t0 * t0;
        t1 = t1 * t1;
        t0 = t0 * ray_length;
        t1 = t1 * ray_length;

        float dt = t1 - t0;
        float t = t0 + dt * segment_sample_offset;

        float3 sample_pos = origin + view_dir * t;

        IntegrationStepResult step_result = IntegrateSingleStep(
            sample_pos, dt, atmo, sun_dir, sun_illuminance,
            rayleigh_phase, mie_phase,
            transmittance_srv, transmittance_w, transmittance_h,
            multi_scat_lut, linear_sampler,
            inscatter, throughput);

        inscatter = step_result.inscatter;
        throughput = step_result.throughput;
    }

    out_throughput = throughput;
    return inscatter;
}

//------------------------------------------------------------------------------
// Full Integration Loop (Camera-Volume Mode)
//------------------------------------------------------------------------------

//! Integrates scattered luminance with uniform sample distribution.
//!
//! This is the main entry point for CameraVolumeLut integration. Uses uniform
//! step size with fixed 0.3 offset for better froxel coherence.
//!
//! @param origin             Ray origin in planet-centered coordinates.
//! @param view_dir           Normalized view direction.
//! @param ray_length         Total ray length in meters.
//! @param num_steps          Number of integration steps.
//! @param atmo               Atmosphere parameters.
//! @param sun_dir            Normalized sun direction.
//! @param sun_illuminance    Sun illuminance (RGB).
//! @param transmittance_srv  Transmittance LUT SRV index.
//! @param transmittance_w    Transmittance LUT width.
//! @param transmittance_h    Transmittance LUT height.
//! @param multi_scat_lut     Multi-scatter LUT texture.
//! @param linear_sampler     Linear sampler for LUT access.
//! @param out_throughput     [out] Final view transmittance.
//! @return Accumulated inscattered radiance.
float3 IntegrateScatteredLuminanceUniform(
    float3 origin,
    float3 view_dir,
    float ray_length,
    uint num_steps,
    GpuSkyAtmosphereParams atmo,
    float3 sun_dir,
    float3 sun_illuminance,
    uint transmittance_srv,
    float transmittance_w,
    float transmittance_h,
    Texture2D<float4> multi_scat_lut,
    SamplerState linear_sampler,
    out float3 out_throughput)
{
    float3 inscatter = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);

    float step_size = ray_length / float(num_steps);

    float cos_theta = dot(view_dir, sun_dir);
    float rayleigh_phase = RayleighPhase(cos_theta);
    float mie_phase = CornetteShanksMiePhase(cos_theta, atmo.mie_g);

    for (uint i = 0; i < num_steps; ++i)
    {
        // Uniform distribution with fixed offset
        float t = (float(i) + kSegmentSampleOffset) * step_size;
        float3 sample_pos = origin + view_dir * t;

        IntegrationStepResult step_result = IntegrateSingleStep(
            sample_pos, step_size, atmo, sun_dir, sun_illuminance,
            rayleigh_phase, mie_phase,
            transmittance_srv, transmittance_w, transmittance_h,
            multi_scat_lut, linear_sampler,
            inscatter, throughput);

        inscatter = step_result.inscatter;
        throughput = step_result.throughput;
    }

    out_throughput = throughput;
    return inscatter;
}

#endif // OXYGEN_GRAPHICS_SHADERS_INTEGRATE_SCATTERED_LUMINANCE_HLSLI

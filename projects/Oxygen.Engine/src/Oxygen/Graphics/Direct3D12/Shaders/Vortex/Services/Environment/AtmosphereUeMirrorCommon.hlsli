//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREUEMIRRORCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREUEMIRRORCOMMON_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Renderer/EnvironmentStaticData.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Math.hlsli"

static const float kVortexSkyPi = 3.14159265359f;
static const float kVortexInfiniteOpticalDepth = 1.0e6f;
static const float kVortexSegmentSampleOffset = 0.3f;
static const float kVortexFp16SafeMax = 65000.0f;

struct VortexSingleScatteringResult
{
    float3 L;
    float3 OpticalDepth;
    float3 Transmittance;
};

struct VortexSamplingSetup
{
    bool VariableSampleCount;
    float SampleCountIni;
    float MinSampleCount;
    float MaxSampleCount;
    float DistanceToSampleCountMaxInv;
};

static float VortexAtmosphereExponentialDensity(float altitude_m, float scale_height_m)
{
    return exp(-max(altitude_m, 0.0f) / max(scale_height_m, 1.0e-4f));
}

static float VortexEvaluateDensityProfile(float altitude_m, AtmosphereDensityProfile profile)
{
    const float h = max(altitude_m, 0.0f);
    AtmosphereDensityLayer layer = profile.layers[1];
    if (h < profile.layers[0].width_m)
    {
        layer = profile.layers[0];
    }

    if (layer.exp_term != 0.0f)
    {
        return layer.exp_term * exp(layer.linear_term * h) + layer.constant_term;
    }

    return layer.linear_term * h + layer.constant_term;
}

static float VortexOzoneAbsorptionDensity(float altitude_m, AtmosphereDensityProfile profile)
{
    return saturate(VortexEvaluateDensityProfile(altitude_m, profile));
}

static float3 VortexTransmittanceFromOpticalDepth(float3 optical_depth, GpuSkyAtmosphereParams atmosphere)
{
    const float3 extinction = (atmosphere.rayleigh_scattering_rgb * optical_depth.x)
        + (atmosphere.mie_extinction_rgb * optical_depth.y)
        + (atmosphere.absorption_rgb * optical_depth.z);
    return exp(-extinction);
}

static float VortexRayleighPhase(float cosine_angle)
{
    return (3.0f / (16.0f * kVortexSkyPi)) * (1.0f + cosine_angle * cosine_angle);
}

static float VortexCornetteShanksMiePhase(float cosine_angle, float anisotropy)
{
    const float g = clamp(anisotropy, -0.99f, 0.99f);
    const float k = 3.0f / (8.0f * kVortexSkyPi) * (1.0f - g * g) / (2.0f + g * g);
    const float denom = max(1.0f + g * g - 2.0f * g * cosine_angle, 1.0e-5f);
    return min(k * (1.0f + cosine_angle * cosine_angle) / pow(denom, 1.5f), kVortexFp16SafeMax);
}

static float VortexHorizonCosineFromAltitude(float planet_radius_m, float altitude_m)
{
    const float radius = planet_radius_m + max(altitude_m, 0.0f);
    const float rho = planet_radius_m / max(radius, 1.0f);
    return -sqrt(max(0.0f, 1.0f - rho * rho));
}

static float2 VortexGetTransmittanceLutUv(
    float cos_zenith,
    float altitude_m,
    float planet_radius_m,
    float atmosphere_height_m)
{
    const float view_height = planet_radius_m + altitude_m;
    const float top_radius = planet_radius_m + atmosphere_height_m;
    const float H = sqrt(max(0.0f, top_radius * top_radius - planet_radius_m * planet_radius_m));
    const float rho = sqrt(max(0.0f, view_height * view_height - planet_radius_m * planet_radius_m));

    const float discriminant = view_height * view_height * (cos_zenith * cos_zenith - 1.0f)
        + top_radius * top_radius;
    const float d = max(0.0f, (-view_height * cos_zenith + sqrt(max(discriminant, 0.0f))));
    const float d_min = top_radius - view_height;
    const float d_max = rho + H;
    const float x_mu = (d - d_min) / max(d_max - d_min, 1.0e-4f);
    const float x_r = rho / max(H, 1.0e-4f);
    return saturate(float2(x_mu, x_r));
}

static float2 VortexApplyHalfTexelOffset(float2 uv, float lut_width, float lut_height)
{
    uv = uv * float2((lut_width - 1.0f) / lut_width, (lut_height - 1.0f) / lut_height);
    uv += float2(0.5f / lut_width, 0.5f / lut_height);
    return uv;
}

static float3 VortexSampleTransmittanceOpticalDepthLut(
    uint lut_slot,
    float lut_width,
    float lut_height,
    float cos_zenith,
    float altitude_m,
    float planet_radius_m,
    float atmosphere_height_m)
{
    if (lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        return 0.0f.xxx;
    }

    const float cos_horizon = VortexHorizonCosineFromAltitude(planet_radius_m, altitude_m);
    if (cos_zenith < cos_horizon)
    {
        return float3(kVortexInfiniteOpticalDepth, kVortexInfiniteOpticalDepth, kVortexInfiniteOpticalDepth);
    }

    float2 uv = VortexGetTransmittanceLutUv(cos_zenith, altitude_m, planet_radius_m, atmosphere_height_m);
    uv = VortexApplyHalfTexelOffset(uv, lut_width, lut_height);

    Texture2D<float4> lut = ResourceDescriptorHeap[lut_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    return lut.SampleLevel(linear_sampler, uv, 0.0f).rgb;
}

static VortexSingleScatteringResult VortexIntegrateSingleScatteredLuminance(
    float3 world_pos,
    float3 world_dir,
    bool variable_sample_count,
    float sample_count_ini,
    float min_sample_count,
    float max_sample_count,
    float distance_to_sample_count_max_inv,
    float3 light0_dir,
    float3 light1_dir,
    float3 light0_illuminance,
    float3 light1_illuminance,
    float aerial_perspective_view_distance_scale,
    GpuSkyAtmosphereParams atmosphere,
    uint transmittance_lut_srv,
    float transmittance_width,
    float transmittance_height,
    Texture2D<float4> multi_scat_lut,
    SamplerState linear_sampler,
    float t_max_max)
{
    VortexSingleScatteringResult result;
    result.L = 0.0f.xxx;
    result.OpticalDepth = 0.0f.xxx;
    result.Transmittance = 1.0f.xxx;

    const float atmosphere_radius = atmosphere.planet_radius_m + atmosphere.atmosphere_height_m;
    float t_max = 0.0f;
    const float2 top_hits = RayIntersectSphere(world_pos, world_dir, float4(0.0f, 0.0f, 0.0f, atmosphere_radius));
    const float2 bottom_hits = RayIntersectSphere(world_pos, world_dir, float4(0.0f, 0.0f, 0.0f, atmosphere.planet_radius_m));
    const bool no_top_intersection = all(top_hits < 0.0f);
    const bool no_bottom_intersection = all(bottom_hits < 0.0f);
    if (no_top_intersection)
    {
        t_max = 0.0f;
    }
    else if (no_bottom_intersection)
    {
        // Match UE: when the ray does not hit the ground, march to the far top-atmosphere hit.
        t_max = max(top_hits.x, top_hits.y);
    }
    else
    {
        const float t_bottom = max(0.0f, min(bottom_hits.x, bottom_hits.y));
        t_max = t_bottom;
    }
    t_max = min(t_max, t_max_max);
    if (t_max <= 1.0e-4f)
    {
        return result;
    }

    float sample_count = sample_count_ini;
    if (variable_sample_count)
    {
        const float t = saturate(t_max * distance_to_sample_count_max_inv);
        sample_count = lerp(min_sample_count, max_sample_count, t);
    }

    const uint sample_count_u = max(1u, (uint)sample_count);
    const float dt = t_max / float(sample_count_u);
    const float cos_theta0 = dot(world_dir, light0_dir);
    const float cos_theta1 = dot(world_dir, light1_dir);
    const float rayleigh_phase0 = VortexRayleighPhase(cos_theta0);
    const float mie_phase0 = VortexCornetteShanksMiePhase(cos_theta0, atmosphere.mie_g);
    const float rayleigh_phase1 = VortexRayleighPhase(cos_theta1);
    const float mie_phase1 = VortexCornetteShanksMiePhase(cos_theta1, atmosphere.mie_g);

    float3 throughput = 1.0f.xxx;
    float3 optical_depth = 0.0f.xxx;

    [loop]
    for (uint i = 0u; i < sample_count_u; ++i)
    {
        const float t = (float(i) + kVortexSegmentSampleOffset) * dt;
        const float3 sample_pos = world_pos + world_dir * t;
        const float altitude_m = max(length(sample_pos) - atmosphere.planet_radius_m, 0.0f);
        if (altitude_m > atmosphere.atmosphere_height_m)
        {
            continue;
        }

        const float d_r = VortexAtmosphereExponentialDensity(altitude_m, atmosphere.rayleigh_scale_height_m);
        const float d_m = VortexAtmosphereExponentialDensity(altitude_m, atmosphere.mie_scale_height_m);
        const float d_a = VortexOzoneAbsorptionDensity(altitude_m, atmosphere.absorption_density);

        const float3 extinction = atmosphere.rayleigh_scattering_rgb * d_r
            + atmosphere.mie_extinction_rgb * d_m
            + atmosphere.absorption_rgb * d_a;
        const float3 optical_depth_step = float3(d_r, d_m, d_a) * dt;
        const float3 sample_transmittance = exp(-(extinction * dt));

        const float3 sample_up = normalize(sample_pos);
        const float cos_sun_zenith0 = dot(sample_up, light0_dir);
        const float cos_sun_zenith1 = dot(sample_up, light1_dir);
        const float3 sun_od0 = VortexSampleTransmittanceOpticalDepthLut(
            transmittance_lut_srv, transmittance_width, transmittance_height,
            cos_sun_zenith0, altitude_m, atmosphere.planet_radius_m, atmosphere.atmosphere_height_m);
        const float3 sun_od1 = VortexSampleTransmittanceOpticalDepthLut(
            transmittance_lut_srv, transmittance_width, transmittance_height,
            cos_sun_zenith1, altitude_m, atmosphere.planet_radius_m, atmosphere.atmosphere_height_m);
        const float3 sun_t0 = VortexTransmittanceFromOpticalDepth(sun_od0, atmosphere);
        const float3 sun_t1 = VortexTransmittanceFromOpticalDepth(sun_od1, atmosphere);

        const float3 sigma_s0 = (atmosphere.rayleigh_scattering_rgb * d_r * rayleigh_phase0
            + atmosphere.mie_scattering_rgb * d_m * mie_phase0) * sun_t0 * light0_illuminance;
        const float3 sigma_s1 = (atmosphere.rayleigh_scattering_rgb * d_r * rayleigh_phase1
            + atmosphere.mie_scattering_rgb * d_m * mie_phase1) * sun_t1 * light1_illuminance;

        const float2 uv_ms0 = saturate(float2(cos_sun_zenith0 * 0.5f + 0.5f, altitude_m / max(atmosphere.atmosphere_height_m, 1.0f)));
        const float2 uv_ms1 = saturate(float2(cos_sun_zenith1 * 0.5f + 0.5f, altitude_m / max(atmosphere.atmosphere_height_m, 1.0f)));
        const float4 ms0 = multi_scat_lut.SampleLevel(linear_sampler, uv_ms0, 0.0f);
        const float4 ms1 = multi_scat_lut.SampleLevel(linear_sampler, uv_ms1, 0.0f);
        // The multi-scattering LUT already stores luminance with both the
        // geometric series 1/(1-R) and multi_scattering_factor baked in.
        // Match UE5: just multiply by scattering coefficient and illuminance.
        const float3 sigma_ms0 = (atmosphere.rayleigh_scattering_rgb * d_r + atmosphere.mie_scattering_rgb * d_m)
            * ms0.rgb * light0_illuminance;
        const float3 sigma_ms1 = (atmosphere.rayleigh_scattering_rgb * d_r + atmosphere.mie_scattering_rgb * d_m)
            * ms1.rgb * light1_illuminance;

        const float3 source = sigma_s0 + sigma_s1 + sigma_ms0 + sigma_ms1;
        float3 sint;
        if (all(extinction < 1.0e-6f.xxx))
        {
            sint = source * dt;
        }
        else
        {
            sint = (source - source * sample_transmittance) / max(extinction, 1.0e-6f.xxx);
        }

        result.L += throughput * sint;
        throughput *= sample_transmittance;
        optical_depth += optical_depth_step;
    }

    result.L = min(result.L, kVortexFp16SafeMax.xxx);
    result.OpticalDepth = optical_depth;
    result.Transmittance = throughput;
    return result;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREUEMIRRORCOMMON_HLSLI

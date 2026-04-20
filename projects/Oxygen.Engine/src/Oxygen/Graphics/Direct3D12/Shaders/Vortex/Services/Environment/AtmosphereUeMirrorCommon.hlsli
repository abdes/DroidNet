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
static const float kVortexPlanetRadiusOffsetM = 1.0f;

struct VortexSingleScatteringResult
{
    float3 L;
    float3 LMieOnly;
    float3 LRayOnly;
    float3 OpticalDepth;
    float3 Transmittance;
    float3 TransmittanceMieOnly;
    float3 TransmittanceRayOnly;
    float3 MultiScatAs1;
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

static float VortexRayleighPhase(float cosine_angle)
{
    return (3.0f / (16.0f * kVortexSkyPi)) * (1.0f + cosine_angle * cosine_angle);
}

static float VortexHenyeyGreensteinPhase(float anisotropy, float cosine_angle)
{
    const float g = clamp(anisotropy, -0.99f, 0.99f);
    const float denom = max(1.0f + g * g - 2.0f * g * cosine_angle, 1.0e-5f);
    return ((1.0f - g * g) / (4.0f * kVortexSkyPi)) / pow(denom, 1.5f);
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

static float VortexRaySphereIntersectNearestOffset(
    float3 origin,
    float3 dir,
    float3 center,
    float radius)
{
    const float2 hits = RayIntersectSphere(origin, dir, float4(center, radius));
    if (hits.x > 0.0f)
    {
        return hits.x;
    }
    if (hits.y > 0.0f)
    {
        return hits.y;
    }
    return -1.0f;
}

static float3 VortexSampleTransmittanceLut(
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
        return 1.0f.xxx;
    }

    const float cos_horizon = VortexHorizonCosineFromAltitude(planet_radius_m, altitude_m);
    if (cos_zenith < cos_horizon)
    {
        return 0.0f.xxx;
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
    bool ground,
    bool mie_ray_phase,
    bool multi_scattering_approx_sampling_enabled,
    bool variable_sample_count,
    float sample_count_ini,
    float min_sample_count,
    float max_sample_count,
    float distance_to_sample_count_max_inv,
    float3 light0_dir,
    float3 light1_dir,
    float3 light0_illuminance,
    float3 light1_illuminance,
    float output_pre_exposure,
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
    result.LMieOnly = 0.0f.xxx;
    result.LRayOnly = 0.0f.xxx;
    result.OpticalDepth = 0.0f.xxx;
    result.Transmittance = 1.0f.xxx;
    result.TransmittanceMieOnly = 1.0f.xxx;
    result.TransmittanceRayOnly = 1.0f.xxx;
    result.MultiScatAs1 = 0.0f.xxx;

    if (dot(world_pos, world_pos) <= atmosphere.planet_radius_m * atmosphere.planet_radius_m)
    {
        return result;
    }

    const float atmosphere_radius = atmosphere.planet_radius_m + atmosphere.atmosphere_height_m;
    float t_max = 0.0f;
    float t_bottom = 0.0f;
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
        t_bottom = max(0.0f, min(bottom_hits.x, bottom_hits.y));
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

    float sample_count_floor = sample_count;
    float t_max_floor = t_max;
    if (variable_sample_count)
    {
        sample_count_floor = floor(sample_count);
        sample_count_floor = max(sample_count_floor, 1.0f);
        t_max_floor = t_max * sample_count_floor / max(sample_count, 1.0f);
    }

    const uint sample_count_u = max(1u, (uint)ceil(sample_count));
    float dt = t_max / max(sample_count, 1.0f);
    const float cos_theta0 = dot(world_dir, light0_dir);
    const float rayleigh_phase0 = VortexRayleighPhase(cos_theta0);
    const float mie_phase0 = VortexHenyeyGreensteinPhase(atmosphere.mie_g, -cos_theta0);
    const float uniform_phase = 1.0f / (4.0f * kVortexSkyPi);
    const bool second_light_enabled = any(light1_illuminance > 1.0e-6f.xxx);
    float rayleigh_phase1 = 0.0f;
    float mie_phase1 = 0.0f;
    if (second_light_enabled)
    {
        const float cos_theta1 = dot(world_dir, light1_dir);
        rayleigh_phase1 = VortexRayleighPhase(cos_theta1);
        mie_phase1 = VortexHenyeyGreensteinPhase(atmosphere.mie_g, -cos_theta1);
    }

    float3 throughput = 1.0f.xxx;
    float3 throughput_mie_only = 1.0f.xxx;
    float3 throughput_ray_only = 1.0f.xxx;
    float3 optical_depth = 0.0f.xxx;
    const float3 exposed_light0_illuminance = light0_illuminance * output_pre_exposure;
    const float3 exposed_light1_illuminance = light1_illuminance * output_pre_exposure;

    [loop]
    for (uint i = 0u; i < sample_count_u; ++i)
    {
        float t = 0.0f;
        if (variable_sample_count)
        {
            float t0 = float(i) / sample_count_floor;
            float t1 = (float(i) + 1.0f) / sample_count_floor;
            t0 = t0 * t0;
            t1 = t1 * t1;
            t0 = t_max_floor * t0;
            if (t1 > 1.0f)
            {
                t1 = t_max;
            }
            else
            {
                t1 = t_max_floor * t1;
            }
            t = t0 + (t1 - t0) * kVortexSegmentSampleOffset;
            dt = t1 - t0;
        }
        else
        {
            t = t_max * (float(i) + kVortexSegmentSampleOffset) / max(sample_count, 1.0f);
        }
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
        const float distance_scale = max(aerial_perspective_view_distance_scale, 0.0f);
        const float3 optical_depth_step = extinction * dt * distance_scale;
        const float3 sample_transmittance = exp(-(extinction * dt * distance_scale));
        throughput_mie_only *= exp(-((atmosphere.mie_extinction_rgb * d_m + atmosphere.absorption_rgb * d_a) * dt * distance_scale));
        throughput_ray_only *= exp(-((atmosphere.rayleigh_scattering_rgb * d_r + atmosphere.absorption_rgb * d_a) * dt * distance_scale));

        const float3 sample_up = normalize(sample_pos);
        const float cos_sun_zenith0 = dot(sample_up, light0_dir);
        const float3 sun_t0 = VortexSampleTransmittanceLut(
            transmittance_lut_srv, transmittance_width, transmittance_height,
            cos_sun_zenith0, altitude_m, atmosphere.planet_radius_m, atmosphere.atmosphere_height_m);

        const float3 phase_times_scattering0 = mie_ray_phase
            ? (atmosphere.rayleigh_scattering_rgb * d_r * rayleigh_phase0
                + atmosphere.mie_scattering_rgb * d_m * mie_phase0)
            : ((atmosphere.rayleigh_scattering_rgb * d_r + atmosphere.mie_scattering_rgb * d_m) * uniform_phase);
        const float3 phase_times_scattering0_mie_only = mie_ray_phase
            ? (atmosphere.mie_scattering_rgb * d_m * mie_phase0)
            : (atmosphere.mie_scattering_rgb * d_m * uniform_phase);
        const float3 phase_times_scattering0_ray_only = mie_ray_phase
            ? (atmosphere.rayleigh_scattering_rgb * d_r * rayleigh_phase0)
            : (atmosphere.rayleigh_scattering_rgb * d_r * uniform_phase);
        const float3 sigma_s0 = phase_times_scattering0 * sun_t0 * exposed_light0_illuminance;
        float3 sigma_s1 = 0.0f.xxx;
        const float t_planet0 = VortexRaySphereIntersectNearestOffset(
            sample_pos,
            light0_dir,
            sample_up * kVortexPlanetRadiusOffsetM,
            atmosphere.planet_radius_m);
        const float planet_shadow0 = t_planet0 >= 0.0f ? 0.0f : 1.0f;
        float planet_shadow1 = 0.0f;

        float4 ms0 = 0.0f.xxxx;
        if (multi_scattering_approx_sampling_enabled)
        {
            const float2 uv_ms0 = saturate(float2(cos_sun_zenith0 * 0.5f + 0.5f, altitude_m / max(atmosphere.atmosphere_height_m, 1.0f)));
            ms0 = multi_scat_lut.SampleLevel(linear_sampler, uv_ms0, 0.0f);
        }
        // The multi-scattering LUT already stores luminance with both the
        // geometric series 1/(1-R) and multi_scattering_factor baked in.
        // Match UE5: just multiply by scattering coefficient and illuminance.
        const float3 sigma_ms0 = (atmosphere.rayleigh_scattering_rgb * d_r + atmosphere.mie_scattering_rgb * d_m)
            * ms0.rgb * exposed_light0_illuminance;
        const float3 sigma_mie_only_ms0 = atmosphere.mie_scattering_rgb * d_m * ms0.rgb * exposed_light0_illuminance;
        const float3 sigma_ray_only_ms0 = atmosphere.rayleigh_scattering_rgb * d_r * ms0.rgb * exposed_light0_illuminance;
        float3 sigma_ms1 = 0.0f.xxx;
        float3 sigma_mie_only_ms1 = 0.0f.xxx;
        float3 sigma_ray_only_ms1 = 0.0f.xxx;
        if (second_light_enabled)
        {
            const float cos_sun_zenith1 = dot(sample_up, light1_dir);
            const float3 sun_t1 = VortexSampleTransmittanceLut(
                transmittance_lut_srv, transmittance_width, transmittance_height,
                cos_sun_zenith1, altitude_m, atmosphere.planet_radius_m, atmosphere.atmosphere_height_m);
            const float3 phase_times_scattering1 = mie_ray_phase
                ? (atmosphere.rayleigh_scattering_rgb * d_r * rayleigh_phase1
                    + atmosphere.mie_scattering_rgb * d_m * mie_phase1)
                : ((atmosphere.rayleigh_scattering_rgb * d_r + atmosphere.mie_scattering_rgb * d_m) * uniform_phase);
            sigma_s1 = phase_times_scattering1 * sun_t1 * exposed_light1_illuminance;
            const float t_planet1 = VortexRaySphereIntersectNearestOffset(
                sample_pos,
                light1_dir,
                sample_up * kVortexPlanetRadiusOffsetM,
                atmosphere.planet_radius_m);
            planet_shadow1 = t_planet1 >= 0.0f ? 0.0f : 1.0f;
            float4 ms1 = 0.0f.xxxx;
            if (multi_scattering_approx_sampling_enabled)
            {
                const float2 uv_ms1 = saturate(float2(cos_sun_zenith1 * 0.5f + 0.5f, altitude_m / max(atmosphere.atmosphere_height_m, 1.0f)));
                ms1 = multi_scat_lut.SampleLevel(linear_sampler, uv_ms1, 0.0f);
            }
            sigma_ms1 = (atmosphere.rayleigh_scattering_rgb * d_r + atmosphere.mie_scattering_rgb * d_m)
                * ms1.rgb * exposed_light1_illuminance;
            sigma_mie_only_ms1 = atmosphere.mie_scattering_rgb * d_m * ms1.rgb * exposed_light1_illuminance;
            sigma_ray_only_ms1 = atmosphere.rayleigh_scattering_rgb * d_r * ms1.rgb * exposed_light1_illuminance;
        }

        const float3 source = planet_shadow0 * sigma_s0 + planet_shadow1 * sigma_s1 + sigma_ms0 + sigma_ms1;
        const float3 source_mie_only =
            planet_shadow0 * (phase_times_scattering0_mie_only * sun_t0 * exposed_light0_illuminance)
            + sigma_mie_only_ms0
            + (second_light_enabled
                ? planet_shadow1 * ((mie_ray_phase
                    ? (atmosphere.mie_scattering_rgb * d_m * mie_phase1)
                    : (atmosphere.mie_scattering_rgb * d_m * uniform_phase))
                    * VortexSampleTransmittanceLut(
                        transmittance_lut_srv, transmittance_width, transmittance_height,
                        dot(sample_up, light1_dir), altitude_m, atmosphere.planet_radius_m, atmosphere.atmosphere_height_m)
                    * exposed_light1_illuminance)
                    + sigma_mie_only_ms1
                : 0.0f.xxx);
        const float3 source_ray_only =
            planet_shadow0 * (phase_times_scattering0_ray_only * sun_t0 * exposed_light0_illuminance)
            + sigma_ray_only_ms0
            + (second_light_enabled
                ? planet_shadow1 * ((mie_ray_phase
                    ? (atmosphere.rayleigh_scattering_rgb * d_r * rayleigh_phase1)
                    : (atmosphere.rayleigh_scattering_rgb * d_r * uniform_phase))
                    * VortexSampleTransmittanceLut(
                        transmittance_lut_srv, transmittance_width, transmittance_height,
                        dot(sample_up, light1_dir), altitude_m, atmosphere.planet_radius_m, atmosphere.atmosphere_height_m)
                    * exposed_light1_illuminance)
                    + sigma_ray_only_ms1
                : 0.0f.xxx);
        result.MultiScatAs1 += throughput * (atmosphere.rayleigh_scattering_rgb * d_r + atmosphere.mie_scattering_rgb * d_m) * dt;
        float3 sint;
        float3 sint_mie_only;
        float3 sint_ray_only;
        if (all(extinction < 1.0e-6f.xxx))
        {
            sint = source * dt;
            sint_mie_only = source_mie_only * dt;
            sint_ray_only = source_ray_only * dt;
        }
        else
        {
            sint = (source - source * sample_transmittance) / max(extinction, 1.0e-6f.xxx);
            sint_mie_only = (source_mie_only - source_mie_only * sample_transmittance) / max(extinction, 1.0e-6f.xxx);
            sint_ray_only = (source_ray_only - source_ray_only * sample_transmittance) / max(extinction, 1.0e-6f.xxx);
        }

        result.L += throughput * sint;
        result.LMieOnly += throughput * sint_mie_only;
        result.LRayOnly += throughput * sint_ray_only;
        throughput *= sample_transmittance;
        optical_depth += optical_depth_step;
    }

    if (ground && t_max == t_bottom)
    {
        const float3 ground_pos = world_pos + t_bottom * world_dir;
        const float ground_height = length(ground_pos);
        const float3 up_vector = ground_pos / max(ground_height, 1.0e-6f);
        const float light0_zenith_cos_angle = dot(light0_dir, up_vector);
        const float3 transmittance_to_light0 = VortexSampleTransmittanceLut(
            transmittance_lut_srv,
            transmittance_width,
            transmittance_height,
            light0_zenith_cos_angle,
            max(ground_height - atmosphere.planet_radius_m, 0.0f),
            atmosphere.planet_radius_m,
            atmosphere.atmosphere_height_m);
        const float n_dot_l0 = saturate(dot(up_vector, light0_dir));
        result.L += exposed_light0_illuminance * transmittance_to_light0 * throughput * n_dot_l0
            * atmosphere.ground_albedo_rgb / kVortexSkyPi;

        if (second_light_enabled)
        {
            const float light1_zenith_cos_angle = dot(light1_dir, up_vector);
            const float3 transmittance_to_light1 = VortexSampleTransmittanceLut(
                transmittance_lut_srv,
                transmittance_width,
                transmittance_height,
                light1_zenith_cos_angle,
                max(ground_height - atmosphere.planet_radius_m, 0.0f),
                atmosphere.planet_radius_m,
                atmosphere.atmosphere_height_m);
            const float n_dot_l1 = saturate(dot(up_vector, light1_dir));
            result.L += exposed_light1_illuminance * transmittance_to_light1 * throughput * n_dot_l1
                * atmosphere.ground_albedo_rgb / kVortexSkyPi;
        }
    }

    result.L = min(result.L, kVortexFp16SafeMax.xxx);
    result.LMieOnly = min(result.LMieOnly, kVortexFp16SafeMax.xxx);
    result.LRayOnly = min(result.LRayOnly, kVortexFp16SafeMax.xxx);
    result.OpticalDepth = optical_depth;
    result.Transmittance = throughput;
    result.TransmittanceMieOnly = throughput_mie_only;
    result.TransmittanceRayOnly = throughput_ray_only;
    return result;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_ATMOSPHEREUEMIRRORCOMMON_HLSLI

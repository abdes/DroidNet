//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Multiple Scattering LUT Compute Shader
//!
//! Precomputes the total amount of light that escapes a point after infinite
//! scattering events (approximation).
//! Output: RGBA16F texture where:
//!   RGB = second-order scattering (used to approximate infinite bounces)
//!   A   = average transmittance
//!
//! UV Parameterization:
//!   u = (cos_sun_zenith + 1) / 2
//!   v = altitude / atmosphere_height

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/SceneConstants.hlsli"
#include "Common/Math.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Coordinates.hlsli"

// Root constants (b2, space0)
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Pass constants for MultiScat LUT generation
struct MultiScatLutPassConstants
{
    uint output_uav_index;
    uint transmittance_srv_index;
    uint output_width;
    uint output_height;

    uint transmittance_width;
    uint transmittance_height;
    float atmosphere_height_m;
    float planet_radius_m;
};


#define THREAD_GROUP_SIZE 8

// Re-use logic from common headers - these will move to AtmosphereMath.hlsli
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

float RayleighPhase(float cos_theta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

float HenyeyGreensteinPhase(float cos_theta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cos_theta;
    return (1.0 / FOUR_PI) * (1.0 - g2) / (denom * sqrt(denom));
}

float3 TransmittanceFromOpticalDepth(float3 optical_depth, GpuSkyAtmosphereParams atmo)
{
    float3 beta_rayleigh = atmo.rayleigh_scattering_rgb;
    float3 beta_mie_ext = atmo.mie_scattering_rgb / 0.9;
    float3 beta_abs = atmo.absorption_rgb;
    float3 tau = beta_rayleigh * optical_depth.x + beta_mie_ext * optical_depth.y + beta_abs * optical_depth.z;
    return exp(-tau);
}

float3 SampleTransmittanceLut(
    float altitude,
    float cos_zenith,
    GpuSkyAtmosphereParams atmo,
    Texture2D<float4> transmittance_lut,
    SamplerState linear_sampler)
{
    float u = (cos_zenith + 0.15) / 1.15;
    float v = sqrt(altitude / atmo.atmosphere_height_m);
    return transmittance_lut.SampleLevel(linear_sampler, float2(u, v), 0).rgb;
}

[numthreads(THREAD_GROUP_SIZE, THREAD_GROUP_SIZE, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    ConstantBuffer<MultiScatLutPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    if (dispatch_thread_id.x >= pass_constants.output_width
        || dispatch_thread_id.y >= pass_constants.output_height)
    {
        return;
    }

    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        return;
    }

    GpuSkyAtmosphereParams atmo = env_data.atmosphere;
    Texture2D<float4> transmittance_lut = ResourceDescriptorHeap[pass_constants.transmittance_srv_index];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    // Map UV to Sun Zenith and Altitude
    float2 uv = (float2(dispatch_thread_id.xy) + 0.5) / float2(pass_constants.output_width, pass_constants.output_height);
    float cos_sun_zenith = uv.x * 2.0 - 1.0;
    float altitude = uv.y * atmo.atmosphere_height_m;

    float3 sun_dir = float3(sqrt(max(0.0, 1.0 - cos_sun_zenith * cos_sun_zenith)), 0.0, cos_sun_zenith);
    float r = atmo.planet_radius_m + altitude;
    float3 origin = float3(0.0, 0.0, r);

    // Integral over all directions (Sphere)
    const uint SAMPLES_COUNT = 64;
    float3 multi_scat_sum = 0;
    float3 f_ms_sum = 0;

    for (uint i = 0; i < SAMPLES_COUNT; ++i)
    {
        // Fibonacci sphere sampling
        float phi = TWO_PI * (i + 0.5) / SAMPLES_COUNT;
        float cos_theta = 1.0 - 2.0 * (i + 0.5) / SAMPLES_COUNT;
        float sin_theta = sqrt(max(0.0, 1.0 - cos_theta * cos_theta));
        float3 view_dir = float3(sin_theta * cos(phi), sin_theta * sin(phi), cos_theta);

        // Raymarch this direction
        float atmosphere_radius = atmo.planet_radius_m + atmo.atmosphere_height_m;
        float b = dot(origin, view_dir);
        float c = dot(origin, origin) - atmosphere_radius * atmosphere_radius;
        float det = b * b - c;
        float ray_length = (det < 0.0) ? -1.0 : -b + sqrt(det);

        float ground_dist = -dot(origin, view_dir) - sqrt(max(0.0, dot(origin, view_dir) * dot(origin, view_dir) - (dot(origin, origin) - atmo.planet_radius_m * atmo.planet_radius_m)));
        if (ground_dist > 0.0 && (ray_length < 0.0 || ground_dist < ray_length))
        {
            ray_length = ground_dist;
        }

        if (ray_length > 0.0)
        {
            const uint STEPS = 16;
            float3 inscatter = 0;
            float3 accumulated_od = 0;
            float step_size = ray_length / STEPS;

            for (uint j = 0; j < STEPS; ++j)
            {
                float3 p = origin + view_dir * (j + 0.5) * step_size;
                float h = length(p) - atmo.planet_radius_m;

                float d_r = GetAtmosphereDensity(h, atmo.rayleigh_scale_height_m);
                float d_m = GetAtmosphereDensity(h, atmo.mie_scale_height_m);
                float d_a = GetAbsorptionDensity(h, atmo.absorption_scale_height_m);

                float3 od_step = float3(d_r, d_m, d_a) * step_size;
                float3 view_transmittance = TransmittanceFromOpticalDepth(accumulated_od + od_step * 0.5, atmo);

                // --- Sun Transmittance to this point ---
                // We need to know how much sun light reaches this scattering point.
                float3 p_dir = normalize(p);
                float cos_sun_p = dot(p_dir, sun_dir);
                float3 sun_od = SampleTransmittanceLut(h, cos_sun_p, atmo, transmittance_lut, linear_sampler);
                float3 sun_transmittance = TransmittanceFromOpticalDepth(sun_od, atmo);

                // Scattering event: SunLight * Transmittance * ScatteringCoeff
                // Note: We use isotropic phase 1/4PI for the second bounce precompute.
                float3 sigma_s = (atmo.rayleigh_scattering_rgb * d_r + atmo.mie_scattering_rgb * d_m);
                inscatter += sun_transmittance * view_transmittance * sigma_s * step_size;

                accumulated_od += od_step;
            }

            // --- Ground Bounce contribution ---
            // If the ray hit the ground, add the light reflected from the ground
            // back into the atmosphere.
            if (ground_dist > 0.0 && (ray_length < 0.0 || ground_dist <= ray_length))
            {
                float3 ground_pos = origin + view_dir * ground_dist;
                float3 ground_normal = normalize(ground_pos);
                float ground_ndotl = max(0.0, dot(ground_normal, sun_dir));

                // Sun transmittance to the ground point
                float3 ground_sun_od = SampleTransmittanceLut(0.0, dot(ground_normal, sun_dir), atmo, transmittance_lut, linear_sampler);
                float3 ground_sun_transmittance = TransmittanceFromOpticalDepth(ground_sun_od, atmo);

                float3 view_transmittance = TransmittanceFromOpticalDepth(accumulated_od, atmo);

                // Lambertian ground bounce: (Albedo / PI) * N.L * SunTransmittance * ViewTransmittance
                inscatter += (atmo.ground_albedo_rgb / PI) * ground_ndotl * ground_sun_transmittance * view_transmittance;
            }

            // Average scattering over the sphere (1/4PI)
            multi_scat_sum += inscatter * (1.0 / (4.0 * PI));
            f_ms_sum += (1.0 - TransmittanceFromOpticalDepth(accumulated_od, atmo)) * (1.0 / (4.0 * PI));
        }
    }

    multi_scat_sum /= SAMPLES_COUNT;
    f_ms_sum /= SAMPLES_COUNT;

    // Output RGBA
    RWTexture2D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
    output[dispatch_thread_id.xy] = float4(multi_scat_sum, f_ms_sum.x);
}

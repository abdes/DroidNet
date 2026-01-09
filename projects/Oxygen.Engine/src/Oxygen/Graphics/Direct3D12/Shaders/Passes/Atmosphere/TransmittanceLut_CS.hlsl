//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Transmittance LUT Compute Shader
//!
//! Precomputes optical depth integrals for a planet's atmosphere.
//! Output: RGBA16F texture where RGB = optical depth for
//!   Rayleigh, Mie, absorption (ozone-like). Alpha is reserved.
//!
//! UV Parameterization:
//!   u = normalized cos_zenith: (cos_zenith + 0.15) / 1.15
//!   v = normalized altitude: sqrt(altitude / atmosphere_height)
//!
//! === Bindless Discipline ===
//! - All resources accessed via SM 6.6 descriptor heaps
//! - SceneConstants at b1, RootConstants at b2, EnvironmentDynamicData at b3

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/SceneConstants.hlsli"

// Root constants (b2, space0)
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Pass constants for transmittance LUT generation
struct TransmittanceLutPassConstants
{
    uint output_uav_index;      // UAV index for output RWTexture2D<float4>
    uint output_width;          // LUT width
    uint output_height;         // LUT height
    uint _pad0;
};

// Thread group size: 8x8 threads per group
#define THREAD_GROUP_SIZE_X 8
#define THREAD_GROUP_SIZE_Y 8

// Number of integration samples along the ray
static const uint NUM_INTEGRATION_SAMPLES = 40;

//! Converts UV coordinates to altitude and cos_zenith.
//!
//! @param uv Normalized texture coordinates [0, 1].
//! @param atmosphere_height Atmosphere thickness in meters.
//! @return (altitude, cos_zenith) in meters and [-1, 1].
float2 UvToAtmosphereParams(float2 uv, float atmosphere_height)
{
    // Invert the UV parameterization
    // v = sqrt(altitude / atmosphere_height)
    // => altitude = v^2 * atmosphere_height
    float altitude = uv.y * uv.y * atmosphere_height;

    // u = (cos_zenith + 0.15) / 1.15
    // => cos_zenith = u * 1.15 - 0.15
    float cos_zenith = uv.x * 1.15 - 0.15;
    cos_zenith = clamp(cos_zenith, -1.0, 1.0);

    return float2(altitude, cos_zenith);
}

//! Computes atmospheric density at a given altitude.
//!
//! @param altitude Altitude above ground in meters.
//! @param scale_height Atmospheric scale height in meters.
//! @return Density ratio relative to ground level.
float GetAtmosphereDensity(float altitude, float scale_height)
{
    return exp(-altitude / scale_height);
}

//! Simple ozone-like absorption density profile.
//!
//! We model absorption as a finite layer centered at absorption_scale_height_m
//! (typically ~25km for Earth) with a fixed relative width.
float GetAbsorptionDensity(float altitude, float absorption_center_m)
{
    // Clamp below ground.
    altitude = max(altitude, 0.0);

    // Treat the authored value as the layer center.
    float center = max(1.0, absorption_center_m);

    // Empirical width: wide enough to cover roughly 10..35 km on Earth.
    float width = max(1000.0, center * 0.6);

    float t = 1.0 - abs(altitude - center) / width;
    return saturate(t);
}

//! Computes ray-sphere intersection distance.
//!
//! @param origin Ray origin (relative to planet center).
//! @param dir Ray direction (normalized).
//! @param radius Sphere radius.
//! @return Distance to sphere, or -1 if no intersection.
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

    // Return the positive intersection (exit point for inside, entry for outside)
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

//! Integrates optical depth along a ray through the atmosphere.
//!
//! @param origin Ray origin (position relative to planet center).
//! @param dir Ray direction (normalized, pointing upward/outward).
//! @param ray_length Distance to integrate.
//! @param atmo Atmosphere parameters.
//! @return (optical_depth_rayleigh, optical_depth_mie).
float3 IntegrateOpticalDepth(
    float3 origin,
    float3 dir,
    float ray_length,
    GpuSkyAtmosphereParams atmo)
{
    float3 optical_depth = float3(0.0, 0.0, 0.0);
    float step_size = ray_length / float(NUM_INTEGRATION_SAMPLES);

    for (uint i = 0; i < NUM_INTEGRATION_SAMPLES; ++i)
    {
        float t = (float(i) + 0.5) * step_size;
        float3 sample_pos = origin + dir * t;

        float altitude = length(sample_pos) - atmo.planet_radius_m;
        altitude = max(altitude, 0.0);

        float density_rayleigh = GetAtmosphereDensity(altitude, atmo.rayleigh_scale_height_m);
        float density_mie = GetAtmosphereDensity(altitude, atmo.mie_scale_height_m);
        float density_absorption = GetAbsorptionDensity(altitude, atmo.absorption_scale_height_m);

        optical_depth.x += density_rayleigh * step_size;
        optical_depth.y += density_mie * step_size;
        optical_depth.z += density_absorption * step_size;
    }

    return optical_depth;
}

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    // Load pass constants
    ConstantBuffer<TransmittanceLutPassConstants> pass_constants
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
        // Fallback: write zero optical depth
        RWTexture2D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
        output[dispatch_thread_id.xy] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    GpuSkyAtmosphereParams atmo = env_data.atmosphere;

    // Compute UV from texel center
    float2 uv = (float2(dispatch_thread_id.xy) + 0.5)
              / float2(pass_constants.output_width, pass_constants.output_height);

    // Convert UV to atmosphere parameters
    float2 atmo_params = UvToAtmosphereParams(uv, atmo.atmosphere_height_m);
    float altitude = atmo_params.x;
    float cos_zenith = atmo_params.y;

    // Compute ray origin and direction
    // Origin is at altitude above planet surface, on the Z-axis
    float r = atmo.planet_radius_m + altitude;
    float3 origin = float3(0.0, 0.0, r);

    // Direction is toward zenith angle
    float sin_zenith = sqrt(max(0.0, 1.0 - cos_zenith * cos_zenith));
    float3 dir = float3(sin_zenith, 0.0, cos_zenith);

    // Compute ray length to atmosphere top
    float atmosphere_radius = atmo.planet_radius_m + atmo.atmosphere_height_m;
    float ray_length = RaySphereIntersect(origin, dir, atmosphere_radius);

    float3 optical_depth = float3(0.0, 0.0, 0.0);

    if (ray_length > 0.0)
    {
        // Check if ray hits the ground
        float ground_dist = RaySphereIntersect(origin, dir, atmo.planet_radius_m);
        const float integrate_length
            = (ground_dist > 0.0 && ground_dist < ray_length) ? ground_dist : ray_length;

        // Integrate optical depth along the ray segment.
        optical_depth = IntegrateOpticalDepth(origin, dir, integrate_length, atmo);
    }

    // Write result
    RWTexture2D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
    output[dispatch_thread_id.xy] = float4(optical_depth, 0.0);
}

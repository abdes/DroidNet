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
//! UV Parameterization (UE5/Bruneton distance-based):
//!   u = x_mu mapping (distance to top)
//!   v = x_r mapping (altitude relative to top)
//!
//! === Bindless Discipline ===
//! - All resources accessed via SM 6.6 descriptor heaps
//! - SceneConstants at b1, RootConstants at b2, EnvironmentDynamicData at b3

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/SceneConstants.hlsli"
#include "Atmosphere/AtmosphereMedium.hlsli"
#include "Common/Math.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Coordinates.hlsli"

#include "Atmosphere/AtmospherePassConstants.hlsli"

// Root constants (b2, space0)
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Thread group size: 8x8 threads per group

// Thread group size: 8x8 threads per group
#define THREAD_GROUP_SIZE_X 8
#define THREAD_GROUP_SIZE_Y 8

// Number of integration samples along the ray
static const uint NUM_INTEGRATION_SAMPLES = 40;

//! Converts UV coordinates to altitude and cos_zenith.
//!
//! Uses the UE5/Bruneton distance-based parameterization inverse.
//! This MUST match the forward mapping in GetTransmittanceLutUv()
//! (AtmosphereSampling.hlsli) exactly.
//!
//! Forward mapping (for reference):
//!   x_r = rho / H  where rho = sqrt(r² - R²), H = sqrt(R_top² - R²)
//!   x_mu = (d - d_min) / (d_max - d_min)  where d = distance to atmosphere top
//!
//! Inverse:
//!   rho = x_r * H  →  altitude = sqrt(rho² + R²) - R
//!   d = x_mu * (d_max - d_min) + d_min  →  solve quadratic for cos_zenith
//!
//! @param uv Normalized texture coordinates [0, 1].
//! @param planet_radius Planet radius in meters.
//! @param atmosphere_height Atmosphere thickness in meters.
//! @return (altitude_m, cos_zenith) in meters and [-1, 1].
float2 UvToAtmosphereParamsBruneton(
    float2 uv,
    float planet_radius,
    float atmosphere_height)
{
    float top_radius = planet_radius + atmosphere_height;

    // H = maximum rho (at atmosphere top)
    float H = sqrt(max(0.0, top_radius * top_radius - planet_radius * planet_radius));

    // Invert x_r mapping: rho = x_r * H
    float rho = uv.y * H;

    // view_height = sqrt(rho² + planet_radius²)
    float view_height = sqrt(rho * rho + planet_radius * planet_radius);
    float altitude_m = view_height - planet_radius;

    // Invert x_mu mapping
    // d_min = top_radius - view_height
    // d_max = rho + H
    // d = x_mu * (d_max - d_min) + d_min
    float d_min = top_radius - view_height;
    float d_max = rho + H;
    float d = uv.x * (d_max - d_min) + d_min;

    // Solve for cos_zenith from the distance equation:
    // d = -view_height * cos_zenith + sqrt(view_height² * (cos_zenith² - 1) + top_radius²)
    //
    // Let c = cos_zenith
    // d + view_height * c = sqrt(view_height² * c² - view_height² + top_radius²)
    // (d + view_height * c)² = view_height² * c² - view_height² + top_radius²
    // d² + 2*d*view_height*c + view_height²*c² = view_height²*c² - view_height² + top_radius²
    // d² + 2*d*view_height*c = top_radius² - view_height²
    // 2*d*view_height*c = top_radius² - view_height² - d²
    // c = (top_radius² - view_height² - d²) / (2 * d * view_height)
    //
    // Handle edge case where d ≈ 0 or view_height ≈ 0
    float cos_zenith;
    float denom = 2.0 * d * view_height;
    if (abs(denom) < 1e-6)
    {
        // At the very top of atmosphere looking straight up
        cos_zenith = 1.0;
    }
    else
    {
        cos_zenith = (top_radius * top_radius - view_height * view_height - d * d) / denom;
    }
    cos_zenith = clamp(cos_zenith, -1.0, 1.0);

    return float2(altitude_m, cos_zenith);
}


//! Integrates optical depth along a ray through the atmosphere.
//!
//! @param origin Ray origin (position relative to planet center).
//! @param dir Ray direction (normalized, pointing upward/outward).
//! @param ray_length Distance to integrate.
//! @param atmo Atmosphere parameters.
//! @return (optical_depth_rayleigh, optical_depth_mie, optical_depth_absorption).
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

        float altitude_m = length(sample_pos) - atmo.planet_radius_m;
        altitude_m = max(altitude_m, 0.0);

        float density_rayleigh = AtmosphereExponentialDensity(altitude_m, atmo.rayleigh_scale_height_m);
        float density_mie = AtmosphereExponentialDensity(altitude_m, atmo.mie_scale_height_m);
        float density_absorption = OzoneAbsorptionDensity(altitude_m, atmo.absorption_density);

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
    ConstantBuffer<AtmospherePassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    // Bounds check
    if (dispatch_thread_id.x >= pass_constants.output_extent.x
        || dispatch_thread_id.y >= pass_constants.output_extent.y)
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
              / float2(pass_constants.output_extent);

    // Convert UV to atmosphere parameters (UE5/Bruneton parameterization)
    float2 atmo_params = UvToAtmosphereParamsBruneton(
        uv,
        atmo.planet_radius_m,
        atmo.atmosphere_height_m);
    float altitude_m = atmo_params.x;
    float cos_zenith = atmo_params.y;

    // Compute ray origin and direction
    // Origin is at altitude above planet surface, on the Z-axis
    float r = atmo.planet_radius_m + altitude_m;
    float3 origin = float3(0.0, 0.0, r);

    // Direction is toward zenith angle
    float sin_zenith = sqrt(max(0.0, 1.0 - cos_zenith * cos_zenith));
    float3 dir = float3(sin_zenith, 0.0, cos_zenith);

    // Compute ray length to atmosphere top
    float atmosphere_radius = atmo.planet_radius_m + atmo.atmosphere_height_m;
    float ray_length = RaySphereIntersectNearest(origin, dir, atmosphere_radius);

    float3 optical_depth = float3(0.0, 0.0, 0.0);

    if (ray_length > 0.0)
    {
        // Check if ray hits the ground
        float ground_dist = RaySphereIntersectNearest(origin, dir, atmo.planet_radius_m);
        const float integrate_length
            = (ground_dist > 0.0 && ground_dist < ray_length) ? ground_dist : ray_length;

        // Integrate optical depth along the ray segment.
        optical_depth = IntegrateOpticalDepth(origin, dir, integrate_length, atmo);
    }

    // Write result
    RWTexture2D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
    output[dispatch_thread_id.xy] = float4(optical_depth, 0.0);
}

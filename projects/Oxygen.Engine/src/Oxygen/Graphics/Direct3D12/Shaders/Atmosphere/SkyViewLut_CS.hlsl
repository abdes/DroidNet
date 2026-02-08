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
#include "Atmosphere/AtmospherePhase.hlsli"
#include "Common/Math.hlsli"
#include "Atmosphere/AtmosphereConstants.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Coordinates.hlsli"
#include "Common/Lighting.hlsli"
#include "Atmosphere/AtmosphereSampling.hlsli"
#include "Atmosphere/IntegrateScatteredLuminance.hlsli"

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

//! Computes single-scattering inscatter along a view ray.
//!
//! @param origin Ray origin (position relative to planet center).
//! @param view_dir View direction (normalized).
//! @param sun_dir Sun direction (toward sun, normalized).
//! @param ray_length Distance to raymarch.
//! @param hits_ground True if the ray terminates at ground level.
//! @param atmo Atmosphere parameters.
//! @param transmittance_srv_index SRV index for transmittance LUT.
//! @param transmittance_width Transmittance LUT width.
//! @param transmittance_height Transmittance LUT height.
//! @param multi_scat_lut Multi-scatter LUT.
//! @param linear_sampler Linear sampler.
//! @param sun_illuminance Sun illuminance at the camera.
//! @return (inscattered_radiance.rgb, total_transmittance).
float4 ComputeSingleScattering(
    float3 origin,
    float3 view_dir,
    float3 sun_dir,
    float ray_length,
    bool hits_ground,
    GpuSkyAtmosphereParams atmo,
    uint transmittance_srv_index,
    float transmittance_width,
    float transmittance_height,
    Texture2D<float4> multi_scat_lut,
    SamplerState linear_sampler,
    float3 sun_illuminance)
{
    // Adaptive step count: more steps for long paths (horizon/sunset) to reduce banding.
    // Reference suggests 32, but for high quality we can go higher.
    const float min_steps = 32.0;
    const float max_steps = 64.0;
    float step_factor = saturate(ray_length / 200000.0);
    const uint num_steps = (uint)lerp(min_steps, max_steps, step_factor);

    // Use shared integration helper with quadratic sample distribution
    float3 throughput;
    float3 inscatter = IntegrateScatteredLuminanceQuadratic(
        origin, view_dir, ray_length, num_steps, atmo,
        sun_dir, sun_illuminance,
        transmittance_srv_index, transmittance_width, transmittance_height,
        multi_scat_lut, linear_sampler,
        throughput);

    // Ground albedo contribution: if the ray hits the ground, add reflected light
    if (hits_ground)
    {
        // Ground position is at end of ray
        float3 ground_pos = origin + view_dir * ray_length;
        float3 ground_normal = normalize(ground_pos);

        // Sun illumination on ground (Lambertian)
        float ground_ndotl = max(0.0, dot(ground_normal, sun_dir));

        // Transmittance from sun to ground
        float3 ground_sun_od = SampleTransmittanceOpticalDepthLut(
            transmittance_srv_index,
            transmittance_width,
            transmittance_height,
            ground_ndotl, 0.0,
            atmo.planet_radius_m,
            atmo.atmosphere_height_m);
        float3 ground_sun_transmittance = TransmittanceFromOpticalDepth(ground_sun_od, atmo);

        // Direct sun illumination on ground (Lambertian BRDF = albedo / PI)
        // UE5 Reference: Ground-hit uses direct Lambert only. Multi-scattering
        // contributions are handled by the MS pipeline, not via an extra ambient term.
        float3 ground_reflected = atmo.ground_albedo_rgb * INV_PI
                                * ground_ndotl
                                * ground_sun_transmittance
                                * sun_illuminance;

        // Add direct ground reflection only, attenuated by path transmittance
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
    float altitude_m = GetSliceAltitudeM(
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
    float r = pass_constants.planet_radius_m + altitude_m;
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
        Texture2D<float4> multi_scat_lut
            = ResourceDescriptorHeap[pass_constants.multi_scat_srv_index];
        SamplerState linear_sampler = SamplerDescriptorHeap[0]; // Assume sampler 0 is linear

        // Sun illuminance (linear RGB, Lux).
        float3 sun_illuminance = GetSunColorRGB() * GetSunIlluminance();

        result = ComputeSingleScattering(
            origin, view_dir, sun_dir, ray_length, hits_ground, atmo,
            pass_constants.transmittance_srv_index,
            float(pass_constants.transmittance_width),
            float(pass_constants.transmittance_height),
            multi_scat_lut, linear_sampler,
            sun_illuminance);
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

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Camera Volume LUT Compute Shader
//!
//! Precomputes scattering and transmittance in camera-aligned froxels.
//! Output: RGBA16F 3D texture where:
//!   RGB = inscattered radiance
//!   A   = opacity (1 - transmittance)
//!
//! Froxel Distribution:
//!   32 slices with SQUARED distribution for near-camera detail
//!   Slice depth = (slice_index / 32)² × max_distance
//!
//! === Bindless Discipline ===
//! - All resources accessed via SM 6.6 descriptor heaps
//! - SceneConstants at b1, RootConstants at b2, EnvironmentDynamicData at b3

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/SceneConstants.hlsli"
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

// Pass constants for camera volume LUT generation
struct CameraVolumeLutPassConstants
{
    uint output_uav_index;          // UAV index for RWTexture3D<float4>
    uint transmittance_srv_index;   // Transmittance LUT
    uint multi_scat_srv_index;      // Multi-scatter LUT
    uint output_width;              // Resolution (e.g., 160)

    uint output_height;             // Resolution (e.g., 90)
    uint output_depth;              // Slice count (32)
    uint transmittance_width;
    uint transmittance_height;

    float max_distance_km;          // Maximum froxel distance (e.g., 128 km)
    float sun_cos_zenith;
    uint atmosphere_flags;
    uint _pad0;

    float4x4 inv_projection_matrix;
    float4x4 inv_view_matrix;
};

#define THREAD_GROUP_SIZE_X 8
#define THREAD_GROUP_SIZE_Y 8
#define THREAD_GROUP_SIZE_Z 1

// Froxel constants (matches UE5)
static const uint AP_SLICE_COUNT = 32;
static const float AP_KM_PER_SLICE = 4.0;

//! Converts froxel slice index to world-space depth in meters.
//! Uses squared distribution for better near-camera detail.
float AerialPerspectiveSliceToDepth(float slice, float max_distance_km)
{
    // Squared distribution: depth = (slice/32)² × max_distance
    float t = slice / float(AP_SLICE_COUNT);
    return t * t * max_distance_km * 1000.0; // Convert km to meters
}

// Atmosphere density functions (shared with other shaders)
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

//! Samples the transmittance LUT for optical depth.
float3 SampleTransmittanceLutOpticalDepth(
    float altitude,
    float cos_zenith,
    GpuSkyAtmosphereParams atmo,
    Texture2D<float4> transmittance_lut,
    SamplerState linear_sampler,
    uint2 lut_size)
{
    float cos_horizon = HorizonCosineFromAltitude(atmo.planet_radius_m, altitude);
    if (cos_zenith < cos_horizon)
    {
        return float3(1e6, 1e6, 1e6);
    }

    float view_height = atmo.planet_radius_m + altitude;
    float top_radius = atmo.planet_radius_m + atmo.atmosphere_height_m;
    float H = SafeSqrt(top_radius * top_radius - atmo.planet_radius_m * atmo.planet_radius_m);
    float rho = SafeSqrt(view_height * view_height - atmo.planet_radius_m * atmo.planet_radius_m);

    float discriminant = view_height * view_height * (cos_zenith * cos_zenith - 1.0) + top_radius * top_radius;
    float d = max(0.0, (-view_height * cos_zenith + SafeSqrt(discriminant)));

    float d_min = top_radius - view_height;
    float d_max = rho + H;
    float u = (d - d_min) / (d_max - d_min);
    float v = rho / H;
    u = clamp(u, 0.0, 1.0);
    v = clamp(v, 0.0, 1.0);

    return transmittance_lut.SampleLevel(linear_sampler, float2(u, v), 0).rgb;
}

//! Converts optical depth to transmittance.
float3 TransmittanceFromOpticalDepth(float3 optical_depth, GpuSkyAtmosphereParams atmo)
{
    float3 beta_rayleigh = atmo.rayleigh_scattering_rgb;
    float3 beta_mie_ext = atmo.mie_scattering_rgb / 0.9;
    float3 beta_abs = atmo.absorption_rgb;
    float3 tau = beta_rayleigh * optical_depth.x + beta_mie_ext * optical_depth.y + beta_abs * optical_depth.z;
    return exp(-tau);
}

//! Rayleigh phase function.
float RayleighPhase(float cos_theta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cos_theta * cos_theta);
}

//! Cornette-Shanks Mie phase function.
float CornetteShanksMiePhaseFunction(float g, float cos_theta)
{
    float k = 3.0 / (8.0 * PI) * (1.0 - g * g) / (2.0 + g * g);
    float denom = max(1.0 + g * g - 2.0 * g * cos_theta, 1e-5);
    return k * (1.0 + cos_theta * cos_theta) / pow(denom, 1.5);
}

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, THREAD_GROUP_SIZE_Z)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    ConstantBuffer<CameraVolumeLutPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    if (dispatch_thread_id.x >= pass_constants.output_width
        || dispatch_thread_id.y >= pass_constants.output_height
        || dispatch_thread_id.z >= pass_constants.output_depth)
    {
        return;
    }

    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        RWTexture3D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
        output[dispatch_thread_id] = float4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    GpuSkyAtmosphereParams atmo = env_data.atmosphere;

    // Compute froxel depth with squared distribution
    float slice = float(dispatch_thread_id.z) + 0.5;
    float t_max_m = AerialPerspectiveSliceToDepth(slice, pass_constants.max_distance_km);

    // Reconstruct view ray from UV
    float2 uv = (float2(dispatch_thread_id.xy) + 0.5) / float2(pass_constants.output_width, pass_constants.output_height);
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;

    // Get camera position and view direction from scene constants
    float3 camera_pos_ws = camera_position;
    float4x4 inv_proj = pass_constants.inv_projection_matrix;
    float4x4 inv_view = pass_constants.inv_view_matrix;

    float4 clip_pos = float4(ndc, 1.0, 1.0);
    float4 view_pos = mul(inv_proj, clip_pos);
    view_pos /= view_pos.w;
    float3 view_dir_vs = normalize(view_pos.xyz);
    float3 view_dir_ws = normalize(mul((float3x3)inv_view, view_dir_vs));

    // Sun direction (world space).
    // Use the designated/override sun from EnvironmentDynamicData to preserve azimuth.
    float3 sun_dir = normalize(GetSunDirectionWS());

    // Sun radiance proxy (linear RGB).
    // Keep consistent with SkyViewLut_CS.hlsl: MultiScatLUT is normalized (unit sun),
    // so we apply the actual sun radiance here.
    float3 sun_radiance = GetSunColorRGB() * GetSunIlluminance();

    // Ray origin in planet-centered coordinates
    float camera_altitude_m = GetCameraAltitudeM();
    float3 origin = float3(0.0, 0.0, atmo.planet_radius_m + camera_altitude_m);

    // Clamp to ground if camera is underground
    float view_height = length(origin);
    if (view_height <= (atmo.planet_radius_m + 0.01))
    {
        origin = normalize(origin) * (atmo.planet_radius_m + 0.01);
        view_height = length(origin);
    }

    // Integrate scattering along ray segment [0, t_max_m]
    float3 inscatter = float3(0.0, 0.0, 0.0);
    float3 throughput = float3(1.0, 1.0, 1.0);

    // Variable sample count based on slice depth (more samples for distant slices)
    uint num_steps = max(4, min(32, uint(slice + 1.0) * 2));
    float step_size = t_max_m / float(num_steps);

    float cos_theta = dot(view_dir_ws, sun_dir);
    float rayleigh_phase = RayleighPhase(cos_theta);
    float mie_phase = CornetteShanksMiePhaseFunction(atmo.mie_g, cos_theta);

    float3 beta_rayleigh = atmo.rayleigh_scattering_rgb;
    float3 beta_mie_ext = atmo.mie_scattering_rgb / 0.9;
    float3 beta_abs = atmo.absorption_rgb;

    Texture2D<float4> transmittance_lut = ResourceDescriptorHeap[pass_constants.transmittance_srv_index];
    Texture2D<float4> multi_scat_lut = ResourceDescriptorHeap[pass_constants.multi_scat_srv_index];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    uint2 lut_size = uint2(pass_constants.transmittance_width, pass_constants.transmittance_height);

    for (uint i = 0; i < num_steps; ++i)
    {
        // UE tuned parameter: fixed 0.3 offset within each segment (SampleSegmentT).
        float t = (float(i) + 0.3) * step_size;
        float3 sample_pos = origin + view_dir_ws * t;
        float altitude = length(sample_pos) - atmo.planet_radius_m;
        altitude = max(altitude, 0.0);

        if (altitude > atmo.atmosphere_height_m) continue;

        float d_r = GetAtmosphereDensity(altitude, atmo.rayleigh_scale_height_m);
        float d_m = GetAtmosphereDensity(altitude, atmo.mie_scale_height_m);
        float d_a = GetAbsorptionDensity(altitude, atmo.absorption_scale_height_m);

        float3 extinction = beta_rayleigh * d_r + beta_mie_ext * d_m + beta_abs * d_a;
        float3 sample_optical_depth = extinction * step_size;
        float3 sample_transmittance = exp(-sample_optical_depth);

        // Sun transmittance
        float3 sample_dir = normalize(sample_pos);
        float cos_sun_zenith = dot(sample_dir, sun_dir);
        float3 sun_od = SampleTransmittanceLutOpticalDepth(altitude, cos_sun_zenith, atmo, transmittance_lut, linear_sampler, lut_size);
        float3 sun_transmittance = TransmittanceFromOpticalDepth(sun_od, atmo);

        // Single scattering
        float3 sigma_s_single = (atmo.rayleigh_scattering_rgb * d_r * rayleigh_phase
                      + atmo.mie_scattering_rgb * d_m * mie_phase)
                      * sun_transmittance * sun_radiance;

        // Multi-scattering
        float u_ms = (cos_sun_zenith + 1.0) / 2.0;
        float v_ms = altitude / atmo.atmosphere_height_m;
        float4 ms_sample = multi_scat_lut.SampleLevel(linear_sampler, float2(u_ms, v_ms), 0);
        float3 multi_scat_radiance = ms_sample.rgb;
        float f_ms = ms_sample.a;
        float3 energy_compensation = 1.0 / max(1.0 - f_ms, 1e-4);
        float3 sigma_s_multi = (atmo.rayleigh_scattering_rgb * d_r + atmo.mie_scattering_rgb * d_m)
                     * multi_scat_radiance * energy_compensation
                     * atmo.multi_scattering_factor * sun_radiance;

        float3 S = sigma_s_single + sigma_s_multi;

        // Frostbite analytic integration
        float3 Sint;
        if (all(extinction < 1e-6))
        {
            Sint = S * step_size;
        }
        else
        {
            Sint = (S - S * sample_transmittance) / max(extinction, 1e-6);
        }

        inscatter += throughput * Sint;
        inscatter = min(inscatter, float3(65000.0, 65000.0, 65000.0)); // FP16 safety
        throughput *= sample_transmittance;
    }

    // Output: RGB = inscatter, A = opacity
    // Match UE reference: opacity derived from average (non-colored) transmittance.
    float opacity = 1.0 - dot(throughput, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
    opacity = saturate(opacity);

    RWTexture3D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
    output[dispatch_thread_id] = float4(inscatter, opacity);
}

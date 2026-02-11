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
#include "Atmosphere/AtmosphereMedium.hlsli"
#include "Atmosphere/AtmospherePhase.hlsli"
#include "Common/Math.hlsli"
#include "Atmosphere/AtmosphereConstants.hlsli"
#include "Common/Geometry.hlsli"
#include "Common/Coordinates.hlsli"
#include "Common/Lighting.hlsli"
#include "Atmosphere/AtmosphereSampling.hlsli"
#include "Atmosphere/IntegrateScatteredLuminance.hlsli"

#include "Atmosphere/AtmospherePassConstants.hlsli"

// Root constants (b2, space0)
cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

#define THREAD_GROUP_SIZE_X 8
#define THREAD_GROUP_SIZE_Y 8
#define THREAD_GROUP_SIZE_Z 1

// Froxel constants (matches UE5)
static const uint AP_SLICE_COUNT = kAerialPerspectiveSliceCount;
static const float AP_KM_PER_SLICE = kAerialPerspectiveKmPerSlice;

//! Converts froxel slice index to world-space depth in meters.
//! Uses squared distribution for better near-camera detail.
float AerialPerspectiveSliceToDepth(float slice, float max_distance_km)
{
    // Squared distribution: depth = (slice/32)² × max_distance
    float t = slice / float(AP_SLICE_COUNT);
    return t * t * max_distance_km * 1000.0; // Convert km to meters
}

[numthreads(THREAD_GROUP_SIZE_X, THREAD_GROUP_SIZE_Y, THREAD_GROUP_SIZE_Z)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    ConstantBuffer<AtmospherePassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];

    if (dispatch_thread_id.x >= pass_constants.output_extent.x
        || dispatch_thread_id.y >= pass_constants.output_extent.y
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
    float2 uv = (float2(dispatch_thread_id.xy) + 0.5) / float2(pass_constants.output_extent);
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
    float view_height_m = length(origin);
    if (view_height_m <= (atmo.planet_radius_m + 1e-1))
    {
        origin = normalize(origin) * (atmo.planet_radius_m + 1e-1);
        view_height_m = length(origin);
    }

    // Integrate scattering along ray segment [0, t_max_m]
    // Variable sample count based on slice depth (more samples for distant slices)
    uint num_steps = max(4, min(32, uint(slice + 1.0) * 2));

    // Load multi-scat LUT for integration
    Texture2D<float4> multi_scat_lut = ResourceDescriptorHeap[pass_constants.multi_scat_srv_index];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    // Use shared integration helper with uniform sample distribution
    float3 throughput;
    float3 inscatter = IntegrateScatteredLuminanceUniform(
        origin, view_dir_ws, t_max_m, num_steps, atmo,
        sun_dir, sun_radiance,
        pass_constants.transmittance_srv_index,
        float(pass_constants.transmittance_extent.x),
        float(pass_constants.transmittance_extent.y),
        multi_scat_lut, linear_sampler,
        throughput);

    // Output: RGB = inscatter, A = opacity
    // Match UE reference: opacity derived from average (non-colored) transmittance.
    float opacity = 1.0 - dot(throughput, float3(1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0));
    opacity = saturate(opacity);

    RWTexture3D<float4> output = ResourceDescriptorHeap[pass_constants.output_uav_index];
    output[dispatch_thread_id] = float4(inscatter, opacity);
}

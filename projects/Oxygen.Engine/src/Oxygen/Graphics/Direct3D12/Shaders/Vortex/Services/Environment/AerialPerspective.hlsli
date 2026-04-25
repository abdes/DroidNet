//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Aerial Perspective Sampling for Forward Rendering
//!
//! Computes atmospheric scattering between the camera and scene geometry.
//! Uses precomputed LUTs when enabled.
//!
//! === Algorithm Overview ===
//! Aerial perspective adds color and fades distant objects by simulating
//! light scattering in the atmosphere between camera and surface:
//!   final_color = surface_color * transmittance + inscatter
//!
//! The transmittance LUT encodes optical depth, and the sky-view LUT provides
//! inscattered radiance. For scene geometry, we raymarch or approximate using
//! the average inscatter along the view ray.

#ifndef OXYGEN_D3D12_SHADERS_ATMOSPHERE_AERIAL_PERSPECTIVE_HLSLI
#define OXYGEN_D3D12_SHADERS_ATMOSPHERE_AERIAL_PERSPECTIVE_HLSLI

#include "Vortex/Contracts/Environment/EnvironmentStaticData.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentViewHelpers.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/Draw/DrawHelpers.hlsli"
#include "Vortex/Contracts/View/ViewColorHelpers.hlsli"
#include "Vortex/Services/Environment/AtmosphereSampling.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"
#include "Vortex/Services/Environment/AtmosphereConstants.hlsli"

#include "Vortex/Shared/Geometry.hlsli"
#include "Vortex/Shared/Lighting.hlsli"

//! Result of aerial perspective computation.
struct AerialPerspectiveResult
{
    float3 inscatter;     //!< Inscattered radiance to add (linear RGB).
    float3 transmittance; //!< RGB transmittance through atmosphere [0, 1].
};

static inline bool IsOrthoProjection()
{
    return is_orthographic != 0u;
}

static inline float ResolveNearDepthReference()
{
    return reverse_z != 0u ? 1.0f : 0.0f;
}

static inline float WorldMetersToAtmosphereKm(float meters)
{
    return meters * 1.0e-3f;
}

//! Samples the camera-volume LUT at the fragment's screen UV and depth slice.
//!
//! Returns float4 where rgb = inscatter, a = transmittance.
float4 SampleCameraVolumeLut(
    GpuSkyAtmosphereParams atmo,
    float3 world_pos,
    float3 camera_pos,
    float view_distance)
{
    if (view_distance < 0.1 || atmo.camera_volume_lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    // UE5.7 applies AerialPespectiveViewDistanceScale during volume generation.
    // Do not rescale the lookup distance here or the camera volume contract
    // drifts and below-horizon fog bands over-brighten.
    float view_distance_km = WorldMetersToAtmosphereKm(view_distance);

    const EnvironmentViewData view_data = LoadResolvedEnvironmentViewData();
    const float depth_resolution = view_data.camera_aerial_volume_depth_params.x;
    const float depth_resolution_inv = view_data.camera_aerial_volume_depth_params.y;
    const float depth_slice_length_km = view_data.camera_aerial_volume_depth_params.z;
    const float depth_slice_length_km_inv = view_data.camera_aerial_volume_depth_params.w;
    const float start_depth_km
        = view_data.sky_aerial_luminance_aerial_start_depth_km.w;

    float t_depth = max(0.0, view_distance_km - start_depth_km);
    float linear_slice = t_depth * depth_slice_length_km_inv;
    float linear_w = linear_slice * depth_resolution_inv;
    float non_linear_w = sqrt(saturate(linear_w));
    float non_linear_slice = non_linear_w * depth_resolution;

    const float half_slice_depth = 0.70710678118654752440084436210485f;
    float weight = 1.0;
    if (non_linear_slice < half_slice_depth)
    {
        weight = saturate(non_linear_slice * non_linear_slice * 2.0);
    }
    const float near_fade_out_range_inv_depth_km = 1.0 / 0.00001;
    weight *= saturate(t_depth * near_fade_out_range_inv_depth_km);

    // Screen UV from world position
    float4 view_pos = mul(view_matrix, float4(world_pos, 1.0));
    float4 clip_pos = mul(projection_matrix, view_pos);
    clip_pos /= clip_pos.w;
    float2 screen_uv = clip_pos.xy * 0.5 + 0.5;
    screen_uv.y = 1.0 - screen_uv.y;

    if (IsOrthoProjection())
    {
        const ViewHistoryFrameBindings view_history = LoadResolvedViewHistoryFrameBindings();
        const float2 sv_pos_xy =
            screen_uv * view_history.current_view_rect_min_and_size.zw
            + view_history.current_view_rect_min_and_size.xy;
        screen_uv =
            (sv_pos_xy - view_history.current_view_rect_min_and_size.xy)
            / view_history.current_view_rect_min_and_size.zw;
        const float3 near_world_position = ReconstructWorldPosition(
            screen_uv,
            ResolveNearDepthReference(),
            inverse_view_projection_matrix);
        const float3 ortho_camera_offset = near_world_position - camera_pos;
        view_distance = max(0.0f, length((world_pos - camera_pos) + ortho_camera_offset) - 0.0f);
        t_depth = max(0.0f, WorldMetersToAtmosphereKm(view_distance) - start_depth_km);
        linear_slice = t_depth * depth_slice_length_km_inv;
        linear_w = linear_slice * depth_resolution_inv;
        non_linear_w = sqrt(saturate(linear_w));
        non_linear_slice = non_linear_w * depth_resolution;
    }

    Texture3D<float4> camera_volume = ResourceDescriptorHeap[atmo.camera_volume_lut_slot];
    // The aerial-perspective volume is camera-aligned, not tileable. Using the
    // wrap sampler here reintroduces sunset / below-horizon ghosting.
    SamplerState linear_sampler
        = SamplerDescriptorHeap[kAtmosphereLinearClampSampler];
    float4 aerial = camera_volume.SampleLevel(
        linear_sampler, float3(screen_uv, non_linear_w), 0);
    aerial.rgb *= weight;
    aerial.a = 1.0 - (weight * (1.0 - aerial.a));
    aerial.rgb *= rcp(max(GetExposure(), 1.0e-6f));
    return aerial;
}

float RaySphereIntersectFarthest(float3 origin, float3 dir, float radius)
{
    float t0, t1;
    if (!RaySphereIntersectBoth(origin, dir, radius, t0, t1))
    {
        return -1.0;
    }
    float t = max(t0, t1);
    return (t > 0.0) ? t : -1.0;
}

//! Computes aerial perspective using LUT sampling.
//!
//! Approximates inscattering along the view ray by sampling the sky-view LUT
//! at multiple points and integrating. Uses a simplified 2-sample approximation
//! for performance.
//!
//! @param atmo Atmosphere parameters from EnvironmentStaticData.
//! @param world_pos World-space position of the fragment.
//! @param camera_pos World-space camera position.
//! @param sun_dir Normalized direction toward the sun.
//! @param view_distance Distance from camera to fragment in meters.
//! @return Aerial perspective result with inscatter and transmittance.
//! Computes aerial perspective using froxel-based camera volume LUT.
//!
//! Replaces the simplified 2-sample approximation with a high-performance
//! 3D texture lookup into the camera-aligned volume.
//!
//! @param atmo Atmosphere parameters from EnvironmentStaticData.
//! @param world_pos World-space position of the fragment.
//! @param camera_pos World-space camera position.
//! @param sun_dir Normalized direction toward the sun.
//! @param view_distance Distance from camera to fragment in meters.
//! @return Aerial perspective result with inscatter and transmittance.
AerialPerspectiveResult ComputeAerialPerspectiveLut(
    GpuSkyAtmosphereParams atmo,
    float3 world_pos,
    float3 camera_pos,
    float3 sun_dir,
    float view_distance)
{
    AerialPerspectiveResult result;
    result.inscatter = float3(0.0, 0.0, 0.0);
    result.transmittance = float3(1.0, 1.0, 1.0);

    // User controls from resolved environment view data.
    float scattering_strength = max(GetAerialScatteringStrength(), 0.0);

    // Early out if disabled via strength
    if (scattering_strength < 0.0001)
    {
        return result;
    }

    float4 ap_sample = SampleCameraVolumeLut(atmo, world_pos, camera_pos, view_distance);

    // Apply user scattering strength
    result.inscatter = ap_sample.rgb * scattering_strength;

    // Camera-volume alpha stores transmittance, matching the producer contract.
    // Keep transmittance independent of the artistic scattering strength control;
    // strength scales only the added inscatter term.
    result.transmittance = saturate(ap_sample.a);

    return result;
}

//! Computes aerial perspective (main entry point).
//!
//! @param env_data Static environment data.
//! @param world_pos World-space position of the fragment.
//! @param camera_pos World-space camera position.
//! @param sun_dir Normalized direction toward the sun.
//! @return Aerial perspective result.
AerialPerspectiveResult ComputeAerialPerspective(
    EnvironmentStaticData env_data,
    float3 world_pos,
    float3 camera_pos,
    float3 sun_dir)
{
    AerialPerspectiveResult result;
    result.inscatter = float3(0.0, 0.0, 0.0);
    result.transmittance = float3(1.0, 1.0, 1.0);

    const EnvironmentViewData view_data = LoadResolvedEnvironmentViewData();
    if (view_data.trace_sample_scale_transmittance_min_light_elevation_holdout_mainpass.w <= 0.5f)
    {
        return result;
    }

    float3 view_vec = world_pos - camera_pos;
    float view_distance = length(view_vec);

    if (view_distance < 0.001)
    {
        return result;
    }

    result = ComputeAerialPerspectiveLut(
        env_data.atmosphere,
        world_pos,
        camera_pos,
        sun_dir,
        view_distance);

    return result;
}

//! Applies aerial perspective to a lit fragment color.
//!
//! Blends the fragment color with atmospheric inscatter based on transmittance.
//!
//! @param fragment_color Original lit fragment color (linear RGB).
//! @param ap Aerial perspective result from ComputeAerialPerspective.
//! @return Final color with aerial perspective applied (linear RGB).
float3 ApplyAerialPerspective(float3 fragment_color, AerialPerspectiveResult ap)
{
    return fragment_color * ap.transmittance + ap.inscatter;
}

#endif // OXYGEN_D3D12_SHADERS_ATMOSPHERE_AERIAL_PERSPECTIVE_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Aerial Perspective Sampling
//!
//! Applies the camera aerial-perspective volume produced by
//! AtmosphereCameraAerialPerspective.hlsl. The contract is shaped after
//! UE5.7 SkyAtmosphereCommon.ush:
//!   final_color = surface_color * transmittance + inscatter
//!
//! Height fog is composed in Vortex's dedicated fog pass after AP. That is
//! mathematically equivalent to UE's WithFogOver helper for opaque scene
//! composition: fog.rgb + ap.rgb * fog.a, fog.a * ap.a.

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

static inline float3 WorldMetersToAtmosphereKm(float3 meters)
{
    return meters * 1.0e-3f;
}

static inline float2 FromUnitToSubUvs(float2 uv, float4 size_and_inv_size)
{
    return (uv + 0.5f * size_and_inv_size.zw)
        * (size_and_inv_size.xy / (size_and_inv_size.xy + 1.0f.xx));
}

static inline float ComputeAerialPerspectiveNearFadeWeight(
    float non_linear_slice,
    float t_depth_km,
    float near_fade_out_range_inv_depth_km)
{
    const float half_slice_depth = 0.70710678118654752440084436210485f;
    float weight = 1.0f;
    if (non_linear_slice < half_slice_depth)
    {
        weight = saturate(non_linear_slice * non_linear_slice * 2.0f);
    }
    return weight * saturate(t_depth_km * near_fade_out_range_inv_depth_km);
}

static inline float4 GetAerialPerspectiveLuminanceTransmittance(
    bool view_is_real_time_reflection_capture,
    float4 camera_aerial_perspective_volume_size_and_inv_size,
    float4 ndc_position,
    float3 world_position_relative_to_camera_km,
    Texture3D<float4> aerial_perspective_volume_texture,
    SamplerState aerial_perspective_volume_texture_sampler,
    float aerial_perspective_volume_depth_resolution_inv,
    float aerial_perspective_volume_depth_resolution,
    float aerial_perspective_volume_start_depth_km,
    float aerial_perspective_volume_depth_slice_length_km_inv,
    float one_over_exposure,
    float near_fade_out_range_inv_depth_km)
{
    if (IsOrthoProjection())
    {
        const float2 screen_uv_for_view = (ndc_position.xy / ndc_position.ww)
            * float2(0.5f, -0.5f) + 0.5f;
        const float3 near_world_position = ReconstructWorldPosition(
            screen_uv_for_view,
            ResolveNearDepthReference(),
            inverse_view_projection_matrix);
        world_position_relative_to_camera_km += WorldMetersToAtmosphereKm(
            near_world_position - camera_position);
    }

    float2 screen_uv = (ndc_position.xy / ndc_position.ww)
        * float2(0.5f, -0.5f) + 0.5f;

    const float t_depth_km = max(0.0f,
        length(world_position_relative_to_camera_km)
            - aerial_perspective_volume_start_depth_km);
    const float linear_slice = t_depth_km
        * aerial_perspective_volume_depth_slice_length_km_inv;
    const float linear_w = linear_slice
        * aerial_perspective_volume_depth_resolution_inv;
    const float non_linear_w = sqrt(saturate(linear_w));
    const float non_linear_slice = non_linear_w
        * aerial_perspective_volume_depth_resolution;
    const float weight = ComputeAerialPerspectiveNearFadeWeight(
        non_linear_slice, t_depth_km, near_fade_out_range_inv_depth_km);

    if (view_is_real_time_reflection_capture)
    {
        float3 world_dir = normalize(world_position_relative_to_camera_km);
        const float sin_phi = world_dir.z;
        const float cos_phi = sqrt(saturate(1.0f - sin_phi * sin_phi));
        screen_uv.y = world_dir.z * 0.5f + 0.5f;

        const float cos_theta = world_dir.x / max(cos_phi, 1.0e-6f);
        const float sin_theta = world_dir.y / max(cos_phi, 1.0e-6f);
        float theta = acos(clamp(cos_theta, -1.0f, 1.0f));
        theta = sin_theta < 0.0f ? (PI - theta) + PI : theta;
        screen_uv.x = theta / (2.0f * PI);
        screen_uv = FromUnitToSubUvs(
            screen_uv, camera_aerial_perspective_volume_size_and_inv_size);
    }

    float4 ap = aerial_perspective_volume_texture.SampleLevel(
        aerial_perspective_volume_texture_sampler,
        float3(screen_uv, non_linear_w),
        0.0f);
    ap.rgb *= weight;
    ap.a = 1.0f - (weight * (1.0f - ap.a));
    ap.rgb *= one_over_exposure;
    return ap;
}

static inline float4 GetAerialPerspectiveLuminanceTransmittanceWithFogOver(
    bool view_is_real_time_reflection_capture,
    float4 camera_aerial_perspective_volume_size_and_inv_size,
    float4 ndc_position,
    float3 world_position_relative_to_camera_km,
    Texture3D<float4> aerial_perspective_volume_texture,
    SamplerState aerial_perspective_volume_texture_sampler,
    float aerial_perspective_volume_depth_resolution_inv,
    float aerial_perspective_volume_depth_resolution,
    float aerial_perspective_volume_start_depth_km,
    float aerial_perspective_volume_depth_slice_length_km_inv,
    float one_over_exposure,
    float4 fog_to_apply_over)
{
    const float near_fade_out_range_inv_depth_km = 1.0f / 0.00001f;
    const float4 ap = GetAerialPerspectiveLuminanceTransmittance(
        view_is_real_time_reflection_capture,
        camera_aerial_perspective_volume_size_and_inv_size,
        ndc_position,
        world_position_relative_to_camera_km,
        aerial_perspective_volume_texture,
        aerial_perspective_volume_texture_sampler,
        aerial_perspective_volume_depth_resolution_inv,
        aerial_perspective_volume_depth_resolution,
        aerial_perspective_volume_start_depth_km,
        aerial_perspective_volume_depth_slice_length_km_inv,
        one_over_exposure,
        near_fade_out_range_inv_depth_km);

    float4 final_fog;
    final_fog.rgb = fog_to_apply_over.rgb + ap.rgb * fog_to_apply_over.a;
    final_fog.a = fog_to_apply_over.a * ap.a;
    return final_fog;
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

    const EnvironmentViewData view_data = LoadResolvedEnvironmentViewData();
    Texture3D<float4> camera_volume = ResourceDescriptorHeap[atmo.camera_volume_lut_slot];
    SamplerState linear_sampler
        = SamplerDescriptorHeap[kAtmosphereLinearClampSampler];
    const float4 view_pos = mul(view_matrix, float4(world_pos, 1.0f));
    const float4 ndc_position = mul(projection_matrix, view_pos);
    const float3 relative_to_camera_km = WorldMetersToAtmosphereKm(world_pos - camera_pos);
    const float4 camera_volume_size_and_inv_size = float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float near_fade_out_range_inv_depth_km = 1.0f / 0.00001f;
    return GetAerialPerspectiveLuminanceTransmittance(
        false,
        camera_volume_size_and_inv_size,
        ndc_position,
        relative_to_camera_km,
        camera_volume,
        linear_sampler,
        view_data.camera_aerial_volume_depth_params.y,
        view_data.camera_aerial_volume_depth_params.x,
        view_data.sky_aerial_luminance_aerial_start_depth_km.w,
        view_data.camera_aerial_volume_depth_params.w,
        rcp(max(GetExposure(), 1.0e-6f)),
        near_fade_out_range_inv_depth_km);
}

//! Computes aerial perspective using LUT sampling.
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

    float scattering_strength = max(GetAerialScatteringStrength(), 0.0);

    if (scattering_strength < 0.0001)
    {
        return result;
    }

    float4 ap_sample = SampleCameraVolumeLut(atmo, world_pos, camera_pos, view_distance);

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

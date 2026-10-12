//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_LOCAL_FOG_VOLUME_PIXEL_HLSLI
#define OXYGEN_VORTEX_LOCAL_FOG_VOLUME_PIXEL_HLSLI

#include "Vortex/Services/Environment/LocalFogVolumeCommon.hlsli"
#include "Vortex/Services/Environment/VolumetricFogSampling.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

// Length of the view ray traced through local fog over the sky; volumes clamp
// the ray to their own extent, so it only needs to exceed every volume.
static const float kLocalFogSkyRayLengthMeters = 1.0e6f;

// Camera-relative view ray through a pixel. Clip depths 0.5 and 1 stay finite
// under reverse-Z with an infinite far plane. Perspective rays start at the
// camera; orthographic rays are parallel and start at the pixel's point on
// the plane through the camera facing the view direction.
static inline void LocalFogPixelRay(
    float2 uv, out float3 origin, out float3 dir)
{
    const float3 a = ReconstructWorldPosition(
        uv, 0.5f, inverse_view_projection_matrix) - camera_position;
    const float3 b = ReconstructWorldPosition(
        uv, 1.0f, inverse_view_projection_matrix) - camera_position;
    const bool b_farther = dot(b, b) > dot(a, a);
    dir = normalize(b_farther ? b - a : a - b);
    origin = 0.0f.xxx;
    if (is_orthographic != 0u)
    {
        const float3 nearer = b_farther ? a : b;
        origin = nearer - dir * dot(nearer, dir);
    }
}

// Local fog in scene units (luminance, transmittance) along the view ray of
// the pixel at `uv` up to its depth, or past every volume over the sky. With
// a positive `exclude_view_depth_m`, the trace starts where that view depth is
// reached along the ray: volumetric fog holds the local fog in front of it, as
// UE does when local fog volumes are injected into volumetric fog.
static float4 EvaluateLocalFogVolumesAtPixel(
    LocalFogComposePassConstants pass,
    float2 uv,
    float raw_depth,
    bool far_background,
    float exclude_view_depth_m)
{
    if (pass.instance_buffer_slot == K_INVALID_BINDLESS_INDEX
        || pass.tile_data_texture_slot == K_INVALID_BINDLESS_INDEX
        || pass.instance_count == 0u
        || pass.tile_resolution_x == 0u
        || pass.tile_resolution_y == 0u
        || pass.tile_pixel_size == 0u)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 ray_origin;
    float3 ray_dir;
    LocalFogPixelRay(uv, ray_origin, ray_dir);
    const float3 translated_world_position = far_background
        ? ray_origin + ray_dir * kLocalFogSkyRayLengthMeters
        : ReconstructWorldPosition(uv, raw_depth, inverse_view_projection_matrix)
            - camera_position;
    const float exclude_distance_m = exclude_view_depth_m > 0.0f
        ? exclude_view_depth_m * RayLengthPerViewDepth(view_matrix, ray_dir)
        : 0.0f;

    const float2 pixel = uv * float2(pass.view_width, pass.view_height);
    const uint2 tile_coord = min(
        uint2(max(pixel, 0.0f.xx)) / pass.tile_pixel_size,
        uint2(pass.tile_resolution_x - 1u, pass.tile_resolution_y - 1u));

    StructuredBuffer<LocalFogVolumeInstanceData> instances
        = ResourceDescriptorHeap[pass.instance_buffer_slot];
    Texture2DArray<uint> tile_data_texture
        = ResourceDescriptorHeap[pass.tile_data_texture_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[K_SAMPLER_LINEAR_WRAP];
    return GetLocalFogVolumeContribution(
        instances, tile_data_texture, pass, tile_coord, linear_sampler,
        camera_position, ray_origin, translated_world_position,
        exclude_distance_m);
}

#endif

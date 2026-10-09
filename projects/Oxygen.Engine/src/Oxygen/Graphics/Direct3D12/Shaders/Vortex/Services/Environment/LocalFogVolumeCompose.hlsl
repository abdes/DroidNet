//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Vortex/Contracts/View/FrameExposureHelpers.hlsli"
#include "Core/Bindless/Generated.BindlessAbi.hlsl"
#include "Vortex/Contracts/View/ViewConstants.hlsli"

#include "Vortex/Contracts/Scene/SceneTextures.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Services/Environment/LocalFogVolumeCommon.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct LocalFogTileVertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    nointerpolation uint packed_tile : TEXCOORD1;
};

// Length of the view ray traced through local fog over the sky; volumes clamp
// the ray to their own extent, so it only needs to exceed every volume.
static const float kLocalFogSkyRayLengthMeters = 1.0e6f;

static inline float ResolveFarDepthReference()
{
    return reverse_z != 0u ? 0.0f : 1.0f;
}

static inline bool IsFarBackgroundPixel(float scene_depth)
{
    return abs(scene_depth - ResolveFarDepthReference()) <= 1.0e-6f;
}

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

[shader("vertex")]
LocalFogTileVertexOutput VortexLocalFogVolumeComposeVS(
    uint vertex_id : SV_VertexID,
    uint instance_id : SV_InstanceID)
{
    LocalFogTileVertexOutput output = (LocalFogTileVertexOutput)0;
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return output;
    }

    StructuredBuffer<LocalFogComposePassConstants> pass_constants_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const LocalFogComposePassConstants pass = pass_constants_buffer[0];
    if (pass.occupied_tile_buffer_slot == K_INVALID_BINDLESS_INDEX
        || pass.tile_resolution_x == 0u
        || pass.tile_resolution_y == 0u)
    {
        return output;
    }

    StructuredBuffer<uint> occupied_tiles
        = ResourceDescriptorHeap[pass.occupied_tile_buffer_slot];
    const uint packed_tile = occupied_tiles[instance_id];
    const uint2 tile_coord = UnpackLocalFogTile(packed_tile);
    const float2 tile_min_pixel = float2(tile_coord * pass.tile_pixel_size);
    const float2 tile_max_pixel = tile_min_pixel + float2(pass.tile_pixel_size, pass.tile_pixel_size);

    float2 pixel = tile_min_pixel;
    switch (vertex_id)
    {
    case 0u: pixel = float2(tile_min_pixel.x, tile_min_pixel.y); break;
    case 1u: pixel = float2(tile_max_pixel.x, tile_min_pixel.y); break;
    case 2u: pixel = float2(tile_max_pixel.x, tile_max_pixel.y); break;
    case 3u: pixel = float2(tile_min_pixel.x, tile_min_pixel.y); break;
    case 4u: pixel = float2(tile_max_pixel.x, tile_max_pixel.y); break;
    default: pixel = float2(tile_min_pixel.x, tile_max_pixel.y); break;
    }

    output.uv = pixel / float2(pass.view_width, pass.view_height);
    output.packed_tile = packed_tile;
    output.position = float4(
        pixel * float2(2.0f / pass.view_width, -2.0f / pass.view_height)
            + float2(-1.0f, 1.0f),
        pass.start_depth_z,
        1.0f);
    return output;
}

[shader("pixel")]
[earlydepthstencil]
float4 VortexLocalFogVolumeComposePS(LocalFogTileVertexOutput input) : SV_Target0
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return 0.0f.xxxx;
    }

    StructuredBuffer<LocalFogComposePassConstants> pass_constants_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const LocalFogComposePassConstants pass = pass_constants_buffer[0];
    if (pass.instance_buffer_slot == K_INVALID_BINDLESS_INDEX
        || pass.tile_data_texture_slot == K_INVALID_BINDLESS_INDEX
        || pass.instance_count == 0u
        || pass.tile_resolution_x == 0u
        || pass.tile_resolution_y == 0u)
    {
        return 0.0f.xxxx;
    }

    const SceneTextureBindingData bindings
        = LoadSceneTextureBindings(bindless_view_frame_bindings_slot);
    const float raw_depth = SampleSceneDepth(input.uv, bindings);
    float3 ray_origin;
    float3 ray_dir;
    LocalFogPixelRay(input.uv, ray_origin, ray_dir);
    // The sky has no surface: trace the view ray past every volume, so a
    // volume in front of the sky is composed like one in front of geometry.
    const float3 translated_world_position = IsFarBackgroundPixel(raw_depth)
        ? ray_origin + ray_dir * kLocalFogSkyRayLengthMeters
        : ReconstructWorldPosition(
              input.uv, raw_depth, inverse_view_projection_matrix)
            - camera_position;
    const uint2 tile_coord = UnpackLocalFogTile(input.packed_tile);

    StructuredBuffer<LocalFogVolumeInstanceData> instances
        = ResourceDescriptorHeap[pass.instance_buffer_slot];
    Texture2DArray<uint> tile_data_texture
        = ResourceDescriptorHeap[pass.tile_data_texture_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    const float4 fog = GetLocalFogVolumeContribution(
        instances, tile_data_texture, pass, tile_coord, linear_sampler,
        camera_position, ray_origin, translated_world_position);
    return float4(fog.rgb * GetPreExposure(), 1.0f - fog.a);
}

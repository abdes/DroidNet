//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Services/Environment/LocalFogVolumeCommon.hlsli"
#include "Vortex/Services/Shadows/DirectionalShadowCommon.hlsli"
#include "Vortex/Shared/PositionReconstruction.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VolumetricFogOutputHeader
{
    uint output_texture_uav;
    uint output_width;
    uint output_height;
    uint output_depth;
};

struct VolumetricFogGridControl
{
    float start_distance_m;
    float end_distance_m;
    float near_fade_in_distance_m;
    float global_extinction_scale;
};

struct VolumetricFogMediaControl0
{
    float3 albedo_rgb;
    float scattering_distribution;
};

struct VolumetricFogGridZControl
{
    float3 grid_z_params;
    float shadowed_directional_light0_enabled;
};

struct VolumetricFogMediaControl1
{
    float3 emissive_rgb;
    float static_lighting_scattering_intensity;
};

struct VolumetricFogLocalFogControl0
{
    uint instance_buffer_slot;
    uint tile_data_texture_slot;
    uint instance_count;
    uint enabled;
};

struct VolumetricFogLocalFogControl1
{
    uint tile_resolution_x;
    uint tile_resolution_y;
    uint max_instances_per_tile;
    float global_start_distance_m;
};

struct VolumetricFogLocalFogControl2
{
    float max_density_into_volumetric_fog;
    float _pad0;
    float _pad1;
    float _pad2;
};

struct VolumetricFogHeightFogMediaControl0
{
    float primary_density;
    float primary_height_falloff;
    float primary_height_offset_m;
    float secondary_density;
};

struct VolumetricFogHeightFogMediaControl1
{
    float secondary_height_falloff;
    float secondary_height_offset_m;
    float match_height_fog_factor;
    uint enabled;
};

struct VolumetricFogPassConstants
{
    VolumetricFogOutputHeader output_header;
    VolumetricFogGridControl grid;
    VolumetricFogGridZControl grid_z;
    VolumetricFogMediaControl0 media0;
    VolumetricFogMediaControl1 media1;
    VolumetricFogHeightFogMediaControl0 height_fog0;
    VolumetricFogHeightFogMediaControl1 height_fog1;
    VolumetricFogLocalFogControl0 local_fog0;
    VolumetricFogLocalFogControl1 local_fog1;
    VolumetricFogLocalFogControl2 local_fog2;
    float4 light0_direction_enabled;
    float4 light0_illuminance_rgb;
    float4 light1_direction_enabled;
    float4 light1_illuminance_rgb;
};

struct VolumetricLocalFogMedia
{
    float extinction;
    float3 scattering;
    float3 emissive;
};

static float3 EvaluateDirectionalContribution(
    float4 direction_enabled,
    float4 illuminance_rgb,
    float scattering_distribution,
    float3 view_direction_to_camera,
    float shadow_visibility)
{
    if (direction_enabled.w <= 0.0f) {
        return 0.0f.xxx;
    }

    const float3 light_direction = normalize(direction_enabled.xyz);
    const float phase = HenyeyGreensteinPhase(
        dot(view_direction_to_camera, light_direction),
        scattering_distribution);
    return max(illuminance_rgb.xyz, 0.0f.xxx) * phase * saturate(shadow_visibility);
}

static float ComputeDepthFromZSlice(float z_slice, float3 grid_z_params)
{
    const float scale = max(abs(grid_z_params.x), 1.0e-8f);
    const float distribution = max(abs(grid_z_params.z), 1.0e-4f);
    return (exp2(z_slice / distribution) - grid_z_params.y) / scale;
}

static float ComputeDeviceDepthFromViewDepth(float view_depth)
{
    const float safe_depth = max(view_depth, 1.0e-4f);
    const float4 clip_position =
        mul(projection_matrix, float4(0.0f, 0.0f, -safe_depth, 1.0f));
    return abs(clip_position.w) > 1.0e-6f
        ? clip_position.z / clip_position.w
        : (reverse_z != 0u ? 0.0f : 1.0f);
}

static float EvaluateHeightFogLayerDensity(
    float density,
    float height_falloff,
    float height_offset_m,
    float world_position_z)
{
    if (density <= 0.0f)
    {
        return 0.0f;
    }

    const float exponent = clamp(
        -max(height_falloff, 0.0f) * (world_position_z - height_offset_m),
        -125.0f,
        126.0f);
    return density * exp2(exponent);
}

static float EvaluateHeightFogMediaDensity(
    VolumetricFogPassConstants pass,
    float world_position_z)
{
    if (pass.height_fog1.enabled == 0u)
    {
        return 0.0f;
    }

    const float primary_density = EvaluateHeightFogLayerDensity(
        pass.height_fog0.primary_density,
        pass.height_fog0.primary_height_falloff,
        pass.height_fog0.primary_height_offset_m,
        world_position_z);
    const float secondary_density = EvaluateHeightFogLayerDensity(
        pass.height_fog0.secondary_density,
        pass.height_fog1.secondary_height_falloff,
        pass.height_fog1.secondary_height_offset_m,
        world_position_z);
    return max(primary_density + secondary_density, 0.0f)
        * pass.height_fog1.match_height_fog_factor;
}

static VolumetricLocalFogMedia EvaluateLocalFogVolumeFroxelMedia(
    LocalFogVolumeInstanceData encoded_instance,
    float3 translated_world_position,
    float soft_density_scale,
    float max_density)
{
    VolumetricLocalFogMedia media = (VolumetricLocalFogMedia)0;
    const DecodedLocalFogVolumeInstanceData instance
        = DecodeLocalFogVolumeInstanceData(encoded_instance);
    const float3 unit_space_position
        = TransformTranslatedWorldPositionToLocal(instance, translated_world_position);
    const float sphere_fade = saturate(1.0f - length(unit_space_position));
    if (sphere_fade <= 0.0f)
    {
        return media;
    }

    const float height_extinction = instance.height_fog_extinction
        * exp(-instance.height_fog_falloff
            * (unit_space_position.z - instance.height_fog_offset));
    const float radial_extinction = instance.radial_fog_extinction
        * pow(sphere_fade, 0.82f);
    const float height_transmittance = exp(-max(height_extinction, 0.0f));
    const float radial_transmittance = exp(-max(radial_extinction, 0.0f));
    const float combined_transmittance = max(
        height_transmittance
            - height_transmittance * radial_transmittance
            + radial_transmittance,
        1.0e-6f);

    const float extinction = min(max(max_density, 0.0f),
        max(-log(combined_transmittance), 0.0f)) * saturate(soft_density_scale);
    media.extinction = extinction;
    media.scattering = extinction * max(instance.albedo, 0.0f.xxx);
    media.emissive = extinction * max(instance.emissive, 0.0f.xxx);
    return media;
}

static VolumetricLocalFogMedia EvaluateLocalFogVolumesForFroxel(
    VolumetricFogPassConstants pass,
    float2 screen_uv,
    float3 translated_world_position,
    float front_distance,
    float back_distance)
{
    VolumetricLocalFogMedia accumulated = (VolumetricLocalFogMedia)0;
    if (pass.local_fog0.enabled == 0u
        || pass.local_fog0.instance_buffer_slot == K_INVALID_BINDLESS_INDEX
        || pass.local_fog0.tile_data_texture_slot == K_INVALID_BINDLESS_INDEX
        || pass.local_fog0.instance_count == 0u
        || pass.local_fog1.tile_resolution_x == 0u
        || pass.local_fog1.tile_resolution_y == 0u
        || pass.local_fog1.max_instances_per_tile == 0u
        || back_distance <= pass.local_fog1.global_start_distance_m)
    {
        return accumulated;
    }

    const float froxel_depth = max(back_distance - front_distance, 1.0e-4f);
    const float soft_density_scale
        = front_distance < pass.local_fog1.global_start_distance_m
        ? saturate((back_distance - pass.local_fog1.global_start_distance_m)
            / froxel_depth)
        : 1.0f;
    if (soft_density_scale <= 0.0f)
    {
        return accumulated;
    }

    const uint2 tile_resolution = uint2(
        pass.local_fog1.tile_resolution_x, pass.local_fog1.tile_resolution_y);
    const uint2 tile_coord = min(
        (uint2)floor(saturate(screen_uv) * float2(tile_resolution)),
        tile_resolution - 1u);

    StructuredBuffer<LocalFogVolumeInstanceData> instances
        = ResourceDescriptorHeap[pass.local_fog0.instance_buffer_slot];
    Texture2DArray<uint> tile_data_texture
        = ResourceDescriptorHeap[pass.local_fog0.tile_data_texture_slot];
    const uint tile_count = min(
        tile_data_texture[uint3(tile_coord, 0u)],
        pass.local_fog1.max_instances_per_tile);

    [loop]
    for (uint tile_index = 0u; tile_index < tile_count; ++tile_index)
    {
        const uint instance_index
            = tile_data_texture[uint3(tile_coord, 1u + tile_index)];
        if (instance_index >= pass.local_fog0.instance_count)
        {
            continue;
        }
        const VolumetricLocalFogMedia media = EvaluateLocalFogVolumeFroxelMedia(
            instances[instance_index],
            translated_world_position,
            soft_density_scale,
            pass.local_fog2.max_density_into_volumetric_fog);
        accumulated.extinction += media.extinction;
        accumulated.scattering += media.scattering;
        accumulated.emissive += media.emissive;
    }

    return accumulated;
}

[shader("compute")]
[numthreads(4, 4, 4)]
void VortexVolumetricFogCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    StructuredBuffer<VolumetricFogPassConstants> pass_buffer =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    const VolumetricFogPassConstants pass = pass_buffer[0];
    if (pass.output_header.output_texture_uav == K_INVALID_BINDLESS_INDEX
        || dispatch_id.x >= pass.output_header.output_width
        || dispatch_id.y >= pass.output_header.output_height
        || dispatch_id.z >= pass.output_header.output_depth) {
        return;
    }

    RWTexture3D<float4> output_texture =
        ResourceDescriptorHeap[pass.output_header.output_texture_uav];

    const float front_distance = clamp(
        ComputeDepthFromZSlice(float(dispatch_id.z), pass.grid_z.grid_z_params),
        pass.grid.start_distance_m,
        pass.grid.end_distance_m);
    const float back_distance = clamp(
        ComputeDepthFromZSlice(float(dispatch_id.z) + 1.0f, pass.grid_z.grid_z_params),
        pass.grid.start_distance_m,
        pass.grid.end_distance_m);
    const float slice_distance = clamp(
        ComputeDepthFromZSlice(float(dispatch_id.z) + 0.5f, pass.grid_z.grid_z_params),
        pass.grid.start_distance_m,
        pass.grid.end_distance_m);
    const float ray_length = max(slice_distance - pass.grid.start_distance_m, 0.0f);
    const float near_fade = pass.grid.near_fade_in_distance_m > 1.0e-3f
        ? saturate(ray_length / pass.grid.near_fade_in_distance_m)
        : 1.0f;

    const float2 screen_uv =
        (float2(dispatch_id.xy) + float2(0.5f, 0.5f))
        / max(float2(pass.output_header.output_width, pass.output_header.output_height),
            float2(1.0f, 1.0f));
    const float device_depth = ComputeDeviceDepthFromViewDepth(slice_distance);
    const float3 world_position =
        ReconstructWorldPosition(screen_uv, device_depth, inverse_view_projection_matrix);
    const float3 translated_world_position = world_position - camera_position;
    const float3 camera_delta = camera_position - world_position;
    const float camera_delta_length_sq = dot(camera_delta, camera_delta);
    const float3 view_direction_to_camera = camera_delta_length_sq > 1.0e-8f
        ? camera_delta * rsqrt(camera_delta_length_sq)
        : float3(0.0f, 0.0f, 1.0f);

    const VolumetricLocalFogMedia local_fog_media =
        EvaluateLocalFogVolumesForFroxel(
            pass,
            screen_uv,
            translated_world_position,
            front_distance,
            back_distance);
    const float height_fog_density =
        EvaluateHeightFogMediaDensity(pass, world_position.z);
    const float extinction =
        height_fog_density * max(pass.grid.global_extinction_scale, 0.0f) * near_fade
        + max(local_fog_media.extinction, 0.0f);
    const float transmittance = exp(-extinction * ray_length);
    const float opacity = saturate(1.0f - transmittance);

    float light0_shadow_visibility = 1.0f;
    if (pass.grid_z.shadowed_directional_light0_enabled > 0.0f
        && pass.light0_direction_enabled.w > 0.0f) {
        light0_shadow_visibility = ComputeDirectionalVolumetricShadowVisibility(
            world_position, pass.light0_direction_enabled.xyz);
    }

    float3 directional_lighting = 0.0f.xxx;
    directional_lighting += EvaluateDirectionalContribution(
        pass.light0_direction_enabled,
        pass.light0_illuminance_rgb,
        pass.media0.scattering_distribution,
        view_direction_to_camera,
        light0_shadow_visibility);
    directional_lighting += EvaluateDirectionalContribution(
        pass.light1_direction_enabled,
        pass.light1_illuminance_rgb,
        pass.media0.scattering_distribution,
        view_direction_to_camera,
        1.0f);

    const float3 bounded_lighting =
        directional_lighting * (2.0e-5f * pass.media1.static_lighting_scattering_intensity);
    const float3 scattering =
        max(pass.media0.albedo_rgb, 0.0f.xxx) * bounded_lighting
        + local_fog_media.scattering * bounded_lighting
        + max(pass.media1.emissive_rgb, 0.0f.xxx);
    const float3 integrated_luminance =
        (scattering + local_fog_media.emissive) * opacity;

    output_texture[dispatch_id] = float4(max(integrated_luminance, 0.0f.xxx),
        saturate(transmittance));
}

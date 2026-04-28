//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_LOCALFOGVOLUMECOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_LOCALFOGVOLUMECOMMON_HLSLI

#include "Vortex/Services/Environment/AtmospherePhase.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentStaticData.hlsli"
#include "Vortex/Contracts/Lighting/LightingHelpers.hlsli"
#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/Scene/ScreenHzbBindings.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"

struct LocalFogVolumeInstanceData
{
    uint4 data0;
    uint4 data1;
    uint4 data2;
};

struct LocalFogVolumeCullingData
{
    float4 sphere_world;
};

struct LocalFogComposePassConstants
{
    uint instance_buffer_slot;
    uint tile_data_texture_slot;
    uint occupied_tile_buffer_slot;
    uint tile_resolution_x;
    uint tile_resolution_y;
    uint max_instances_per_tile;
    uint instance_count;
    uint tile_pixel_size;
    uint view_width;
    uint view_height;
    float global_start_distance_meters;
    float start_depth_z;
    uint _pad0;
    uint _pad1;
};

struct LocalFogTiledCullingPassConstants
{
    uint instance_buffer_slot;
    uint instance_culling_buffer_slot;
    uint tile_data_texture_slot;
    uint occupied_tile_buffer_slot;
    uint indirect_args_buffer_slot;
    uint instance_count;
    uint tile_resolution_x;
    uint tile_resolution_y;
    uint max_instances_per_tile;
    uint use_hzb;
    uint _pad0;
    uint _pad1;
    float4 left_plane;
    float4 right_plane;
    float4 top_plane;
    float4 bottom_plane;
    float4 near_plane;
    float2 view_to_tile_space_ratio;
    float2 _pad2;
};

struct LocalFogClipSphere
{
    float4 center;
    float4 extent;
};

struct LocalFogVolumeIntegralData
{
    float integrated_luminance_factor;
    float coverage;
};

struct DecodedLocalFogVolumeInstanceData
{
    float3 translated_world_pos;
    float uniform_scale;
    float uniform_scale_inv;
    float radial_fog_extinction;
    float height_fog_extinction;
    float height_fog_falloff;
    float height_fog_offset;
    float3 emissive;
    float3 albedo;
    float phase_g;
    float3x3 pre_translated_inv_transform;
};

static const uint kLocalFogDirectionalLightAtmosphereAuthority = 1u << 0u;

static inline uint PackLocalFogTile(uint2 tile_coord)
{
    return (tile_coord.x & 0xFFFFu) | ((tile_coord.y & 0xFFFFu) << 16u);
}

static inline uint2 UnpackLocalFogTile(uint packed_tile)
{
    return uint2(packed_tile & 0xFFFFu, packed_tile >> 16u);
}

static inline float2 UnpackFloat2FromUInt(uint packed)
{
    return float2(f16tof32(packed & 0xFFFFu), f16tof32(packed >> 16u));
}

static inline float DecodeUnsignedMiniFloat(uint value, uint mantissa_bits)
{
    const uint exponent_bits = 5u;
    const uint exponent_mask = (1u << exponent_bits) - 1u;
    const uint mantissa_mask = (1u << mantissa_bits) - 1u;
    const uint exponent = (value >> mantissa_bits) & exponent_mask;
    const uint mantissa = value & mantissa_mask;

    if (exponent == 0u)
    {
        if (mantissa == 0u)
        {
            return 0.0f;
        }
        return ldexp((float)mantissa / (float)(1u << mantissa_bits), -14);
    }

    if (exponent == exponent_mask)
    {
        return 65504.0f;
    }

    return ldexp(1.0f + (float)mantissa / (float)(1u << mantissa_bits),
        int(exponent) - 15);
}

static inline float3 UnpackFloat111110(uint packed)
{
    return float3(
        DecodeUnsignedMiniFloat((packed >> 0u) & 0x7FFu, 6u),
        DecodeUnsignedMiniFloat((packed >> 11u) & 0x7FFu, 6u),
        DecodeUnsignedMiniFloat((packed >> 22u) & 0x3FFu, 5u));
}

static inline float4 UnpackUNorm8888(uint packed)
{
    return float4(
        (float)((packed >> 0u) & 0xFFu) / 255.0f,
        (float)((packed >> 8u) & 0xFFu) / 255.0f,
        (float)((packed >> 16u) & 0xFFu) / 255.0f,
        (float)((packed >> 24u) & 0xFFu) / 255.0f);
}

static inline DecodedLocalFogVolumeInstanceData DecodeLocalFogVolumeInstanceData(
    LocalFogVolumeInstanceData instance)
{
    DecodedLocalFogVolumeInstanceData decoded = (DecodedLocalFogVolumeInstanceData)0;
    decoded.translated_world_pos = asfloat(instance.data0.xyz);
    decoded.uniform_scale = asfloat(instance.data0.w);
    decoded.uniform_scale_inv = 1.0f / max(decoded.uniform_scale, 1.0e-6f);

    float3 x_vec = 0.0f.xxx;
    float3 y_vec = 0.0f.xxx;
    x_vec.xy = UnpackFloat2FromUInt(instance.data1.x);
    const float2 temp = UnpackFloat2FromUInt(instance.data1.y);
    x_vec.z = temp.x;
    y_vec.x = temp.y;
    y_vec.yz = UnpackFloat2FromUInt(instance.data1.z);
    const float3 z_vec = cross(x_vec, y_vec);
    decoded.pre_translated_inv_transform = float3x3(
        x_vec * decoded.uniform_scale_inv,
        y_vec * decoded.uniform_scale_inv,
        z_vec * decoded.uniform_scale_inv);

    const float3 packed_extinction = UnpackFloat111110(instance.data2.x);
    decoded.radial_fog_extinction = packed_extinction.x;
    decoded.height_fog_extinction = packed_extinction.y;
    decoded.height_fog_falloff = packed_extinction.z;

    decoded.emissive = UnpackFloat111110(instance.data2.y);
    const float4 packed_albedo_phase = UnpackUNorm8888(instance.data2.z);
    decoded.albedo = packed_albedo_phase.rgb;
    decoded.phase_g = packed_albedo_phase.a;
    decoded.height_fog_offset = asfloat(instance.data2.w);
    return decoded;
}

static inline bool GetLocalFogDirectionalLight(
    out float3 directional_light_color,
    out float3 directional_light_direction)
{
    const LightingFrameBindings lighting = LoadResolvedLightingFrameBindings();
    if (lighting.has_directional_light == 0u)
    {
        directional_light_color = 0.0f.xxx;
        directional_light_direction = float3(0.0f, 0.0f, 1.0f);
        return false;
    }

    directional_light_color = lighting.directional.color
        * lighting.directional.illuminance_lux;
    directional_light_direction = lighting.directional.direction;

    if ((lighting.directional.atmosphere_mode_flags
            & kLocalFogDirectionalLightAtmosphereAuthority) != 0u)
    {
        // Match UE5.7 local-fog lighting: the local fog path consumes the
        // directional light color after the atmosphere proxy has published the
        // baked ground-level transmittance. For per-pixel atmosphere lights UE
        // still applies the baked transmittance here as an approximation to
        // avoid a transmittance texture sample in the local-fog shader.
        directional_light_color
            *= lighting.directional.transmittance_toward_sun_rgb;
    }

    return true;
}

static inline float3 TransformTranslatedWorldVectorToLocal(
    DecodedLocalFogVolumeInstanceData instance, float3 translated_world_vector)
{
    return mul(translated_world_vector, instance.pre_translated_inv_transform);
}

static inline float3 TransformTranslatedWorldPositionToLocal(
    DecodedLocalFogVolumeInstanceData instance, float3 translated_world_position)
{
    return TransformTranslatedWorldVectorToLocal(
        instance, translated_world_position - instance.translated_world_pos);
}

static inline LocalFogClipSphere BuildLocalFogClipSphere(float4 sphere_world)
{
    const float3 center_ws = sphere_world.xyz;
    const float radius = max(sphere_world.w, 0.0f);

    const float4 center_clip
        = mul(projection_matrix, mul(view_matrix, float4(center_ws, 1.0f)));
    const float4 x_clip = mul(
        projection_matrix,
        mul(view_matrix, float4(center_ws + float3(radius, 0.0f, 0.0f), 1.0f)));
    const float4 y_clip = mul(
        projection_matrix,
        mul(view_matrix, float4(center_ws + float3(0.0f, radius, 0.0f), 1.0f)));
    const float4 z_clip = mul(
        projection_matrix,
        mul(view_matrix, float4(center_ws + float3(0.0f, 0.0f, radius), 1.0f)));

    LocalFogClipSphere result;
    result.center = center_clip;
    result.extent = abs(x_clip - center_clip);
    result.extent = max(result.extent, abs(y_clip - center_clip));
    result.extent = max(result.extent, abs(z_clip - center_clip));
    return result;
}

static inline float ResolveLocalFogNearestDepth(LocalFogClipSphere clip_sphere)
{
    const float near_w = clip_sphere.center.w - clip_sphere.extent.w;
    if (near_w <= 1.0e-5f)
    {
        return 0.0f;
    }

    return saturate((clip_sphere.center.z + clip_sphere.extent.z) / near_w);
}

static inline bool ProjectLocalFogClipSphereToViewportUvBounds(
    LocalFogClipSphere clip_sphere, out float2 uv_min, out float2 uv_max)
{
    uv_min = 0.0f.xx;
    uv_max = 0.0f.xx;

    const float near_w = clip_sphere.center.w - clip_sphere.extent.w;
    if (near_w <= 1.0e-5f)
    {
        uv_min = 0.0f.xx;
        uv_max = 1.0f.xx;
        return true;
    }

    const float2 ndc_min
        = (clip_sphere.center.xy - clip_sphere.extent.xy) / near_w;
    const float2 ndc_max
        = (clip_sphere.center.xy + clip_sphere.extent.xy) / near_w;

    if (ndc_max.x < -1.0f || ndc_min.x > 1.0f
        || ndc_max.y < -1.0f || ndc_min.y > 1.0f)
    {
        return false;
    }

    const float2 clamped_ndc_min = clamp(
        ndc_min, float2(-1.0f, -1.0f), float2(1.0f, 1.0f));
    const float2 clamped_ndc_max = clamp(
        ndc_max, float2(-1.0f, -1.0f), float2(1.0f, 1.0f));
    uv_min = float2(
        clamped_ndc_min.x * 0.5f + 0.5f,
        0.5f - clamped_ndc_max.y * 0.5f);
    uv_max = float2(
        clamped_ndc_max.x * 0.5f + 0.5f,
        0.5f - clamped_ndc_min.y * 0.5f);
    return true;
}

static inline bool ProjectLocalFogClipSphereToHzb(LocalFogClipSphere clip_sphere,
    ScreenHzbFrameBindingsData screen_hzb, out uint2 pixel_min, out uint2 pixel_max,
    out uint mip_level)
{
    pixel_min = uint2(0u, 0u);
    pixel_max = uint2(0u, 0u);
    mip_level = 0u;

    if (screen_hzb.width == 0u || screen_hzb.height == 0u)
    {
        return false;
    }

    float2 uv_min = 0.0f.xx;
    float2 uv_max = 0.0f.xx;
    if (!ProjectLocalFogClipSphereToViewportUvBounds(clip_sphere, uv_min, uv_max))
    {
        return false;
    }
    const float2 clamped_uv_min = clamp(uv_min, 0.0f.xx, 1.0f.xx);
    const float2 clamped_uv_max = clamp(uv_max, 0.0f.xx, 1.0f.xx);
    if (clamped_uv_max.x <= clamped_uv_min.x || clamped_uv_max.y <= clamped_uv_min.y)
    {
        return false;
    }

    const float2 hzb_uv_min
        = clamped_uv_min * GetViewportUvToHzbBufferUv(screen_hzb);
    const float2 hzb_uv_max
        = clamped_uv_max * GetViewportUvToHzbBufferUv(screen_hzb);

    const float2 rect_texels = max(
        (hzb_uv_max - hzb_uv_min) * GetHzbSize(screen_hzb),
        float2(1.0f, 1.0f));
    mip_level = (uint)floor(log2(max(rect_texels.x, rect_texels.y)));

    pixel_min = min((uint2)floor(hzb_uv_min * GetHzbSize(screen_hzb)),
        uint2(screen_hzb.width - 1u, screen_hzb.height - 1u));
    pixel_max = min((uint2)floor(
                        max(hzb_uv_max * GetHzbSize(screen_hzb) - 1.0f, 0.0f)),
        uint2(screen_hzb.width - 1u, screen_hzb.height - 1u));
    return true;
}

static const float LOCAL_FOG_HZB_OCCLUSION_EPSILON = 1.0e-3f;

static inline bool LocalFogIsOccludedByHzb(Texture2D<float> furthest_hzb,
    ScreenHzbFrameBindingsData screen_hzb,
    LocalFogClipSphere clip_sphere)
{
    if (!IsScreenHzbFurthestValid(screen_hzb)
        || screen_hzb.furthest_srv == INVALID_BINDLESS_INDEX
        || screen_hzb.width == 0u
        || screen_hzb.height == 0u
        || screen_hzb.mip_count == 0u)
    {
        return false;
    }

    uint2 pixel_min = uint2(0u, 0u);
    uint2 pixel_max = uint2(0u, 0u);
    uint mip_level = 0u;
    if (!ProjectLocalFogClipSphereToHzb(
            clip_sphere, screen_hzb, pixel_min, pixel_max, mip_level))
    {
        return false;
    }

    const float nearest_depth = ResolveLocalFogNearestDepth(clip_sphere);
    mip_level = min(mip_level, screen_hzb.mip_count - 1u);

    const uint mip_width = max(screen_hzb.width >> mip_level, 1u);
    const uint mip_height = max(screen_hzb.height >> mip_level, 1u);
    const float2 scale = float2(
        (float)mip_width / (float)screen_hzb.width,
        (float)mip_height / (float)screen_hzb.height);
    const uint2 sample_min = min((uint2)floor(float2(pixel_min) * scale),
        uint2(mip_width - 1u, mip_height - 1u));
    const uint2 sample_max = min((uint2)floor(float2(pixel_max) * scale),
        uint2(mip_width - 1u, mip_height - 1u));
    const uint2 sample_center = min((sample_min + sample_max) / 2u,
        uint2(mip_width - 1u, mip_height - 1u));

    const float depth00 = furthest_hzb.Load(int3(sample_min, mip_level)).r;
    const float depth10 = furthest_hzb.Load(
        int3(uint2(sample_max.x, sample_min.y), mip_level)).r;
    const float depth01 = furthest_hzb.Load(
        int3(uint2(sample_min.x, sample_max.y), mip_level)).r;
    const float depth11 = furthest_hzb.Load(int3(sample_max, mip_level)).r;
    const float depth_center = furthest_hzb.Load(int3(sample_center, mip_level)).r;

    return nearest_depth + LOCAL_FOG_HZB_OCCLUSION_EPSILON < depth00
        && nearest_depth + LOCAL_FOG_HZB_OCCLUSION_EPSILON < depth10
        && nearest_depth + LOCAL_FOG_HZB_OCCLUSION_EPSILON < depth01
        && nearest_depth + LOCAL_FOG_HZB_OCCLUSION_EPSILON < depth11
        && nearest_depth + LOCAL_FOG_HZB_OCCLUSION_EPSILON < depth_center;
}

static inline bool LocalFogPlaneIsActive(float4 plane)
{
    return dot(plane.xyz, plane.xyz) > 0.0f;
}

static inline bool LocalFogSphereOutsidePlane(float4 plane, float4 sphere_world)
{
    if (!LocalFogPlaneIsActive(plane))
    {
        return false;
    }

    return dot(plane.xyz, sphere_world.xyz) - plane.w > sphere_world.w;
}

static inline float2 RayIntersectUnitSphere(float3 ray_origin, float3 ray_dir)
{
    const float b = dot(ray_origin, ray_dir);
    const float c = dot(ray_origin, ray_origin) - 1.0f;
    const float h = b * b - c;
    if (h < 0.0f)
    {
        return float2(-1.0f, -1.0f);
    }

    const float sqrt_h = sqrt(h);
    return float2(-b - sqrt_h, -b + sqrt_h);
}

static inline LocalFogVolumeIntegralData EvaluateLocalFogVolumeIntegral(
    DecodedLocalFogVolumeInstanceData instance,
    float3 ray_start_local,
    float3 ray_dir_local,
    float ray_length_local)
{
    LocalFogVolumeIntegralData fog_data;
    fog_data.integrated_luminance_factor = 0.0f;
    fog_data.coverage = 0.0f;

    float radial_optical_depth = 0.0f;
    if (instance.radial_fog_extinction > 0.0f)
    {
        const float3 sphere_to_origin = ray_start_local;
        const float b = dot(ray_dir_local, sphere_to_origin);
        const float c = dot(sphere_to_origin, sphere_to_origin) - 1.0f;
        const float h = b * b - c;
        if (h >= 0.0f)
        {
            const float sqrt_h = sqrt(h);
            float length0 = max(-b - sqrt_h, 0.0f);
            float length1 = min(max(-b + sqrt_h, 0.0f), ray_length_local);
            if (length1 > length0)
            {
                const float length0_sqr = length0 * length0;
                const float length1_sqr = length1 * length1;
                const float integral0
                    = -(c * length0 + b * length0_sqr + length0_sqr * length0 / 3.0f);
                const float integral1
                    = -(c * length1 + b * length1_sqr + length1_sqr * length1 / 3.0f);
                radial_optical_depth
                    = max(0.0f, instance.radial_fog_extinction * (integral1 - integral0) * 0.75f);
            }
        }
    }

    float height_optical_depth = 0.0f;
    if (instance.height_fog_extinction > 0.0f)
    {
        const float start_height = ray_start_local.z - instance.height_fog_offset;
        const float safe_dir_threshold = 1.0e-4f;
        const float sign_dir_z = ray_dir_local.z >= 0.0f ? 1.0f : -1.0f;
        const float safe_dir_z = abs(ray_dir_local.z) < safe_dir_threshold
            ? safe_dir_threshold * sign_dir_z
            : ray_dir_local.z;
        float factor0 = max(-80.0f, start_height * instance.height_fog_falloff);
        const float factor1 = safe_dir_z * ray_length_local * instance.height_fog_falloff;
        height_optical_depth
            = (instance.height_fog_extinction / (instance.height_fog_falloff * safe_dir_z))
            * (exp(-factor0) - exp(-(factor0 + factor1)));
    }

    const float transmittance_radial = exp(-radial_optical_depth);
    const float transmittance_height = exp(-height_optical_depth);
    const float combined_transmittance
        = 1.0f - (1.0f - transmittance_radial) * (1.0f - transmittance_height);
    const float optical_depth = -log(max(combined_transmittance, 1.0e-6f));
    const float transmittance
        = exp(-optical_depth * instance.uniform_scale);
    fog_data.coverage = saturate(1.0f - transmittance);
    fog_data.integrated_luminance_factor = fog_data.coverage;
    return fog_data;
}

static inline float3 EvaluateLocalFogVolumeInScattering(
    DecodedLocalFogVolumeInstanceData instance,
    LocalFogVolumeIntegralData fog_data,
    SamplerState linear_sampler,
    float3 ray_dir_world)
{
    float3 in_scattering = 0.0f.xxx;

    float3 directional_light_color = 0.0f.xxx;
    float3 directional_light_direction = 0.0f.xxx;
    if (GetLocalFogDirectionalLight(
            directional_light_color,
            directional_light_direction))
    {
        in_scattering += directional_light_color
            * HenyeyGreensteinPhase(
                -instance.phase_g,
                dot(ray_dir_world, directional_light_direction));
    }

    EnvironmentStaticData env_data = (EnvironmentStaticData)0;
    if (LoadEnvironmentStaticData(env_data)
        && env_data.sky_light.enabled != 0u
        && env_data.sky_light.diffuse_sh_slot != K_INVALID_BINDLESS_INDEX)
    {
        const float3 sky_direction = normalize(lerp(
            float3(0.0f, 0.0f, 1.0f),
            -ray_dir_world,
            saturate(abs(instance.phase_g))));
        const float3 sky_lighting = EvaluateStaticSkyLightDiffuseSh(
            env_data, sky_direction);
        in_scattering += sky_lighting
            * env_data.sky_light.tint_rgb
            * env_data.sky_light.radiance_scale
            * env_data.sky_light.diffuse_intensity;
    }

    in_scattering *= instance.albedo;
    in_scattering += instance.emissive;
    return in_scattering * fog_data.integrated_luminance_factor;
}

static inline float4 GetLocalFogVolumeInstanceContribution(
    LocalFogVolumeInstanceData encoded_instance,
    float global_start_distance_meters,
    SamplerState linear_sampler,
    float3 camera_position_world,
    float3 translated_world_position)
{
    const DecodedLocalFogVolumeInstanceData instance
        = DecodeLocalFogVolumeInstanceData(encoded_instance);
    const float ray_length_world = length(translated_world_position);
    if (ray_length_world <= 1.0e-4f)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float3 ray_dir_world = translated_world_position / ray_length_world;
    const float3 ray_origin_local
        = TransformTranslatedWorldPositionToLocal(instance, 0.0f.xxx);
    const float3 ray_dir_local = normalize(
        TransformTranslatedWorldVectorToLocal(instance, ray_dir_world));
    const float ray_length_local = ray_length_world * instance.uniform_scale_inv;

    float2 ray_segment = RayIntersectUnitSphere(ray_origin_local, ray_dir_local);
    const float global_start_distance_local
        = global_start_distance_meters * instance.uniform_scale_inv;
    ray_segment = max(ray_segment, float2(global_start_distance_local, global_start_distance_local));
    ray_segment.x = max(ray_segment.x, 0.0f);
    ray_segment.y = min(ray_segment.y, ray_length_local);
    if (ray_segment.y <= ray_segment.x)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float traced_length = ray_segment.y - ray_segment.x;
    const LocalFogVolumeIntegralData fog_data = EvaluateLocalFogVolumeIntegral(
        instance, ray_origin_local + ray_dir_local * ray_segment.x,
        ray_dir_local, traced_length);
    const float3 luminance = EvaluateLocalFogVolumeInScattering(
        instance, fog_data, linear_sampler, ray_dir_world);
    return float4(luminance, 1.0f - fog_data.coverage);
}

static inline float4 GetLocalFogVolumeContribution(
    StructuredBuffer<LocalFogVolumeInstanceData> instances,
    Texture2DArray<uint> tile_data_texture,
    LocalFogComposePassConstants pass,
    uint2 tile_coord,
    SamplerState linear_sampler,
    float3 camera_position_world,
    float3 translated_world_position)
{
    const uint tile_count
        = min(tile_data_texture[uint3(tile_coord, 0)], pass.max_instances_per_tile);
    if (tile_count == 0u)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 accumulated_luminance = 0.0f.xxx;
    float accumulated_transmittance = 1.0f;
    [loop]
    for (uint tile_index = 0u; tile_index < tile_count; ++tile_index)
    {
        const uint instance_index = tile_data_texture[uint3(tile_coord, 1u + tile_index)];
        if (instance_index >= pass.instance_count)
        {
            continue;
        }
        const float4 contribution = GetLocalFogVolumeInstanceContribution(
            instances[instance_index], pass.global_start_distance_meters,
            linear_sampler, camera_position_world, translated_world_position);
        accumulated_luminance = accumulated_luminance * contribution.a + contribution.rgb;
        accumulated_transmittance *= contribution.a;
    }

    return float4(accumulated_luminance, accumulated_transmittance);
}

#endif

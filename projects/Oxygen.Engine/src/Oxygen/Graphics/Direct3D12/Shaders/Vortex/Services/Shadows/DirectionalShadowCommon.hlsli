//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_SHADOWS_DIRECTIONALSHADOWCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_SHADOWS_DIRECTIONALSHADOWCOMMON_HLSLI

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Contracts/View/ViewConstants.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"

static const uint VORTEX_SHADOW_TECHNIQUE_DIRECTIONAL_CONVENTIONAL = 1u << 0u;
static const uint VORTEX_SHADOW_TECHNIQUE_SPOT_CONVENTIONAL = 1u << 1u;
static const uint VORTEX_SHADOW_TECHNIQUE_POINT_CONVENTIONAL = 1u << 2u;

struct VortexShadowCascadeBinding
{
    float4x4 light_view_projection;
    float split_near;
    float split_far;
    float4 sampling_metadata0;
    float4 sampling_metadata1;
    float2 _padding0;
};

struct VortexSpotShadowBinding
{
    float4x4 light_view_projection;
    float4 position_and_inv_range;
    float4 direction_and_bias;
    float4 sampling_metadata0;
    float4 sampling_metadata1;
};

struct VortexPointShadowBinding
{
    float4x4 face_light_view_projection[6];
    float4 position_and_inv_range;
    float4 sampling_metadata0;
    float4 sampling_metadata1;
    float4 _padding0;
};

struct VortexShadowFrameBindings
{
    uint conventional_shadow_surface_handle;
    uint cascade_count;
    uint technique_flags;
    uint sampling_contract_flags;
    float4 light_direction_to_source;
    uint spot_shadow_surface_handle;
    uint spot_shadow_count;
    uint _padding0;
    uint _padding1;
    VortexShadowCascadeBinding cascades[4];
    VortexSpotShadowBinding spot_shadows[8];
    uint point_shadow_surface_handle;
    uint point_shadow_count;
    uint _padding2;
    uint _padding3;
    VortexPointShadowBinding point_shadows[4];
};

static inline VortexShadowFrameBindings MakeInvalidVortexShadowFrameBindings()
{
    VortexShadowFrameBindings bindings = (VortexShadowFrameBindings)0;
    bindings.conventional_shadow_surface_handle = K_INVALID_BINDLESS_INDEX;
    bindings.cascade_count = 0u;
    bindings.technique_flags = 0u;
    bindings.sampling_contract_flags = 0u;
    bindings.spot_shadow_surface_handle = K_INVALID_BINDLESS_INDEX;
    bindings.spot_shadow_count = 0u;
    bindings.point_shadow_surface_handle = K_INVALID_BINDLESS_INDEX;
    bindings.point_shadow_count = 0u;
    return bindings;
}

static inline VortexShadowFrameBindings LoadVortexShadowFrameBindings()
{
    const ViewFrameBindingsData view_bindings =
        LoadVortexViewFrameBindings(bindless_view_frame_bindings_slot);
    if (view_bindings.shadow_frame_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(view_bindings.shadow_frame_slot)) {
        return MakeInvalidVortexShadowFrameBindings();
    }

    StructuredBuffer<VortexShadowFrameBindings> bindings_buffer =
        ResourceDescriptorHeap[view_bindings.shadow_frame_slot];
    return bindings_buffer[0];
}

static inline uint SelectDirectionalShadowCascade(
    VortexShadowFrameBindings bindings,
    float view_depth)
{
    const uint cascade_count = min(max(bindings.cascade_count, 1u), 4u);
    [unroll]
    for (uint i = 0u; i < 4u; ++i) {
        if (i >= cascade_count) {
            break;
        }
        if (view_depth <= bindings.cascades[i].split_far) {
            return i;
        }
    }
    return cascade_count - 1u;
}

static inline float SampleDirectionalShadowSurface(
    VortexShadowFrameBindings bindings,
    uint cascade_index,
    float2 shadow_uv,
    float receiver_depth)
{
    if (bindings.conventional_shadow_surface_handle == K_INVALID_BINDLESS_INDEX) {
        return 1.0f;
    }

    Texture2DArray<float> shadow_surface =
        ResourceDescriptorHeap[bindings.conventional_shadow_surface_handle];
    const uint layer =
        (uint)(bindings.cascades[cascade_index].sampling_metadata0.x + 0.5f);
    const float2 inverse_resolution =
        max(bindings.cascades[cascade_index].sampling_metadata0.yz, float2(0.000001f, 0.000001f));
    const float2 texel_size = inverse_resolution;
    const float2 texture_size = 1.0f / texel_size;
    const int2 max_coord =
        max(int2(texture_size) - int2(1, 1), int2(0, 0));
    const int2 center = int2(shadow_uv * texture_size);

    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 coord = clamp(center + int2(x, y), int2(0, 0), max_coord);
            const float stored_depth = shadow_surface.Load(int4(coord, (int)layer, 0));
            visibility += receiver_depth >= stored_depth ? 1.0f : 0.0f;
        }
    }

    return visibility * (1.0f / 9.0f);
}

static inline float ComputeDirectionalCascadeVisibility(
    VortexShadowFrameBindings bindings,
    uint cascade_index,
    float3 world_position,
    float3 safe_normal,
    float3 safe_light_dir,
    float slope_factor)
{
    const VortexShadowCascadeBinding cascade = bindings.cascades[cascade_index];
    const float world_texel_size = max(cascade.sampling_metadata0.w, 0.0f);
    const float normal_bias =
        max(cascade.sampling_metadata1.w, 0.0f)
        + world_texel_size * lerp(0.55f, 1.5f, slope_factor);
    const float constant_bias =
        world_texel_size * lerp(0.03f, 0.18f, slope_factor);
    const float3 biased_world_position =
        world_position + safe_normal * normal_bias + safe_light_dir * constant_bias;

    const float4 shadow_clip =
        mul(cascade.light_view_projection, float4(biased_world_position, 1.0f));
    if (abs(shadow_clip.w) <= 1.0e-6f) {
        return 1.0f;
    }

    const float3 shadow_ndc = shadow_clip.xyz / shadow_clip.w;
    const float2 shadow_uv = float2(
        shadow_ndc.x * 0.5f + 0.5f,
        shadow_ndc.y * -0.5f + 0.5f);
    if (shadow_uv.x < 0.0f || shadow_uv.x > 1.0f
        || shadow_uv.y < 0.0f || shadow_uv.y > 1.0f
        || shadow_ndc.z < 0.0f || shadow_ndc.z > 1.0f) {
        return 1.0f;
    }

    return SampleDirectionalShadowSurface(
        bindings, cascade_index, shadow_uv, shadow_ndc.z);
}

static inline bool HasDirectionalConventionalShadowBindings(
    VortexShadowFrameBindings bindings)
{
    return (bindings.technique_flags & VORTEX_SHADOW_TECHNIQUE_DIRECTIONAL_CONVENTIONAL) != 0u
        && bindings.cascade_count != 0u
        && bindings.conventional_shadow_surface_handle != K_INVALID_BINDLESS_INDEX;
}

static inline bool HasSpotConventionalShadowBindings(
    VortexShadowFrameBindings bindings)
{
    return (bindings.technique_flags & VORTEX_SHADOW_TECHNIQUE_SPOT_CONVENTIONAL) != 0u
        && bindings.spot_shadow_count != 0u
        && bindings.spot_shadow_surface_handle != K_INVALID_BINDLESS_INDEX;
}

static inline bool HasPointConventionalShadowBindings(
    VortexShadowFrameBindings bindings)
{
    return (bindings.technique_flags & VORTEX_SHADOW_TECHNIQUE_POINT_CONVENTIONAL) != 0u
        && bindings.point_shadow_count != 0u
        && bindings.point_shadow_surface_handle != K_INVALID_BINDLESS_INDEX;
}

static inline float SampleSpotShadowSurface(
    VortexShadowFrameBindings bindings,
    uint spot_shadow_index,
    float2 shadow_uv,
    float receiver_depth)
{
    if (bindings.spot_shadow_surface_handle == K_INVALID_BINDLESS_INDEX) {
        return 1.0f;
    }

    Texture2DArray<float> shadow_surface =
        ResourceDescriptorHeap[bindings.spot_shadow_surface_handle];
    const uint layer =
        (uint)(bindings.spot_shadows[spot_shadow_index].sampling_metadata0.x + 0.5f);
    const float2 inverse_resolution =
        max(bindings.spot_shadows[spot_shadow_index].sampling_metadata0.yz,
            float2(0.000001f, 0.000001f));
    const float2 texture_size = 1.0f / inverse_resolution;
    const int2 max_coord =
        max(int2(texture_size) - int2(1, 1), int2(0, 0));
    const int2 center = int2(shadow_uv * texture_size);

    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 coord = clamp(center + int2(x, y), int2(0, 0), max_coord);
            const float stored_depth = shadow_surface.Load(int4(coord, (int)layer, 0));
            visibility += receiver_depth >= stored_depth ? 1.0f : 0.0f;
        }
    }

    return visibility * (1.0f / 9.0f);
}

static inline float ComputeSpotShadowVisibility(
    VortexShadowFrameBindings bindings,
    uint spot_shadow_index,
    float3 world_position,
    float3 world_normal,
    float3 light_direction_to_source)
{
    if (!HasSpotConventionalShadowBindings(bindings)
        || spot_shadow_index >= min(bindings.spot_shadow_count, 8u)) {
        return 1.0f;
    }

    const VortexSpotShadowBinding spot = bindings.spot_shadows[spot_shadow_index];
    const float3 safe_normal = normalize(
        dot(world_normal, world_normal) > 1.0e-8f ? world_normal : float3(0.0f, 1.0f, 0.0f));
    const float3 safe_light_dir = normalize(
        dot(light_direction_to_source, light_direction_to_source) > 1.0e-8f
            ? light_direction_to_source
            : spot.position_and_inv_range.xyz - world_position);
    const float world_texel_size = max(spot.sampling_metadata0.w, 0.0f);
    const float normal_bias = max(spot.sampling_metadata1.w, 0.0f)
        + world_texel_size * 0.75f;
    const float receiver_bias = world_texel_size * 0.5f;
    const float3 biased_world_position =
        world_position + safe_normal * normal_bias + safe_light_dir * receiver_bias;

    const float4 shadow_clip =
        mul(spot.light_view_projection, float4(biased_world_position, 1.0f));
    if (abs(shadow_clip.w) <= 1.0e-6f) {
        return 1.0f;
    }

    const float3 shadow_ndc = shadow_clip.xyz / shadow_clip.w;
    const float2 shadow_uv = float2(
        shadow_ndc.x * 0.5f + 0.5f,
        shadow_ndc.y * -0.5f + 0.5f);
    if (shadow_uv.x < 0.0f || shadow_uv.x > 1.0f
        || shadow_uv.y < 0.0f || shadow_uv.y > 1.0f
        || shadow_ndc.z < 0.0f || shadow_ndc.z > 1.0f) {
        return 1.0f;
    }

    const float axial_distance = max(
        0.0f,
        dot(biased_world_position - spot.position_and_inv_range.xyz,
            normalize(spot.direction_and_bias.xyz)));
    const float receiver_depth =
        saturate(1.0f - axial_distance * spot.position_and_inv_range.w);

    return SampleSpotShadowSurface(
        bindings, spot_shadow_index, shadow_uv, receiver_depth);
}

static inline uint SelectPointShadowFace(float3 light_to_receiver)
{
    const float3 abs_dir = abs(light_to_receiver);
    if (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z) {
        return light_to_receiver.x >= 0.0f ? 0u : 1u;
    }
    if (abs_dir.y >= abs_dir.z) {
        return light_to_receiver.y >= 0.0f ? 2u : 3u;
    }
    return light_to_receiver.z >= 0.0f ? 4u : 5u;
}

static inline float3 PointShadowFaceDirection(uint face_index)
{
    static const float3 kFaceDirections[6] = {
        float3(1.0f, 0.0f, 0.0f),
        float3(-1.0f, 0.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, -1.0f, 0.0f),
        float3(0.0f, 0.0f, 1.0f),
        float3(0.0f, 0.0f, -1.0f),
    };
    return kFaceDirections[min(face_index, 5u)];
}

static inline float SamplePointShadowSurface(
    VortexShadowFrameBindings bindings,
    VortexPointShadowBinding point_shadow,
    uint point_shadow_index,
    uint face_index,
    float2 shadow_uv,
    float receiver_depth)
{
    if (bindings.point_shadow_surface_handle == K_INVALID_BINDLESS_INDEX) {
        return 1.0f;
    }

    Texture2DArray<float> shadow_surface =
        ResourceDescriptorHeap[bindings.point_shadow_surface_handle];
    const uint base_layer =
        (uint)(point_shadow.sampling_metadata0.x + 0.5f) * 6u;
    const uint layer = base_layer + face_index;
    const float inverse_resolution = max(point_shadow.sampling_metadata0.y, 0.000001f);
    const float texture_size = 1.0f / inverse_resolution;
    const int2 max_coord =
        max(int2(texture_size, texture_size) - int2(1, 1), int2(0, 0));
    const int2 center = int2(shadow_uv * texture_size);

    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 coord = clamp(center + int2(x, y), int2(0, 0), max_coord);
            const float stored_depth = shadow_surface.Load(int4(coord, (int)layer, 0));
            visibility += receiver_depth >= stored_depth ? 1.0f : 0.0f;
        }
    }

    return visibility * (1.0f / 9.0f);
}

static inline float ComputePointShadowVisibility(
    VortexShadowFrameBindings bindings,
    uint point_shadow_index,
    float3 world_position,
    float3 world_normal,
    float3 light_direction_to_source)
{
    if (!HasPointConventionalShadowBindings(bindings)
        || point_shadow_index >= min(bindings.point_shadow_count, 4u)) {
        return 1.0f;
    }

    const VortexPointShadowBinding point_shadow =
        bindings.point_shadows[point_shadow_index];
    const float3 safe_normal = normalize(
        dot(world_normal, world_normal) > 1.0e-8f ? world_normal : float3(0.0f, 1.0f, 0.0f));
    const float3 safe_light_dir = normalize(
        dot(light_direction_to_source, light_direction_to_source) > 1.0e-8f
            ? light_direction_to_source
            : point_shadow.position_and_inv_range.xyz - world_position);
    const float world_texel_size = max(point_shadow.sampling_metadata0.z, 0.0f);
    const float normal_bias = max(point_shadow.sampling_metadata1.x, 0.0f)
        + world_texel_size * 0.75f;
    const float receiver_bias = world_texel_size * 0.5f;
    const float3 biased_world_position =
        world_position + safe_normal * normal_bias + safe_light_dir * receiver_bias;

    const float3 light_to_receiver =
        biased_world_position - point_shadow.position_and_inv_range.xyz;
    const float distance_to_light = length(light_to_receiver);
    if (distance_to_light * point_shadow.position_and_inv_range.w >= 1.0f) {
        return 1.0f;
    }

    const uint face_index = SelectPointShadowFace(light_to_receiver);
    const float4 shadow_clip = mul(
        point_shadow.face_light_view_projection[face_index],
        float4(biased_world_position, 1.0f));
    if (abs(shadow_clip.w) <= 1.0e-6f) {
        return 1.0f;
    }

    const float3 shadow_ndc = shadow_clip.xyz / shadow_clip.w;
    const float2 shadow_uv = float2(
        shadow_ndc.x * 0.5f + 0.5f,
        shadow_ndc.y * -0.5f + 0.5f);
    if (shadow_uv.x < 0.0f || shadow_uv.x > 1.0f
        || shadow_uv.y < 0.0f || shadow_uv.y > 1.0f
        || shadow_ndc.z < 0.0f || shadow_ndc.z > 1.0f) {
        return 1.0f;
    }

    const float axial_distance = max(
        0.0f,
        dot(light_to_receiver, PointShadowFaceDirection(face_index)));
    const float receiver_depth =
        saturate(1.0f - axial_distance * point_shadow.position_and_inv_range.w);

    return SamplePointShadowSurface(
        bindings, point_shadow, point_shadow_index, face_index, shadow_uv,
        receiver_depth);
}

static inline float ComputeDirectionalShadowVisibility(
    float3 world_position,
    float3 world_normal,
    float3 light_direction_to_source)
{
    const VortexShadowFrameBindings bindings = LoadVortexShadowFrameBindings();
    if (!HasDirectionalConventionalShadowBindings(bindings)) {
        return 1.0f;
    }

    const float view_depth = max(0.0f, -mul(view_matrix, float4(world_position, 1.0f)).z);
    const uint cascade_count = min(max(bindings.cascade_count, 1u), 4u);
    const uint cascade_index = SelectDirectionalShadowCascade(bindings, view_depth);
    const VortexShadowCascadeBinding cascade = bindings.cascades[cascade_index];

    const float3 safe_normal = normalize(
        dot(world_normal, world_normal) > 1.0e-8f ? world_normal : float3(0.0f, 1.0f, 0.0f));
    const float3 safe_light_dir = normalize(
        dot(light_direction_to_source, light_direction_to_source) > 1.0e-8f
            ? light_direction_to_source
            : float3(0.0f, -1.0f, 0.0f));
    const float ndotl = saturate(dot(safe_normal, safe_light_dir));
    const float slope_factor = saturate(1.0f - ndotl);
    float visibility = ComputeDirectionalCascadeVisibility(
        bindings, cascade_index, world_position, safe_normal, safe_light_dir, slope_factor);

    const float transition_width = max(cascade.sampling_metadata1.x, 0.0f);
    if (cascade_index + 1u < cascade_count && transition_width > 0.0f) {
        const float transition_begin = cascade.split_far - transition_width;
        const float transition_alpha =
            saturate((view_depth - transition_begin) / transition_width);
        if (transition_alpha > 0.0f) {
            const float next_visibility = ComputeDirectionalCascadeVisibility(
                bindings, cascade_index + 1u, world_position, safe_normal,
                safe_light_dir, slope_factor);
            visibility = lerp(visibility, next_visibility, transition_alpha);
        }
    }

    if (cascade_index + 1u == cascade_count) {
        const float fade_begin = cascade.sampling_metadata1.y;
        const float fade_span = max(cascade.split_far - fade_begin, 0.001f);
        const float fade_alpha = saturate((view_depth - fade_begin) / fade_span);
        visibility = lerp(visibility, 1.0f, fade_alpha);
    }

    return visibility;
}

static inline float ComputeDirectionalVolumetricShadowVisibility(
    VortexShadowFrameBindings bindings,
    float3 world_position,
    float3 light_direction_to_source)
{
    if (!HasDirectionalConventionalShadowBindings(bindings)) {
        return 1.0f;
    }

    const float view_depth = max(0.0f, -mul(view_matrix, float4(world_position, 1.0f)).z);
    const uint cascade_count = min(max(bindings.cascade_count, 1u), 4u);
    const uint cascade_index = SelectDirectionalShadowCascade(bindings, view_depth);
    const VortexShadowCascadeBinding cascade = bindings.cascades[cascade_index];

    const float3 safe_light_dir = normalize(
        dot(light_direction_to_source, light_direction_to_source) > 1.0e-8f
            ? light_direction_to_source
            : float3(0.0f, -1.0f, 0.0f));
    float visibility = ComputeDirectionalCascadeVisibility(
        bindings, cascade_index, world_position, 0.0f.xxx, safe_light_dir, 0.0f);

    const float transition_width = max(cascade.sampling_metadata1.x, 0.0f);
    if (cascade_index + 1u < cascade_count && transition_width > 0.0f) {
        const float transition_begin = cascade.split_far - transition_width;
        const float transition_alpha =
            saturate((view_depth - transition_begin) / transition_width);
        if (transition_alpha > 0.0f) {
            const float next_visibility = ComputeDirectionalCascadeVisibility(
                bindings, cascade_index + 1u, world_position, 0.0f.xxx,
                safe_light_dir, 0.0f);
            visibility = lerp(visibility, next_visibility, transition_alpha);
        }
    }

    if (cascade_index + 1u == cascade_count) {
        const float fade_begin = cascade.sampling_metadata1.y;
        const float fade_span = max(cascade.split_far - fade_begin, 0.001f);
        const float fade_alpha = saturate((view_depth - fade_begin) / fade_span);
        visibility = lerp(visibility, 1.0f, fade_alpha);
    }

    return visibility;
}

static inline float ComputeDirectionalVolumetricShadowVisibility(
    float3 world_position,
    float3 light_direction_to_source)
{
    return ComputeDirectionalVolumetricShadowVisibility(
        LoadVortexShadowFrameBindings(), world_position, light_direction_to_source);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_SHADOWS_DIRECTIONALSHADOWCOMMON_HLSLI

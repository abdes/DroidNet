//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_SHADOWS_DIRECTIONALSHADOWCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_SHADOWS_DIRECTIONALSHADOWCOMMON_HLSLI

#include "Vortex/Contracts/Shadows/ShadowFrameBindings.hlsli"

static inline uint SelectDirectionalShadowCascade(
    VortexShadowFrameBindings bindings, DirectionalShadowRecord family,
    float view_depth)
{
    for (uint i = 0u; i < family.cascade_count; ++i) {
        if (view_depth <= LoadShadowCascade(bindings, family.first_cascade + i).split_far) {
            return family.first_cascade + i;
        }
    }
    return family.first_cascade + family.cascade_count - 1u;
}

static inline float SampleDirectionalShadowSurface(
    VortexShadowFrameBindings bindings,
    uint cascade_index,
    float2 shadow_uv,
    float receiver_depth)
{
    const VortexShadowCascadeBinding cascade = LoadShadowCascade(bindings, cascade_index);
    if (cascade.surface_srv == K_INVALID_BINDLESS_INDEX) {
        return 1.0f;
    }

    // Cascade selection and compact local lists can vary the surface per lane.
    Texture2DArray<float> shadow_surface =
        ResourceDescriptorHeap[NonUniformResourceIndex(cascade.surface_srv)];
    const uint layer =
        cascade.array_layer;
    const float2 inverse_resolution =
        max(cascade.inverse_resolution, float2(0.000001f, 0.000001f));
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
    const VortexShadowCascadeBinding cascade = LoadShadowCascade(bindings, cascade_index);
    const float world_texel_size = max(cascade.world_texel_size, 0.0f);
    const float normal_bias =
        max(cascade.normal_bias_m, 0.0f)
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

static inline bool HasDirectionalConventionalShadowBindings(VortexShadowFrameBindings bindings)
{
    return bindings.directional_record_count != 0u && bindings.cascade_record_count != 0u
        && BX_IN_GLOBAL_SRV(bindings.directional_records_srv) && BX_IN_GLOBAL_SRV(bindings.cascade_records_srv);
}

static inline bool HasSpotConventionalShadowBindings(VortexShadowFrameBindings bindings)
{
    return bindings.projected_local_record_count != 0u && BX_IN_GLOBAL_SRV(bindings.projected_local_records_srv);
}

static inline bool HasPointConventionalShadowBindings(VortexShadowFrameBindings bindings)
{
    return bindings.cube_local_record_count != 0u && BX_IN_GLOBAL_SRV(bindings.cube_local_records_srv);
}

static inline float SampleSpotShadowSurface(
    VortexShadowFrameBindings bindings,
    uint spot_shadow_index,
    float2 shadow_uv,
    float receiver_depth)
{
    const ProjectedLocalShadowRecord spot = LoadProjectedLocalShadow(bindings, spot_shadow_index);
    if (spot.surface_srv == K_INVALID_BINDLESS_INDEX) {
        return 1.0f;
    }

    Texture2DArray<float> shadow_surface =
        ResourceDescriptorHeap[NonUniformResourceIndex(spot.surface_srv)];
    const uint layer =
        spot.array_layer;
    const float2 inverse_resolution =
        max(spot.inverse_resolution,
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
        || spot_shadow_index >= bindings.projected_local_record_count) {
        return 1.0f;
    }

    const ProjectedLocalShadowRecord spot = LoadProjectedLocalShadow(bindings, spot_shadow_index);
    const float3 safe_normal = normalize(
        dot(world_normal, world_normal) > 1.0e-8f ? world_normal : float3(0.0f, 1.0f, 0.0f));
    const float3 safe_light_dir = normalize(
        dot(light_direction_to_source, light_direction_to_source) > 1.0e-8f
            ? light_direction_to_source
            : spot.shadow_origin_ws - world_position);
    // The published footprint is at the far plane. Perspective texels shrink
    // with receiver depth; using the far footprint can move nearby receivers
    // by metres for long-range imported lights and erase their shadows.
    const float receiver_axial_distance = max(0.0f,
        dot(spot.light_view_projection[3], float4(world_position, 1.0f)));
    const float world_texel_size = max(spot.world_texel_size, 0.0f)
        * saturate(receiver_axial_distance / spot.far_plane_m);
    const float normal_bias = max(spot.normal_bias_m, 0.0f)
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

    // Perspective clip W is the same axial distance used by the depth writer.
    const float axial_distance = max(0.0f, shadow_clip.w);
    const float receiver_depth =
        saturate(1.0f - axial_distance / spot.far_plane_m);

    return lerp(1.0f, SampleSpotShadowSurface(
        bindings, spot_shadow_index, shadow_uv, receiver_depth), saturate(spot.shadow_strength));
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
    CubeLocalShadowRecord point_shadow,
    uint point_shadow_index,
    uint face_index,
    float2 shadow_uv,
    float receiver_depth)
{
    if (point_shadow.surface_srv == K_INVALID_BINDLESS_INDEX) {
        return 1.0f;
    }

    Texture2DArray<float> shadow_surface =
        ResourceDescriptorHeap[NonUniformResourceIndex(point_shadow.surface_srv)];
    const uint base_layer =
        point_shadow.first_array_layer;
    const uint layer = base_layer + face_index;
    const float2 inverse_resolution = max(point_shadow.inverse_resolution, float2(0.000001f, 0.000001f));
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

static inline float ComputePointShadowVisibility(
    VortexShadowFrameBindings bindings,
    uint point_shadow_index,
    float3 world_position,
    float3 world_normal,
    float3 light_direction_to_source)
{
    if (!HasPointConventionalShadowBindings(bindings)
        || point_shadow_index >= bindings.cube_local_record_count) {
        return 1.0f;
    }

    const CubeLocalShadowRecord point_shadow =
        LoadCubeLocalShadow(bindings, point_shadow_index);
    const float3 safe_normal = normalize(
        dot(world_normal, world_normal) > 1.0e-8f ? world_normal : float3(0.0f, 1.0f, 0.0f));
    const float3 safe_light_dir = normalize(
        dot(light_direction_to_source, light_direction_to_source) > 1.0e-8f
            ? light_direction_to_source
            : point_shadow.shadow_origin_ws - world_position);
    // Each cube face has a 90-degree perspective projection. Its texel
    // footprint is 2 * axial receiver distance / resolution, not 2 * range.
    const float3 unbiased_delta = abs(world_position - point_shadow.shadow_origin_ws);
    const float receiver_axial_distance = max(unbiased_delta.x,
        max(unbiased_delta.y, unbiased_delta.z));
    const float world_texel_size = max(point_shadow.world_texel_size, 0.0f)
        * saturate(receiver_axial_distance / point_shadow.far_plane_m);
    const float normal_bias = max(point_shadow.normal_bias_m, 0.0f)
        + world_texel_size * 0.75f;
    const float receiver_bias = world_texel_size * 0.5f;
    const float3 biased_world_position =
        world_position + safe_normal * normal_bias + safe_light_dir * receiver_bias;

    const float3 light_to_receiver =
        biased_world_position - point_shadow.shadow_origin_ws;
    const float distance_to_light = length(light_to_receiver);
    if (distance_to_light >= point_shadow.far_plane_m) {
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
        saturate(1.0f - axial_distance / point_shadow.far_plane_m);

    return lerp(1.0f, SamplePointShadowSurface(
        bindings, point_shadow, point_shadow_index, face_index, shadow_uv,
        receiver_depth), saturate(point_shadow.shadow_strength));
}

// Apply the retained center-source visibility approximation exactly once,
// independently of whether a spot is represented by a projection or six faces.
static float ComputeLocalShadowVisibility(LightShadowReference reference,
    float3 world_position, float3 world_normal, float3 direction_to_center)
{
    if (reference.record_index == K_INVALID_BINDLESS_INDEX) return 1.0;
    const VortexShadowFrameBindings bindings = LoadVortexShadowFrameBindings();
    if (reference.projection_kind == SHADOW_PROJECTION_LOCAL_CUBE) {
        return ComputePointShadowVisibility(bindings, reference.record_index,
            world_position, world_normal, direction_to_center);
    }
    if (reference.projection_kind == SHADOW_PROJECTION_LOCAL_PROJECTED_2D) {
        return ComputeSpotShadowVisibility(bindings, reference.record_index,
            world_position, world_normal, direction_to_center);
    }
    return asfloat(0x7fc00000u);
}

static inline float ComputeDirectionalShadowVisibility(
    uint selection_index,
    float3 world_position,
    float3 world_normal,
    float3 light_direction_to_source)
{
    const VortexShadowFrameBindings bindings = LoadVortexShadowFrameBindings();
    DirectionalShadowRecord family;
    if (!TryLoadDirectionalShadowFamily(bindings, selection_index, family)) {
        return 1.0f;
    }

    const float view_depth = max(0.0f, -mul(view_matrix, float4(world_position, 1.0f)).z);
    const uint cascade_end = family.first_cascade + family.cascade_count;
    const uint cascade_index = SelectDirectionalShadowCascade(bindings, family, view_depth);
    const VortexShadowCascadeBinding cascade = LoadShadowCascade(bindings, cascade_index);

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

    const float transition_width = max(cascade.transition_width, 0.0f);
    if (cascade_index + 1u < cascade_end && transition_width > 0.0f) {
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

    if (cascade_index + 1u == cascade_end) {
        const float fade_begin = cascade.fade_begin;
        const float fade_span = max(cascade.fade_end - fade_begin, 0.001f);
        const float fade_alpha = saturate((view_depth - fade_begin) / fade_span);
        visibility = lerp(visibility, 1.0f, fade_alpha);
    }

    return visibility;
}

static inline float ComputeDirectionalVolumetricShadowVisibility(
    uint selection_index,
    float3 world_position,
    float3 light_direction_to_source)
{
    const VortexShadowFrameBindings bindings = LoadVortexShadowFrameBindings();
    DirectionalShadowRecord family;
    if (!TryLoadDirectionalShadowFamily(bindings, selection_index, family)) {
        return 1.0f;
    }

    const float view_depth = max(0.0f, -mul(view_matrix, float4(world_position, 1.0f)).z);
    const uint cascade_end = family.first_cascade + family.cascade_count;
    const uint cascade_index = SelectDirectionalShadowCascade(bindings, family, view_depth);
    const VortexShadowCascadeBinding cascade = LoadShadowCascade(bindings, cascade_index);

    const float3 safe_light_dir = normalize(
        dot(light_direction_to_source, light_direction_to_source) > 1.0e-8f
            ? light_direction_to_source
            : float3(0.0f, -1.0f, 0.0f));
    float visibility = ComputeDirectionalCascadeVisibility(
        bindings, cascade_index, world_position, 0.0f.xxx, safe_light_dir, 0.0f);

    const float transition_width = max(cascade.transition_width, 0.0f);
    if (cascade_index + 1u < cascade_end && transition_width > 0.0f) {
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

    if (cascade_index + 1u == cascade_end) {
        const float fade_begin = cascade.fade_begin;
        const float fade_span = max(cascade.fade_end - fade_begin, 0.001f);
        const float fade_alpha = saturate((view_depth - fade_begin) / fade_span);
        visibility = lerp(visibility, 1.0f, fade_alpha);
    }

    return visibility;
}



#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_SHADOWS_DIRECTIONALSHADOWCOMMON_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_SHADOWHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_SHADOWHELPERS_HLSLI

#include "Renderer/DirectionalShadowMetadata.hlsli"
#include "Renderer/ShadowFrameBindings.hlsli"
#include "Renderer/ShadowInstanceMetadata.hlsli"
#include "Renderer/ViewConstants.hlsli"
#include "Renderer/ViewFrameBindings.hlsli"

#ifndef OXYGEN_SHADOW_USE_MANUAL_COMPARE_FALLBACK
#define OXYGEN_SHADOW_USE_MANUAL_COMPARE_FALLBACK 0
#endif

static const uint kShadowComparisonSamplerIndex = 1u;

struct DirectionalShadowProjection
{
    float2 uv;
    float receiver_depth;
    bool valid;
};

static inline ShadowFrameBindings LoadResolvedShadowFrameBindings()
{
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    return LoadShadowFrameBindings(view_bindings.shadow_frame_slot);
}

static inline float3 ComputeShadowSurfaceNormal(
    float3 world_pos,
    float3 world_normal,
    bool is_front_face)
{
    float3 fallback_shadow_normal = is_front_face ? world_normal : -world_normal;
    const float fallback_shadow_normal_len_sq =
        dot(fallback_shadow_normal, fallback_shadow_normal);
    fallback_shadow_normal = fallback_shadow_normal_len_sq > 1.0e-8
        ? fallback_shadow_normal * rsqrt(fallback_shadow_normal_len_sq)
        : float3(0.0, 0.0, 1.0);
    const float3 world_pos_ddx = ddx_fine(world_pos);
    const float3 world_pos_ddy = ddy_fine(world_pos);
    float3 shadow_normal = cross(world_pos_ddx, world_pos_ddy);
    const float shadow_normal_len_sq = dot(shadow_normal, shadow_normal);
    shadow_normal = shadow_normal_len_sq > 1.0e-8
        ? normalize(shadow_normal)
        : float3(0.0, 0.0, 0.0);
    if (dot(shadow_normal, fallback_shadow_normal) < 0.0) {
        shadow_normal = -shadow_normal;
    }
    if (dot(fallback_shadow_normal, shadow_normal) < 0.25) {
        shadow_normal = fallback_shadow_normal;
    }
    return shadow_normal;
}

static inline uint SelectDirectionalShadowCascade(
    DirectionalShadowMetadata metadata,
    float view_depth)
{
    const uint cascade_count = max(1u, metadata.cascade_count);
    [unroll]
    for (uint i = 0; i < OXYGEN_MAX_SHADOW_CASCADES; ++i) {
        if (i >= cascade_count) {
            break;
        }
        if (view_depth <= metadata.cascade_distances[i]) {
            return i;
        }
    }
    return cascade_count - 1u;
}

static inline DirectionalShadowProjection ProjectDirectionalShadowCascade(
    DirectionalShadowMetadata metadata,
    uint cascade_index,
    float3 world_pos)
{
    DirectionalShadowProjection projection;
    projection.uv = 0.0.xx;
    projection.receiver_depth = 1.0;
    projection.valid = false;

    const float4 shadow_clip =
        mul(metadata.cascade_view_proj[cascade_index], float4(world_pos, 1.0));
    if (abs(shadow_clip.w) <= 1.0e-6) {
        return projection;
    }

    const float3 shadow_ndc = shadow_clip.xyz / shadow_clip.w;
    projection.uv = float2(
        shadow_ndc.x * 0.5 + 0.5,
        shadow_ndc.y * -0.5 + 0.5);
    projection.receiver_depth = shadow_ndc.z;
    projection.valid =
        projection.uv.x >= 0.0 && projection.uv.x <= 1.0
        && projection.uv.y >= 0.0 && projection.uv.y <= 1.0
        && projection.receiver_depth > 0.0 && projection.receiver_depth < 1.0;
    return projection;
}

static inline float ComputeDirectionalCascadeBlendBand(
    DirectionalShadowMetadata metadata,
    uint cascade_index)
{
    const float cascade_end = metadata.cascade_distances[cascade_index];
    const float cascade_begin = cascade_index > 0u
        ? metadata.cascade_distances[cascade_index - 1u]
        : 0.0;
    const float cascade_span = max(cascade_end - cascade_begin, 0.001);
    const float cascade_texel_world =
        max(metadata.cascade_world_texel_size[cascade_index], 1.0e-4);
    const float texel_band = cascade_texel_world * 12.0;
    const float proportional_band = cascade_span * 0.03;
    return min(proportional_band, max(texel_band, 0.05));
}

static inline uint SelectDirectionalShadowFilterRadiusTexels(
    DirectionalShadowMetadata metadata,
    uint cascade_index)
{
    const float base_texel_world =
        max(metadata.cascade_world_texel_size[0], 1.0e-4);
    const float cascade_texel_world =
        max(metadata.cascade_world_texel_size[cascade_index], base_texel_world);
    const float texel_ratio = cascade_texel_world / base_texel_world;
    return texel_ratio > 2.5 ? 2u : 1u;
}

static inline float SampleDirectionalShadowPcf3x3(
    Texture2DArray<float> shadow_texture,
    float2 uv,
    float receiver_depth,
    uint layer)
{
    uint width = 0u;
    uint height = 0u;
    uint layers = 0u;
    shadow_texture.GetDimensions(width, height, layers);
    if (width == 0u || height == 0u || layer >= layers) {
        return 1.0;
    }

    const int2 texture_size = int2((int)width, (int)height);
    const float2 pixel = uv * float2(width, height);
    const int2 center = int2(pixel);

    float visibility = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const int2 coord = clamp(center + int2(x, y), int2(0, 0), texture_size - 1);
            const float stored_depth = shadow_texture.Load(int4(coord, (int)layer, 0));
            visibility += receiver_depth <= stored_depth + 0.0005 ? 1.0 : 0.0;
        }
    }

    return visibility * (1.0 / 9.0);
}

static inline float SampleDirectionalShadowComparisonTent3x3(
    Texture2DArray<float> shadow_texture,
    float2 uv,
    float receiver_depth,
    uint layer)
{
    uint width = 0u;
    uint height = 0u;
    uint layers = 0u;
    shadow_texture.GetDimensions(width, height, layers);
    if (width == 0u || height == 0u || layer >= layers) {
        return 1.0;
    }

    SamplerComparisonState shadow_sampler =
        SamplerDescriptorHeap[kShadowComparisonSamplerIndex];
    const float2 texel_size = 1.0 / float2(width, height);

    static const float kKernelWeights[3] = { 1.0, 2.0, 1.0 };

    float visibility = 0.0;
    float total_weight = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float weight = kKernelWeights[x + 1] * kKernelWeights[y + 1];
            const float2 sample_uv = uv + float2((float)x, (float)y) * texel_size;
            visibility += weight * shadow_texture.SampleCmpLevelZero(
                shadow_sampler, float3(sample_uv, (float)layer), receiver_depth);
            total_weight += weight;
        }
    }

    return visibility / max(total_weight, 1.0);
}

static inline float SampleDirectionalShadowComparisonTent5x5(
    Texture2DArray<float> shadow_texture,
    float2 uv,
    float receiver_depth,
    uint layer)
{
    uint width = 0u;
    uint height = 0u;
    uint layers = 0u;
    shadow_texture.GetDimensions(width, height, layers);
    if (width == 0u || height == 0u || layer >= layers) {
        return 1.0;
    }

    SamplerComparisonState shadow_sampler =
        SamplerDescriptorHeap[kShadowComparisonSamplerIndex];
    const float2 texel_size = 1.0 / float2(width, height);

    static const float kKernelWeights[5] = { 1.0, 4.0, 6.0, 4.0, 1.0 };

    float visibility = 0.0;
    float total_weight = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            const float weight = kKernelWeights[x + 2] * kKernelWeights[y + 2];
            const float2 sample_uv = uv + float2((float)x, (float)y) * texel_size;
            visibility += weight * shadow_texture.SampleCmpLevelZero(
                shadow_sampler, float3(sample_uv, (float)layer), receiver_depth);
            total_weight += weight;
        }
    }

    return visibility / max(total_weight, 1.0);
}

static inline float SampleDirectionalShadowCascadeVisibility(
    DirectionalShadowMetadata metadata,
    Texture2DArray<float> shadow_texture,
    uint cascade_index,
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const float texel_world = max(metadata.cascade_world_texel_size[cascade_index], 0.0);
    const uint filter_radius_texels =
        SelectDirectionalShadowFilterRadiusTexels(metadata, cascade_index);
    const float ndotl = saturate(dot(normal_ws, light_dir_ws));
    const float slope_factor = 1.0 - ndotl;
    const float filter_bias_scale = filter_radius_texels <= 1u ? 0.85 : 1.0;
    const float renderer_normal_bias = texel_world
        * lerp(0.55, 1.5, slope_factor) * filter_bias_scale;
    const float renderer_constant_bias = texel_world
        * lerp(0.03, 0.18, slope_factor) * filter_bias_scale;
    const float normal_bias = metadata.normal_bias + renderer_normal_bias;
    const float constant_bias = metadata.constant_bias + renderer_constant_bias;

    const float3 biased_world_pos =
        world_pos + normal_ws * normal_bias + light_dir_ws * constant_bias;
    const DirectionalShadowProjection projection =
        ProjectDirectionalShadowCascade(metadata, cascade_index, biased_world_pos);
    if (!projection.valid) {
        return 1.0;
    }

    const uint layer = metadata.resource_index + cascade_index;
#if OXYGEN_SHADOW_USE_MANUAL_COMPARE_FALLBACK
    return SampleDirectionalShadowPcf3x3(
        shadow_texture, projection.uv, projection.receiver_depth, layer);
#else
    if (filter_radius_texels <= 1u) {
        return SampleDirectionalShadowComparisonTent3x3(
            shadow_texture, projection.uv, projection.receiver_depth, layer);
    }
    return SampleDirectionalShadowComparisonTent5x5(
        shadow_texture, projection.uv, projection.receiver_depth, layer);
#endif
}

static inline float ComputeConventionalDirectionalShadowVisibility(
    uint payload_index,
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.directional_shadow_metadata_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.directional_shadow_metadata_slot)
        || shadow_bindings.directional_shadow_texture_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.directional_shadow_texture_slot)) {
        return 1.0;
    }

    StructuredBuffer<DirectionalShadowMetadata> metadata_buffer =
        ResourceDescriptorHeap[shadow_bindings.directional_shadow_metadata_slot];
    uint metadata_count = 0u;
    uint metadata_stride = 0u;
    metadata_buffer.GetDimensions(metadata_count, metadata_stride);
    if (payload_index >= metadata_count) {
        return 1.0;
    }

    const DirectionalShadowMetadata metadata = metadata_buffer[payload_index];
    if (metadata.implementation_kind != 1u || metadata.cascade_count == 0u) {
        return 1.0;
    }

    Texture2DArray<float> shadow_texture =
        ResourceDescriptorHeap[shadow_bindings.directional_shadow_texture_slot];

    const float view_depth = max(0.0, -mul(view_matrix, float4(world_pos, 1.0)).z);
    const uint interval_index = SelectDirectionalShadowCascade(metadata, view_depth);
    const uint cascade_count = max(1u, metadata.cascade_count);

    uint cascade_index = min(interval_index, cascade_count - 1u);
    DirectionalShadowProjection cascade_projection =
        ProjectDirectionalShadowCascade(metadata, cascade_index, world_pos);
    if (!cascade_projection.valid) {
        if (cascade_index + 1u < cascade_count) {
            const DirectionalShadowProjection next_projection =
                ProjectDirectionalShadowCascade(metadata, cascade_index + 1u, world_pos);
            if (next_projection.valid) {
                cascade_index += 1u;
                cascade_projection = next_projection;
            }
        }
        if (!cascade_projection.valid && cascade_index > 0u) {
            const DirectionalShadowProjection prev_projection =
                ProjectDirectionalShadowCascade(metadata, cascade_index - 1u, world_pos);
            if (prev_projection.valid) {
                cascade_index -= 1u;
                cascade_projection = prev_projection;
            }
        }
    }

    if (!cascade_projection.valid) {
        return 1.0;
    }

    const float visibility = SampleDirectionalShadowCascadeVisibility(
        metadata, shadow_texture, cascade_index, world_pos, normal_ws, light_dir_ws);

    if (cascade_index + 1u >= cascade_count || cascade_index != interval_index) {
        return visibility;
    }

    const float cascade_end = metadata.cascade_distances[cascade_index];
    const float blend_band = ComputeDirectionalCascadeBlendBand(metadata, cascade_index);
    const float blend_start = cascade_end - blend_band;
    if (view_depth <= blend_start) {
        return visibility;
    }

    const DirectionalShadowProjection next_projection = ProjectDirectionalShadowCascade(
        metadata, cascade_index + 1u, world_pos);
    if (!next_projection.valid) {
        return visibility;
    }

    const float next_visibility = SampleDirectionalShadowCascadeVisibility(
        metadata, shadow_texture, cascade_index + 1u, world_pos, normal_ws, light_dir_ws);
    const float blend_t = saturate((view_depth - blend_start) / max(blend_band, 1.0e-4));
    return lerp(visibility, next_visibility, blend_t);
}

static inline float ComputeShadowVisibility(
    uint shadow_index,
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    if (shadow_index == K_INVALID_BINDLESS_INDEX) {
        return 1.0;
    }

    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.shadow_instance_metadata_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.shadow_instance_metadata_slot)) {
        return 1.0;
    }

    StructuredBuffer<ShadowInstanceMetadata> shadow_instances =
        ResourceDescriptorHeap[shadow_bindings.shadow_instance_metadata_slot];
    uint instance_count = 0u;
    uint instance_stride = 0u;
    shadow_instances.GetDimensions(instance_count, instance_stride);
    if (shadow_index >= instance_count) {
        return 1.0;
    }

    const ShadowInstanceMetadata shadow_instance = shadow_instances[shadow_index];
    if ((shadow_instance.flags & SHADOW_PRODUCT_FLAG_VALID) == 0u) {
        return 1.0;
    }

    if (shadow_instance.domain == SHADOW_DOMAIN_DIRECTIONAL
        && shadow_instance.implementation_kind == SHADOW_IMPLEMENTATION_CONVENTIONAL) {
        return ComputeConventionalDirectionalShadowVisibility(
            shadow_instance.payload_index, world_pos, normal_ws, light_dir_ws);
    }

    return 1.0;
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_SHADOWHELPERS_HLSLI

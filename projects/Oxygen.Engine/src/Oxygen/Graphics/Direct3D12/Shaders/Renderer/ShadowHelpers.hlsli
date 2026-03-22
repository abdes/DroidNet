//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_SHADOWHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_SHADOWHELPERS_HLSLI

#include "Renderer/DirectionalShadowMetadata.hlsli"
#include "Renderer/DirectionalVirtualShadowMetadata.hlsli"
#include "Renderer/ShadowFrameBindings.hlsli"
#include "Renderer/ShadowInstanceMetadata.hlsli"
#include "Renderer/VirtualShadowPageAccess.hlsli"
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

static inline bool ProjectDirectionalVirtualClip(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    float2 light_space_xy,
    out float2 page_coord);

static inline float2 ComputeDirectionalVirtualTexelCenterOffsetUv(
    DirectionalVirtualShadowMetadata metadata,
    float2 clip_uv,
    uint sampling_filter_radius_texels);

static inline float2 ComputeDepthSlopeDirectionalUV(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    float3 estimated_geo_world_normal,
    bool clamp_slope = true);

static inline uint ResolveDirectionalVirtualEstimatedClipIndex(
    DirectionalVirtualShadowMetadata metadata,
    float3 world_pos);

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

static inline float SampleDirectionalVirtualShadowComparison(
    Texture2D<float> shadow_texture,
    float2 atlas_uv,
    float receiver_depth)
{
#if OXYGEN_SHADOW_USE_MANUAL_COMPARE_FALLBACK
    uint width = 0u;
    uint height = 0u;
    shadow_texture.GetDimensions(width, height);
    if (width == 0u || height == 0u) {
        return 1.0;
    }

    const uint2 pool_texel = min(
        uint2(atlas_uv * float2(width, height)),
        uint2(width - 1u, height - 1u));
    const float stored_depth = shadow_texture.Load(int3(pool_texel, 0));
    return receiver_depth <= stored_depth ? 1.0 : 0.0;
#else
    SamplerComparisonState shadow_sampler =
        SamplerDescriptorHeap[kShadowComparisonSamplerIndex];
    return shadow_texture.SampleCmpLevelZero(
        shadow_sampler, atlas_uv, receiver_depth);
#endif
}

static inline float2 ComputeDepthSlopeDirectionalUV(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    float3 estimated_geo_world_normal,
    bool clamp_slope)
{
    if (clip_index >= metadata.clip_level_count || metadata.pages_per_axis == 0u) {
        return 0.0.xx;
    }

    const float3 normal_ls =
        mul(metadata.light_view, float4(estimated_geo_world_normal, 0.0f)).xyz;
    const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[clip_index];
    const float clip_world_extent =
        max(clip.origin_page_scale.z * max((float)metadata.pages_per_axis, 1.0f), 1.0e-4f);
    const float3 normal_plane_uv = float3(
        normal_ls.x / clip_world_extent,
        -normal_ls.y / clip_world_extent,
        normal_ls.z * clip.origin_page_scale.w);
    if (abs(normal_plane_uv.z) <= 1.0e-6f) {
        return 0.0.xx;
    }

    float2 depth_slope_uv = -normal_plane_uv.xy / normal_plane_uv.z;
    const float2 clamp_value = 0.05.xx;
    return clamp_slope ? clamp(depth_slope_uv, -clamp_value, clamp_value) : depth_slope_uv;
}

static inline float ComputeOptimalSlopeBiasDirectional(
    float2 depth_slope_uv,
    float2 offset_uv)
{
    return 2.0f * max(0.0f, dot(depth_slope_uv, offset_uv));
}

static inline float ComputeDirectionalVirtualOptimalSlopeBiasAtlas(
    DirectionalVirtualShadowMetadata metadata,
    uint resolved_clip_index,
    float3 estimated_geo_world_normal,
    float2 resolved_clip_uv,
    uint fallback_lod_offset,
    uint sampling_filter_radius_texels)
{
    if (resolved_clip_index >= metadata.clip_level_count) {
        return 0.0f;
    }

    const float2 depth_slope_uv = ComputeDepthSlopeDirectionalUV(
        metadata, resolved_clip_index, estimated_geo_world_normal);
    const float2 texel_center_offset_uv = ComputeDirectionalVirtualTexelCenterOffsetUv(
        metadata, resolved_clip_uv, sampling_filter_radius_texels);
    const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[resolved_clip_index];
    float optimal_slope_bias = ComputeOptimalSlopeBiasDirectional(
        depth_slope_uv, texel_center_offset_uv);

    optimal_slope_bias = min(
        optimal_slope_bias, abs(100.0f * clip.origin_page_scale.w));
    optimal_slope_bias *= metadata.receiver_slope_bias_scale;
    optimal_slope_bias *= (float)(1u << min(fallback_lod_offset, 30u));
    return optimal_slope_bias;
}

static inline float ComputeDirectionalVirtualResolvedSlopeBiasAtlas(
    DirectionalVirtualShadowMetadata metadata,
    uint requested_clip_index,
    uint resolved_clip_index,
    uint fallback_lod_offset,
    float3 estimated_geo_world_normal,
    float2 resolved_clip_uv,
    uint sampling_filter_radius_texels)
{
    if (requested_clip_index == resolved_clip_index && fallback_lod_offset == 0u) {
        return 0.0f;
    }

    return ComputeDirectionalVirtualOptimalSlopeBiasAtlas(
        metadata,
        resolved_clip_index,
        estimated_geo_world_normal,
        resolved_clip_uv,
        fallback_lod_offset,
        sampling_filter_radius_texels);
}

struct DirectionalVirtualResolvedPageLookup
{
    bool valid;
    uint requested_clip_index;
    uint resolved_clip_index;
    float2 requested_page_coord;
    uint page_x;
    uint page_y;
    float2 page_coord;
    VirtualShadowPageTableEntry entry;
};

static inline float3 ComputeDirectionalVirtualBiasedWorldPosition(
    DirectionalVirtualShadowMetadata metadata,
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const float3 safe_normal_ws =
        normal_ws * rsqrt(max(dot(normal_ws, normal_ws), 1.0e-8f));
    const uint clip_index = ResolveDirectionalVirtualEstimatedClipIndex(
        metadata, world_pos);
    const float page_texels = max((float)metadata.page_size_texels, 1.0f);
    const float guard_texels = min(max(1.0f, page_texels * 0.25f), 1.0f);
    const float logical_texel_count =
        max(page_texels - 2.0 * guard_texels, 1.0f);
    const float texel_world = max(
        max(metadata.clip_metadata[clip_index].origin_page_scale.z, 1.0e-4f)
            / logical_texel_count,
        1.0e-4f);
    const float3 safe_light_dir_ws =
        light_dir_ws * rsqrt(max(dot(light_dir_ws, light_dir_ws), 1.0e-8f));
    const float ndotl = saturate(dot(safe_normal_ws, safe_light_dir_ws));
    const float slope_factor = 1.0f - ndotl;
    const float filter_bias_scale = 0.85f;
    const float renderer_normal_bias = texel_world
        * lerp(0.55f, 1.5f, slope_factor) * filter_bias_scale
        * metadata.receiver_normal_bias_scale;
    const float normal_bias = metadata.normal_bias + renderer_normal_bias;
    const float receiver_constant_bias =
        texel_world * lerp(0.03f, 0.18f, slope_factor) * filter_bias_scale
        * metadata.receiver_constant_bias_scale;
    return world_pos
        + safe_normal_ws * normal_bias
        + safe_light_dir_ws * receiver_constant_bias;
}

struct DirectionalVirtualClipRelativeTransform
{
    bool valid;
    float2 page_coord_scale;
    float2 page_coord_bias;
    float depth_scale;
    float depth_bias;
    float lod_scale;
};

struct DirectionalVirtualShadowDebugResult
{
    bool active;
    bool mapped;
    bool fallback_used;
    bool same_clip_resolve;
    uint raw_requested_clip_index;
    uint requested_clip_index;
    uint resolved_clip_index;
    uint fallback_lod_offset;
    bool requested_page_valid;
    bool requested_page_requested_this_frame;
    bool requested_page_has_any_lod;
    bool requested_page_has_current_lod;
    bool requested_page_has_detail_geometry;
    bool requested_page_has_hierarchy_detail;
    bool biased_page_projects_in_clip;
    bool unbiased_page_valid;
    bool unbiased_page_requested_this_frame;
    bool unbiased_page_has_any_lod;
    bool unbiased_page_has_current_lod;
    float receiver_depth;
    float stored_depth;
    float depth_delta;
};

static inline DirectionalVirtualResolvedPageLookup MakeInvalidDirectionalVirtualResolvedPageLookup()
{
    DirectionalVirtualResolvedPageLookup lookup;
    lookup.valid = false;
    lookup.requested_clip_index = 0u;
    lookup.resolved_clip_index = 0u;
    lookup.requested_page_coord = 0.0.xx;
    lookup.page_x = 0u;
    lookup.page_y = 0u;
    lookup.page_coord = 0.0.xx;
    lookup.entry.tile_x = 0u;
    lookup.entry.tile_y = 0u;
    lookup.entry.fallback_lod_offset = 0u;
    lookup.entry.current_lod_valid = false;
    lookup.entry.any_lod_valid = false;
    lookup.entry.requested_this_frame = false;
    return lookup;
}

static inline DirectionalVirtualShadowDebugResult
MakeInvalidDirectionalVirtualShadowDebugResult()
{
    DirectionalVirtualShadowDebugResult result;
    result.active = false;
    result.mapped = false;
    result.fallback_used = false;
    result.same_clip_resolve = false;
    result.raw_requested_clip_index = 0u;
    result.requested_clip_index = 0u;
    result.resolved_clip_index = 0u;
    result.fallback_lod_offset = 0u;
    result.requested_page_valid = false;
    result.requested_page_requested_this_frame = false;
    result.requested_page_has_any_lod = false;
    result.requested_page_has_current_lod = false;
    result.requested_page_has_detail_geometry = false;
    result.requested_page_has_hierarchy_detail = false;
    result.biased_page_projects_in_clip = false;
    result.unbiased_page_valid = false;
    result.unbiased_page_requested_this_frame = false;
    result.unbiased_page_has_any_lod = false;
    result.unbiased_page_has_current_lod = false;
    result.receiver_depth = 0.0f;
    result.stored_depth = 0.0f;
    result.depth_delta = 0.0f;
    return result;
}

static inline void InspectDirectionalVirtualRequestedPageState(
    DirectionalVirtualShadowMetadata metadata,
    StructuredBuffer<uint> page_table,
    StructuredBuffer<uint> page_flags,
    uint requested_clip_index,
    float2 light_space_xy,
    inout DirectionalVirtualShadowDebugResult result)
{
    if (requested_clip_index >= metadata.clip_level_count
        || metadata.pages_per_axis == 0u) {
        return;
    }

    float2 requested_page_coord = 0.0.xx;
    const bool projects_in_clip = ProjectDirectionalVirtualClip(
        metadata, requested_clip_index, light_space_xy, requested_page_coord);
    result.biased_page_projects_in_clip = projects_in_clip;
    if (!projects_in_clip) {
        return;
    }

    const uint requested_page_x = min((uint)requested_page_coord.x, metadata.pages_per_axis - 1u);
    const uint requested_page_y = min((uint)requested_page_coord.y, metadata.pages_per_axis - 1u);
    const uint pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis;
    const uint page_table_index = metadata.page_table_offset
        + requested_clip_index * pages_per_level
        + requested_page_y * metadata.pages_per_axis
        + requested_page_x;

    uint page_table_count = 0u;
    uint page_table_stride = 0u;
    page_table.GetDimensions(page_table_count, page_table_stride);
    if (page_table_index >= page_table_count) {
        return;
    }

    result.requested_page_valid = true;

    const VirtualShadowPageTableEntry entry =
        DecodeVirtualShadowPageTableEntry(page_table[page_table_index]);
    result.requested_page_requested_this_frame = entry.requested_this_frame;
    result.requested_page_has_any_lod = entry.any_lod_valid;
    result.requested_page_has_current_lod = entry.current_lod_valid;

    uint page_flags_count = 0u;
    uint page_flags_stride = 0u;
    page_flags.GetDimensions(page_flags_count, page_flags_stride);
    if (page_table_index >= page_flags_count) {
        return;
    }

    const uint flags = page_flags[page_table_index];
    result.requested_page_has_detail_geometry =
        VirtualShadowPageHasFlag(flags, OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY);
    result.requested_page_has_hierarchy_detail =
        VirtualShadowPageHasFlag(flags, OXYGEN_VSM_PAGE_FLAG_HIERARCHY_DETAIL_DESCENDANT);
}

static inline DirectionalVirtualClipRelativeTransform
BuildDirectionalVirtualClipRelativeTransform(
    DirectionalVirtualShadowMetadata metadata,
    uint requested_clip_index,
    uint resolved_clip_index)
{
    DirectionalVirtualClipRelativeTransform transform;
    transform.valid = false;
    transform.page_coord_scale = 1.0.xx;
    transform.page_coord_bias = 0.0.xx;
    transform.depth_scale = 1.0;
    transform.depth_bias = 0.0;
    transform.lod_scale = 1.0;

    if (requested_clip_index >= metadata.clip_level_count
        || resolved_clip_index >= metadata.clip_level_count) {
        return transform;
    }

    const DirectionalVirtualClipMetadata requested_clip =
        metadata.clip_metadata[requested_clip_index];
    const DirectionalVirtualClipMetadata resolved_clip =
        metadata.clip_metadata[resolved_clip_index];
    const float requested_page_world =
        max(requested_clip.origin_page_scale.z, 1.0e-4);
    const float resolved_page_world =
        max(resolved_clip.origin_page_scale.z, 1.0e-4);
    transform.valid = true;
    transform.page_coord_scale =
        (requested_page_world / resolved_page_world).xx;
    transform.page_coord_bias =
        (requested_clip.origin_page_scale.xy - resolved_clip.origin_page_scale.xy)
        / resolved_page_world;

    const float requested_depth_scale = requested_clip.origin_page_scale.w;
    const float resolved_depth_scale = resolved_clip.origin_page_scale.w;
    if (abs(requested_depth_scale) > 1.0e-8
        && abs(resolved_depth_scale) > 1.0e-8) {
        transform.depth_scale = resolved_depth_scale / requested_depth_scale;
        transform.depth_bias = resolved_clip.bias_reserved.x
            - requested_clip.bias_reserved.x * transform.depth_scale;
    }
    transform.lod_scale = resolved_page_world / requested_page_world;
    return transform;
}

static inline int ResolveDirectionalVirtualClipGridOriginComponent(
    int4 packed_origins[3],
    uint clip_index)
{
    if (clip_index >= OXYGEN_MAX_VIRTUAL_DIRECTIONAL_CLIP_LEVELS) {
        return 0;
    }

    const uint packed_index = clip_index / 4u;
    const uint packed_lane = clip_index % 4u;
    const int4 packed = packed_origins[packed_index];
    switch (packed_lane) {
        case 0u:
            return packed.x;
        case 1u:
            return packed.y;
        case 2u:
            return packed.z;
        default:
            return packed.w;
    }
}

static inline int ResolveDirectionalVirtualClipGridOriginX(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index)
{
    return ResolveDirectionalVirtualClipGridOriginComponent(
        metadata.clip_grid_origin_x_packed, clip_index);
}

static inline int ResolveDirectionalVirtualClipGridOriginY(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index)
{
    return ResolveDirectionalVirtualClipGridOriginComponent(
        metadata.clip_grid_origin_y_packed, clip_index);
}

static inline float2 TransformDirectionalRequestedPageCoordToResolvedClip(
    float2 requested_page_coord,
    DirectionalVirtualClipRelativeTransform transform)
{
    return requested_page_coord * transform.page_coord_scale
        + transform.page_coord_bias;
}

static inline bool ResolveDirectionalRequestedPageCoordToResolvedClip(
    DirectionalVirtualShadowMetadata metadata,
    uint requested_clip_index,
    uint resolved_clip_index,
    float2 requested_page_coord,
    DirectionalVirtualClipRelativeTransform transform,
    out uint2 resolved_page_index,
    out float2 resolved_page_coord)
{
    resolved_page_index = 0u.xx;
    resolved_page_coord = 0.0.xx;
    if (!transform.valid
        || requested_clip_index >= metadata.clip_level_count
        || resolved_clip_index >= metadata.clip_level_count
        || metadata.pages_per_axis == 0u) {
        return false;
    }

    const int requested_clip_level =
        metadata.clip_metadata[requested_clip_index].clipmap_level_data.x;
    const int resolved_clip_level =
        metadata.clip_metadata[resolved_clip_index].clipmap_level_data.x;
    const int clip_level_offset = resolved_clip_level - requested_clip_level;
    if (clip_level_offset < 0 || clip_level_offset > 30) {
        return false;
    }

    const uint2 requested_page_index = uint2(requested_page_coord);
    int2 absolute_requested_page = int2(
        (int)requested_page_index.x
            + ResolveDirectionalVirtualClipGridOriginX(
                metadata, requested_clip_index),
        (int)requested_page_index.y
            + ResolveDirectionalVirtualClipGridOriginY(
                metadata, requested_clip_index));
    if (clip_level_offset > 0) {
        absolute_requested_page >>= clip_level_offset;
    }

    const int2 resolved_page_index_signed = absolute_requested_page - int2(
        ResolveDirectionalVirtualClipGridOriginX(metadata, resolved_clip_index),
        ResolveDirectionalVirtualClipGridOriginY(metadata, resolved_clip_index));
    if (resolved_page_index_signed.x < 0
        || resolved_page_index_signed.y < 0
        || resolved_page_index_signed.x >= (int)metadata.pages_per_axis
        || resolved_page_index_signed.y >= (int)metadata.pages_per_axis) {
        return false;
    }

    resolved_page_index = uint2(
        (uint)resolved_page_index_signed.x,
        (uint)resolved_page_index_signed.y);
    const float2 resolved_page_min = float2(resolved_page_index);
    const float2 resolved_page_max = resolved_page_min + (1.0 - 1.0e-5).xx;
    resolved_page_coord = clamp(
        TransformDirectionalRequestedPageCoordToResolvedClip(
            requested_page_coord, transform),
        resolved_page_min,
        resolved_page_max);
    return true;
}

static inline float RemapDirectionalRequestedDepthToResolvedClip(
    float requested_depth,
    DirectionalVirtualClipRelativeTransform transform)
{
    return requested_depth * transform.depth_scale + transform.depth_bias;
}

static inline float RemapDirectionalResolvedDepthToRequestedClip(
    float resolved_depth,
    DirectionalVirtualClipRelativeTransform transform)
{
    if (abs(transform.depth_scale) <= 1.0e-8) {
        return resolved_depth;
    }
    return (resolved_depth - transform.depth_bias) / transform.depth_scale;
}

static inline float2 MakeDirectionalVirtualClipUv(
    DirectionalVirtualShadowMetadata metadata,
    float2 page_coord)
{
    const float inv_pages_per_axis =
        1.0f / max((float)metadata.pages_per_axis, 1.0f);
    return float2(
        page_coord.x * inv_pages_per_axis,
        1.0f - page_coord.y * inv_pages_per_axis);
}

static inline uint ResolveDirectionalVirtualGuardTexels(
    uint page_size_texels,
    uint sampling_filter_radius_texels)
{
    const uint max_guard_texels = max(1u, page_size_texels / 4u);
    return min(max_guard_texels, max(1u, sampling_filter_radius_texels));
}

static inline float ResolveDirectionalVirtualFallbackSlopeBiasScale(
    VirtualShadowPageTableEntry entry)
{
    // Exponential scale: clipmap levels double in texel size per level, so
    // the slope bias must scale by 2^offset, not linearly.  UE5 reference:
    //   OptimalSlopeBias *= float(1u << (SampledId - RequestedId));
    return max(1.0, (float)(1u << entry.fallback_lod_offset));
}

static inline float2 ResolveDirectionalVirtualAtlasUvFromResolvedPageCoord(
    DirectionalVirtualShadowMetadata metadata,
    uint tile_x,
    uint tile_y,
    float2 resolved_page_coord,
    float2 pool_size)
{
    const float guard_texels =
        (float)ResolveDirectionalVirtualGuardTexels(metadata.page_size_texels, 1u);
    const float interior_min =
        guard_texels / max((float)metadata.page_size_texels, 1.0);
    const float interior_span = max(1.0 - 2.0 * interior_min, 1.0e-4);
    const float2 page_extent_uv =
        float2((float)metadata.page_size_texels / pool_size.x,
               (float)metadata.page_size_texels / pool_size.y);
    const float2 atlas_page_origin_uv =
        float2((float)(tile_x * metadata.page_size_texels) / pool_size.x,
               (float)(tile_y * metadata.page_size_texels) / pool_size.y);
    const float2 local_page_uv =
        float2(frac(resolved_page_coord.x), 1.0 - frac(resolved_page_coord.y));
    return atlas_page_origin_uv
        + (interior_min.xx + local_page_uv * interior_span) * page_extent_uv;
}

static inline bool TryResolveDirectionalVirtualPageLookup(
    DirectionalVirtualShadowMetadata metadata,
    StructuredBuffer<uint> page_table,
    uint requested_clip_index,
    float2 light_space_xy,
    out DirectionalVirtualResolvedPageLookup lookup)
{
    lookup = MakeInvalidDirectionalVirtualResolvedPageLookup();
    if (requested_clip_index >= metadata.clip_level_count
        || metadata.pages_per_axis == 0u) {
        return false;
    }

    float2 requested_page_coord = 0.0.xx;
    if (!ProjectDirectionalVirtualClip(
            metadata, requested_clip_index, light_space_xy, requested_page_coord)) {
        return false;
    }

    const uint requested_page_x = min((uint)requested_page_coord.x, metadata.pages_per_axis - 1u);
    const uint requested_page_y = min((uint)requested_page_coord.y, metadata.pages_per_axis - 1u);
    const uint pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis;
    const uint page_table_index = metadata.page_table_offset
        + requested_clip_index * pages_per_level
        + requested_page_y * metadata.pages_per_axis
        + requested_page_x;

    uint page_table_count = 0u;
    uint page_table_stride = 0u;
    page_table.GetDimensions(page_table_count, page_table_stride);
    if (page_table_index >= page_table_count) {
        return false;
    }

    const VirtualShadowPageTableEntry entry =
        DecodeVirtualShadowPageTableEntry(page_table[page_table_index]);
    if (!VirtualShadowPageTableEntryHasAnyLod(entry)) {
        return false;
    }

    const uint resolved_clip_index =
        ResolveVirtualShadowFallbackClipIndex(
            requested_clip_index, metadata.clip_level_count, entry);
    if (resolved_clip_index >= metadata.clip_level_count) {
        return false;
    }

    lookup.valid = true;
    lookup.requested_clip_index = requested_clip_index;
    lookup.resolved_clip_index = resolved_clip_index;
    lookup.requested_page_coord = requested_page_coord;
    const DirectionalVirtualClipRelativeTransform transform =
        BuildDirectionalVirtualClipRelativeTransform(
            metadata, requested_clip_index, resolved_clip_index);
    if (!transform.valid) {
        return false;
    }

    uint2 resolved_page_index = 0u.xx;
    float2 resolved_page_coord = 0.0.xx;
    if (!ResolveDirectionalRequestedPageCoordToResolvedClip(
            metadata,
            requested_clip_index,
            resolved_clip_index,
            requested_page_coord,
            transform,
            resolved_page_index,
            resolved_page_coord)) {
        return false;
    }

    lookup.page_x = resolved_page_index.x;
    lookup.page_y = resolved_page_index.y;
    lookup.page_coord = resolved_page_coord;
    lookup.entry = entry;
    return true;
}

static inline bool ResolveDirectionalVirtualPageResidency(
    DirectionalVirtualShadowMetadata metadata,
    StructuredBuffer<uint> page_table,
    uint clip_index,
    float2 light_space_xy,
    out float2 page_coord)
{
    DirectionalVirtualResolvedPageLookup lookup =
        MakeInvalidDirectionalVirtualResolvedPageLookup();
    if (!TryResolveDirectionalVirtualPageLookup(
            metadata, page_table, clip_index, light_space_xy, lookup)) {
        page_coord = 0.0.xx;
        return false;
    }

    page_coord = lookup.page_coord;
    return VirtualShadowPageTableEntryHasAnyLod(lookup.entry);
}

static inline bool ResolveDirectionalVirtualAtlasUv(
    DirectionalVirtualShadowMetadata metadata,
    StructuredBuffer<uint> page_table,
    uint clip_index,
    float2 light_space_xy,
    float2 pool_size,
    out float2 atlas_uv)
{
    atlas_uv = 0.0.xx;
    if (clip_index >= metadata.clip_level_count
        || metadata.page_size_texels == 0u
        || metadata.pages_per_axis == 0u
        || pool_size.x <= 0.0
        || pool_size.y <= 0.0) {
        return false;
    }

    DirectionalVirtualResolvedPageLookup lookup =
        MakeInvalidDirectionalVirtualResolvedPageLookup();
    if (!TryResolveDirectionalVirtualPageLookup(
            metadata, page_table, clip_index, light_space_xy, lookup)) {
        return false;
    }

    atlas_uv = ResolveDirectionalVirtualAtlasUvFromResolvedPageCoord(
        metadata,
        lookup.entry.tile_x,
        lookup.entry.tile_y,
        lookup.page_coord,
        pool_size);
    return true;
}

static inline float2 ResolveDirectionalVirtualAtlasUvInResolvedPage(
    DirectionalVirtualShadowMetadata metadata,
    uint tile_x,
    uint tile_y,
    uint clip_index,
    uint page_x,
    uint page_y,
    float2 light_space_xy,
    float2 pool_size)
{
    const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[clip_index];
    const float page_world_size = max(clip.origin_page_scale.z, 1.0e-4);
    const float2 page_origin_xy =
        clip.origin_page_scale.xy + float2((float)page_x, (float)page_y) * page_world_size;
    const float2 resolved_page_coord = saturate(
        (light_space_xy - page_origin_xy) / page_world_size);
    return ResolveDirectionalVirtualAtlasUvFromResolvedPageCoord(
        metadata,
        tile_x,
        tile_y,
        float2((float)page_x, (float)page_y) + resolved_page_coord,
        pool_size);
}

static inline float2 ComputeDirectionalVirtualTexelCenterOffsetUv(
    DirectionalVirtualShadowMetadata metadata,
    float2 clip_uv,
    uint sampling_filter_radius_texels)
{
    const float guard_texels =
        (float)ResolveDirectionalVirtualGuardTexels(
            metadata.page_size_texels,
            sampling_filter_radius_texels);
    const float logical_page_texel_count =
        max((float)metadata.page_size_texels - 2.0 * guard_texels, 1.0f);
    const float level_dim_texels =
        max((float)metadata.pages_per_axis * logical_page_texel_count, 1.0f);
    const float2 texel_address_float = clip_uv * level_dim_texels;
    const float2 texel_center =
        floor(texel_address_float) + 0.5.xx;
    return (texel_center - texel_address_float) / level_dim_texels;
}

static inline float SampleVirtualShadowComparisonPoint(
    Texture2D<float> shadow_texture,
    float2 atlas_uv,
    float receiver_depth)
{
    SamplerComparisonState shadow_sampler =
        SamplerDescriptorHeap[kShadowComparisonSamplerIndex];
    return shadow_texture.SampleCmpLevelZero(
        shadow_sampler, atlas_uv, receiver_depth);
}

static inline float SampleVirtualShadowComparisonTent3x3ResolvedPage(
    DirectionalVirtualShadowMetadata metadata,
    Texture2D<float> shadow_texture,
    VirtualShadowPageTableEntry entry,
    float2 atlas_uv,
    float2 pool_size,
    uint sampling_filter_radius_texels,
    float receiver_depth)
{
    SamplerComparisonState shadow_sampler =
        SamplerDescriptorHeap[kShadowComparisonSamplerIndex];
    const float2 texel_size = 1.0 / max(pool_size, 1.0.xx);
    const float guard_texels =
        (float)ResolveDirectionalVirtualGuardTexels(
            metadata.page_size_texels,
            sampling_filter_radius_texels);
    const float interior_min =
        guard_texels / max((float)metadata.page_size_texels, 1.0);
    const float interior_max = max(1.0 - interior_min, interior_min);
    const float2 page_extent_uv =
        float2((float)metadata.page_size_texels / pool_size.x,
               (float)metadata.page_size_texels / pool_size.y);
    const float2 atlas_page_origin_uv =
        float2((float)(entry.tile_x * metadata.page_size_texels) / pool_size.x,
               (float)(entry.tile_y * metadata.page_size_texels) / pool_size.y);
    const float2 interior_min_uv =
        atlas_page_origin_uv + interior_min.xx * page_extent_uv;
    const float2 interior_max_uv =
        atlas_page_origin_uv + interior_max.xx * page_extent_uv;

    static const float kKernelWeights[3] = { 1.0, 2.0, 1.0 };
    float visibility = 0.0;
    float total_weight = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float weight = kKernelWeights[x + 1] * kKernelWeights[y + 1];
            const float2 unclamped_sample_uv =
                atlas_uv + float2((float)x, (float)y) * texel_size;
            const float2 sample_uv =
                clamp(unclamped_sample_uv, interior_min_uv, interior_max_uv);
            visibility += weight * shadow_texture.SampleCmpLevelZero(
                shadow_sampler, sample_uv, receiver_depth);
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

    const float3 biased_world_pos
        = world_pos + normal_ws * normal_bias + light_dir_ws * constant_bias;
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

static inline bool SelectDirectionalVirtualClip(
    DirectionalVirtualShadowMetadata metadata,
    float2 light_space_xy,
    out uint clip_index,
    out float2 page_coord)
{
    clip_index = 0u;
    page_coord = 0.0.xx;

    [unroll]
    for (uint i = 0u; i < OXYGEN_MAX_VIRTUAL_DIRECTIONAL_CLIP_LEVELS; ++i) {
        if (i >= metadata.clip_level_count) {
            break;
        }

        const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[i];
        const float2 local_pages
            = (light_space_xy - clip.origin_page_scale.xy) / max(clip.origin_page_scale.z, 1.0e-4);
        if (local_pages.x >= 0.0 && local_pages.x < metadata.pages_per_axis
            && local_pages.y >= 0.0 && local_pages.y < metadata.pages_per_axis) {
            clip_index = i;
            page_coord = local_pages;
            return true;
        }
    }

    return false;
}

static inline float ComputeDirectionalVirtualLogicalTexelWorld(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    uint filter_radius_texels);

static inline float ComputeDirectionalVirtualContinuousClipLevel(
    DirectionalVirtualShadowMetadata metadata,
    float3 world_pos)
{
    if (metadata.clip_level_count == 0u) {
        return 0.0f;
    }

    const float distance_to_clipmap_origin =
        length(world_pos - metadata.clipmap_selection_world_origin_lod_bias.xyz);
    return (distance_to_clipmap_origin > 1.0e-6f)
        ? log2(max(distance_to_clipmap_origin, 1.0e-6f))
            + metadata.clipmap_selection_world_origin_lod_bias.w
        : metadata.clipmap_selection_world_origin_lod_bias.w;
}

static inline uint ResolveDirectionalVirtualEstimatedClipIndex(
    DirectionalVirtualShadowMetadata metadata,
    float3 world_pos)
{
    if (metadata.clip_level_count == 0u) {
        return 0u;
    }

    const int first_clipmap_level = metadata.clip_metadata[0].clipmap_level_data.x;
    const int requested_clipmap_level =
        (int)floor(ComputeDirectionalVirtualContinuousClipLevel(metadata, world_pos));
    const int requested_clip_index =
        requested_clipmap_level - first_clipmap_level;
    return (uint)clamp(
        requested_clip_index,
        0,
        (int)metadata.clip_level_count - 1);
}

static inline bool SelectDirectionalVirtualRequestedClip(
    DirectionalVirtualShadowMetadata metadata,
    float3 world_pos,
    float2 light_space_xy,
    out uint clip_index,
    out float2 page_coord)
{
    clip_index = 0u;
    page_coord = 0.0.xx;

    if (metadata.clip_level_count == 0u) {
        return false;
    }

    clip_index = ResolveDirectionalVirtualEstimatedClipIndex(metadata, world_pos);
    return ProjectDirectionalVirtualClip(
        metadata, clip_index, light_space_xy, page_coord);
}

static inline bool ProjectDirectionalVirtualClip(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    float2 light_space_xy,
    out float2 page_coord)
{
    page_coord = 0.0.xx;
    if (clip_index >= metadata.clip_level_count) {
        return false;
    }

    const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[clip_index];
    const float page_world_size = max(clip.origin_page_scale.z, 1.0e-4);
    const float2 local_pages =
        (light_space_xy - clip.origin_page_scale.xy) / page_world_size;
    page_coord = local_pages;
    return local_pages.x >= 0.0 && local_pages.x < metadata.pages_per_axis
        && local_pages.y >= 0.0 && local_pages.y < metadata.pages_per_axis;
}

static inline float SampleDirectionalVirtualShadowClipVisibility(
    DirectionalVirtualShadowMetadata metadata,
    StructuredBuffer<uint> page_table,
    Texture2D<float> physical_pool,
    uint clip_index,
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws,
    out bool valid,
    out uint resolved_clip_index,
    out float2 resolved_page_coord)
{
    valid = false;
    resolved_clip_index = clip_index;
    resolved_page_coord = 0.0.xx;
    if (clip_index >= metadata.clip_level_count || metadata.page_size_texels == 0u
        || metadata.pages_per_axis == 0u) {
        return 1.0;
    }

    const float3 biased_world_pos = ComputeDirectionalVirtualBiasedWorldPosition(
        metadata, world_pos, normal_ws, light_dir_ws);
    const float3 light_view_pos =
        mul(metadata.light_view, float4(biased_world_pos, 1.0f)).xyz;
    DirectionalVirtualResolvedPageLookup lookup =
        MakeInvalidDirectionalVirtualResolvedPageLookup();
    if (!TryResolveDirectionalVirtualPageLookup(
            metadata, page_table, clip_index, light_view_pos.xy, lookup)) {
        return 1.0;
    }

    const DirectionalVirtualClipRelativeTransform clip_relative_transform =
        BuildDirectionalVirtualClipRelativeTransform(
            metadata, clip_index, lookup.resolved_clip_index);
    resolved_clip_index = lookup.resolved_clip_index;
    resolved_page_coord = lookup.page_coord;
    const DirectionalVirtualClipMetadata requested_clip =
        metadata.clip_metadata[clip_index];

    uint pool_width = 0u;
    uint pool_height = 0u;
    physical_pool.GetDimensions(pool_width, pool_height);
    if (pool_width == 0u || pool_height == 0u) {
        return 1.0;
    }

    float2 atlas_uv = 0.0.xx;
    if (!ResolveDirectionalVirtualAtlasUv(
            metadata,
            page_table,
            clip_index,
            light_view_pos.xy,
            float2(pool_width, pool_height),
            atlas_uv)) {
        return 1.0;
    }
    const float requested_receiver_depth =
        light_view_pos.z * requested_clip.origin_page_scale.w
        + requested_clip.bias_reserved.x;
    const float receiver_depth =
        RemapDirectionalRequestedDepthToResolvedClip(
            requested_receiver_depth, clip_relative_transform);
    const float center_adjusted_receiver_depth =
        receiver_depth + ComputeDirectionalVirtualResolvedSlopeBiasAtlas(
            metadata,
            clip_index,
            lookup.resolved_clip_index,
            lookup.entry.fallback_lod_offset,
            normal_ws,
            MakeDirectionalVirtualClipUv(metadata, lookup.page_coord),
            1u);

    valid = true;
    return SampleDirectionalVirtualShadowComparison(
        physical_pool, atlas_uv, center_adjusted_receiver_depth);
}

static inline float ComputeVirtualDirectionalShadowVisibility(
    uint payload_index,
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.virtual_directional_shadow_metadata_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.virtual_directional_shadow_metadata_slot)
        || shadow_bindings.virtual_shadow_page_table_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.virtual_shadow_page_table_slot)
        || shadow_bindings.virtual_shadow_physical_pool_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.virtual_shadow_physical_pool_slot)) {
        return 1.0;
    }

    StructuredBuffer<DirectionalVirtualShadowMetadata> metadata_buffer =
        ResourceDescriptorHeap[shadow_bindings.virtual_directional_shadow_metadata_slot];
    uint metadata_count = 0u;
    uint metadata_stride = 0u;
    metadata_buffer.GetDimensions(metadata_count, metadata_stride);
    if (payload_index >= metadata_count) {
        return 1.0;
    }

    const DirectionalVirtualShadowMetadata metadata = metadata_buffer[payload_index];
    if (metadata.clip_level_count == 0u || metadata.pages_per_axis == 0u
        || metadata.page_size_texels == 0u) {
        return 1.0;
    }

    const float3 light_view_pos_unbiased =
        mul(metadata.light_view, float4(world_pos, 1.0)).xyz;

    uint clip_index = 0u;
    float2 clip_page_coord = 0.0.xx;
    if (!SelectDirectionalVirtualRequestedClip(
            metadata,
            world_pos,
            light_view_pos_unbiased.xy,
            clip_index,
            clip_page_coord)) {
        return 1.0;
    }
    const uint sample_clip_index = clip_index;

    StructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[shadow_bindings.virtual_shadow_page_table_slot];
    Texture2D<float> physical_pool =
        ResourceDescriptorHeap[shadow_bindings.virtual_shadow_physical_pool_slot];
    bool active_valid = false;
    uint active_sampled_clip = sample_clip_index;
    float2 active_page_coord = clip_page_coord;
    // Stage 8 contract: sample exactly one footprint-selected clip family and
    // rely on the page-table entry to resolve any coarser continuity fallback.
    // The shader no longer performs its own finer/coarser regime blending on
    // top of page-management output.
    const float active_visibility = SampleDirectionalVirtualShadowClipVisibility(
        metadata,
        page_table,
        physical_pool,
        sample_clip_index,
        world_pos,
        normal_ws,
        light_dir_ws,
        active_valid,
        active_sampled_clip,
        active_page_coord);

    if (!active_valid) {
        return 1.0;
    }

    return active_visibility;
}

static inline DirectionalVirtualShadowDebugResult
ComputeVirtualDirectionalShadowDebugResult(
    uint payload_index,
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    DirectionalVirtualShadowDebugResult result =
        MakeInvalidDirectionalVirtualShadowDebugResult();
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.virtual_directional_shadow_metadata_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.virtual_directional_shadow_metadata_slot)
        || shadow_bindings.virtual_shadow_page_table_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.virtual_shadow_page_table_slot)
        || shadow_bindings.virtual_shadow_page_flags_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.virtual_shadow_page_flags_slot)
        || shadow_bindings.virtual_shadow_physical_pool_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.virtual_shadow_physical_pool_slot)) {
        return result;
    }

    StructuredBuffer<DirectionalVirtualShadowMetadata> metadata_buffer =
        ResourceDescriptorHeap[shadow_bindings.virtual_directional_shadow_metadata_slot];
    uint metadata_count = 0u;
    uint metadata_stride = 0u;
    metadata_buffer.GetDimensions(metadata_count, metadata_stride);
    if (payload_index >= metadata_count) {
        return result;
    }

    const DirectionalVirtualShadowMetadata metadata = metadata_buffer[payload_index];
    if (metadata.clip_level_count == 0u || metadata.pages_per_axis == 0u
        || metadata.page_size_texels == 0u) {
        return result;
    }

    const float3 light_view_pos_unbiased =
        mul(metadata.light_view, float4(world_pos, 1.0f)).xyz;
    result.raw_requested_clip_index =
        ResolveDirectionalVirtualEstimatedClipIndex(metadata, world_pos);
    uint requested_clip_index = 0u;
    float2 requested_page_coord = 0.0.xx;
    if (!SelectDirectionalVirtualRequestedClip(
            metadata,
            world_pos,
            light_view_pos_unbiased.xy,
            requested_clip_index,
            requested_page_coord)) {
        result.active = true;
        return result;
    }
    result.requested_clip_index = requested_clip_index;
    result.resolved_clip_index = requested_clip_index;

    const float3 biased_world_pos = ComputeDirectionalVirtualBiasedWorldPosition(
        metadata, world_pos, normal_ws, light_dir_ws);
    const float3 light_view_pos =
        mul(metadata.light_view, float4(biased_world_pos, 1.0f)).xyz;
    const uint sample_clip_index = requested_clip_index;
    result.resolved_clip_index = sample_clip_index;

    StructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[shadow_bindings.virtual_shadow_page_table_slot];
    StructuredBuffer<uint> page_flags =
        ResourceDescriptorHeap[shadow_bindings.virtual_shadow_page_flags_slot];
    Texture2D<float> physical_pool =
        ResourceDescriptorHeap[shadow_bindings.virtual_shadow_physical_pool_slot];
    DirectionalVirtualShadowDebugResult unbiased_request_state =
        MakeInvalidDirectionalVirtualShadowDebugResult();
    InspectDirectionalVirtualRequestedPageState(
        metadata,
        page_table,
        page_flags,
        requested_clip_index,
        light_view_pos_unbiased.xy,
        unbiased_request_state);
    result.unbiased_page_valid = unbiased_request_state.requested_page_valid;
    result.unbiased_page_requested_this_frame =
        unbiased_request_state.requested_page_requested_this_frame;
    result.unbiased_page_has_any_lod =
        unbiased_request_state.requested_page_has_any_lod;
    result.unbiased_page_has_current_lod =
        unbiased_request_state.requested_page_has_current_lod;
    InspectDirectionalVirtualRequestedPageState(
        metadata,
        page_table,
        page_flags,
        sample_clip_index,
        light_view_pos.xy,
        result);
    DirectionalVirtualResolvedPageLookup lookup =
        MakeInvalidDirectionalVirtualResolvedPageLookup();
    if (!TryResolveDirectionalVirtualPageLookup(
            metadata, page_table, sample_clip_index, light_view_pos.xy, lookup)) {
        result.active = true;
        return result;
    }

    uint pool_width = 0u;
    uint pool_height = 0u;
    physical_pool.GetDimensions(pool_width, pool_height);
    if (pool_width == 0u || pool_height == 0u) {
        result.active = true;
        return result;
    }

    float2 atlas_uv = 0.0.xx;
    if (!ResolveDirectionalVirtualAtlasUv(
            metadata,
            page_table,
            sample_clip_index,
            light_view_pos.xy,
            float2(pool_width, pool_height),
            atlas_uv)) {
        result.active = true;
        return result;
    }

    const DirectionalVirtualClipRelativeTransform transform =
        BuildDirectionalVirtualClipRelativeTransform(
            metadata, sample_clip_index, lookup.resolved_clip_index);
    const DirectionalVirtualClipMetadata requested_clip =
        metadata.clip_metadata[sample_clip_index];
    const float requested_receiver_depth =
        light_view_pos.z * requested_clip.origin_page_scale.w
        + requested_clip.bias_reserved.x;
    const float receiver_depth =
        RemapDirectionalRequestedDepthToResolvedClip(
            requested_receiver_depth, transform);
    const float adjusted_receiver_depth =
        receiver_depth + ComputeDirectionalVirtualResolvedSlopeBiasAtlas(
            metadata,
            sample_clip_index,
            lookup.resolved_clip_index,
            lookup.entry.fallback_lod_offset,
            normal_ws,
            MakeDirectionalVirtualClipUv(metadata, lookup.page_coord),
            1u);
    const uint2 pool_texel = min(
        uint2(atlas_uv * float2(pool_width, pool_height)),
        uint2(pool_width - 1u, pool_height - 1u));
    const float stored_depth = physical_pool.Load(int3(pool_texel, 0));

    result.active = true;
    result.mapped = true;
    result.requested_clip_index = sample_clip_index;
    result.resolved_clip_index = lookup.resolved_clip_index;
    result.same_clip_resolve = lookup.resolved_clip_index == sample_clip_index;
    result.fallback_used = lookup.entry.fallback_lod_offset > 0u
        || lookup.resolved_clip_index != sample_clip_index;
    result.fallback_lod_offset = lookup.entry.fallback_lod_offset;
    result.receiver_depth = adjusted_receiver_depth;
    result.stored_depth = stored_depth;
    result.depth_delta = adjusted_receiver_depth - stored_depth;
    return result;
}

static inline float3 ComputeVirtualDirectionalShadowDebugColor(
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.sun_shadow_index == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    StructuredBuffer<ShadowInstanceMetadata> shadow_instances =
        ResourceDescriptorHeap[shadow_bindings.shadow_instance_metadata_slot];
    uint instance_count = 0u;
    uint instance_stride = 0u;
    shadow_instances.GetDimensions(instance_count, instance_stride);
    if (shadow_bindings.sun_shadow_index >= instance_count) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const ShadowInstanceMetadata shadow_instance =
        shadow_instances[shadow_bindings.sun_shadow_index];
    if (shadow_instance.domain != SHADOW_DOMAIN_DIRECTIONAL
        || shadow_instance.implementation_kind != SHADOW_IMPLEMENTATION_VIRTUAL) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const DirectionalVirtualShadowDebugResult debug =
        ComputeVirtualDirectionalShadowDebugResult(
            shadow_instance.payload_index, world_pos, normal_ws, light_dir_ws);
    if (!debug.active) {
        return float3(1.0f, 0.0f, 1.0f);
    }
    if (!debug.mapped) {
        return float3(0.0f, 0.35f, 1.0f);
    }
    if (debug.fallback_used) {
        return float3(1.0f, 0.9f, 0.0f);
    }

    const float delta = debug.depth_delta;
    const float abs_delta = abs(delta);

    // Acne is a near-threshold compare problem. Show only the narrow band
    // around the decision boundary so periodic floor self-shadowing lights up.
    const float acne_window = 4.0e-4f;
    if (abs_delta >= acne_window) {
        return 0.10.xxx;
    }

    // Peak brightness at the compare boundary and fade out quickly so only
    // threshold-sensitive pixels remain visible on the scene.
    const float proximity = 1.0f - saturate(abs_delta / acne_window);
    const float intensity = proximity * proximity;

    return delta < 0.0f
        ? float3(0.05f, 0.15f + 0.85f * intensity, 0.05f)
        : float3(0.15f + 0.85f * intensity, 0.05f, 0.05f);
}

static inline float3 ComputeVirtualDirectionalShadowDepthDebugColor(
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws,
    bool show_receiver_depth)
{
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.sun_shadow_index == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    StructuredBuffer<ShadowInstanceMetadata> shadow_instances =
        ResourceDescriptorHeap[shadow_bindings.shadow_instance_metadata_slot];
    uint instance_count = 0u;
    uint instance_stride = 0u;
    shadow_instances.GetDimensions(instance_count, instance_stride);
    if (shadow_bindings.sun_shadow_index >= instance_count) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const ShadowInstanceMetadata shadow_instance =
        shadow_instances[shadow_bindings.sun_shadow_index];
    if (shadow_instance.domain != SHADOW_DOMAIN_DIRECTIONAL
        || shadow_instance.implementation_kind != SHADOW_IMPLEMENTATION_VIRTUAL) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const DirectionalVirtualShadowDebugResult debug =
        ComputeVirtualDirectionalShadowDebugResult(
            shadow_instance.payload_index, world_pos, normal_ws, light_dir_ws);
    if (!debug.active) {
        return float3(1.0f, 0.0f, 1.0f);
    }
    if (!debug.mapped) {
        return float3(0.0f, 0.35f, 1.0f);
    }

    const float depth_value =
        show_receiver_depth ? debug.receiver_depth : debug.stored_depth;
    const float phase = frac(depth_value * 2048.0f);
    const float3 palette =
        0.5f + 0.5f * cos(6.2831853f * (phase + float3(0.0f, 0.33f, 0.67f)));
    return palette;
}

static inline float3 GetDirectionalVirtualClipDebugColor(uint clip_index)
{
    switch (clip_index & 7u) {
        case 0u: return float3(0.72f, 0.72f, 0.72f);
        case 1u: return float3(1.0f, 0.55f, 0.15f);
        case 2u: return float3(0.62f, 0.32f, 0.92f);
        case 3u: return float3(0.85f, 0.55f, 0.75f);
        case 4u: return float3(0.55f, 0.85f, 0.82f);
        case 5u: return float3(0.78f, 0.62f, 0.35f);
        case 6u: return float3(0.95f, 0.75f, 0.85f);
        default: return float3(0.82f, 0.72f, 0.52f);
    }
}

static inline float3 ComputeVirtualDirectionalShadowRequestedClipDebugColor(
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.sun_shadow_index == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    StructuredBuffer<ShadowInstanceMetadata> shadow_instances =
        ResourceDescriptorHeap[shadow_bindings.shadow_instance_metadata_slot];
    uint instance_count = 0u;
    uint instance_stride = 0u;
    shadow_instances.GetDimensions(instance_count, instance_stride);
    if (shadow_bindings.sun_shadow_index >= instance_count) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const ShadowInstanceMetadata shadow_instance =
        shadow_instances[shadow_bindings.sun_shadow_index];
    if (shadow_instance.domain != SHADOW_DOMAIN_DIRECTIONAL
        || shadow_instance.implementation_kind != SHADOW_IMPLEMENTATION_VIRTUAL) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const DirectionalVirtualShadowDebugResult debug =
        ComputeVirtualDirectionalShadowDebugResult(
            shadow_instance.payload_index, world_pos, normal_ws, light_dir_ws);
    if (!debug.active) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    return GetDirectionalVirtualClipDebugColor(debug.raw_requested_clip_index);
}

static inline float3 ComputeVirtualDirectionalShadowResolvedClipDebugColor(
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.sun_shadow_index == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    StructuredBuffer<ShadowInstanceMetadata> shadow_instances =
        ResourceDescriptorHeap[shadow_bindings.shadow_instance_metadata_slot];
    uint instance_count = 0u;
    uint instance_stride = 0u;
    shadow_instances.GetDimensions(instance_count, instance_stride);
    if (shadow_bindings.sun_shadow_index >= instance_count) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const ShadowInstanceMetadata shadow_instance =
        shadow_instances[shadow_bindings.sun_shadow_index];
    if (shadow_instance.domain != SHADOW_DOMAIN_DIRECTIONAL
        || shadow_instance.implementation_kind != SHADOW_IMPLEMENTATION_VIRTUAL) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const DirectionalVirtualShadowDebugResult debug =
        ComputeVirtualDirectionalShadowDebugResult(
            shadow_instance.payload_index, world_pos, normal_ws, light_dir_ws);
    if (!debug.active) {
        return float3(1.0f, 0.0f, 1.0f);
    }
    if (!debug.mapped) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    return GetDirectionalVirtualClipDebugColor(debug.resolved_clip_index);
}

static inline float3 ComputeVirtualDirectionalShadowClipDeltaDebugColor(
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.sun_shadow_index == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    StructuredBuffer<ShadowInstanceMetadata> shadow_instances =
        ResourceDescriptorHeap[shadow_bindings.shadow_instance_metadata_slot];
    uint instance_count = 0u;
    uint instance_stride = 0u;
    shadow_instances.GetDimensions(instance_count, instance_stride);
    if (shadow_bindings.sun_shadow_index >= instance_count) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const ShadowInstanceMetadata shadow_instance =
        shadow_instances[shadow_bindings.sun_shadow_index];
    if (shadow_instance.domain != SHADOW_DOMAIN_DIRECTIONAL
        || shadow_instance.implementation_kind != SHADOW_IMPLEMENTATION_VIRTUAL) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const DirectionalVirtualShadowDebugResult debug =
        ComputeVirtualDirectionalShadowDebugResult(
            shadow_instance.payload_index, world_pos, normal_ws, light_dir_ws);
    if (!debug.active) {
        return float3(1.0f, 0.0f, 1.0f);
    }
    if (!debug.mapped) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const uint clip_delta = debug.resolved_clip_index >= debug.requested_clip_index
        ? debug.resolved_clip_index - debug.requested_clip_index
        : 0u;
    switch (min(clip_delta, 3u)) {
        case 0u:
            return float3(0.0f, 1.0f, 0.0f);
        case 1u:
            return float3(1.0f, 1.0f, 0.0f);
        case 2u:
            return float3(1.0f, 0.55f, 0.0f);
        default:
            return float3(1.0f, 0.0f, 0.0f);
    }
}

static inline float3 ComputeVirtualDirectionalShadowDepthDeltaDebugColor(
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.sun_shadow_index == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    StructuredBuffer<ShadowInstanceMetadata> shadow_instances =
        ResourceDescriptorHeap[shadow_bindings.shadow_instance_metadata_slot];
    uint instance_count = 0u;
    uint instance_stride = 0u;
    shadow_instances.GetDimensions(instance_count, instance_stride);
    if (shadow_bindings.sun_shadow_index >= instance_count) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const ShadowInstanceMetadata shadow_instance =
        shadow_instances[shadow_bindings.sun_shadow_index];
    if (shadow_instance.domain != SHADOW_DOMAIN_DIRECTIONAL
        || shadow_instance.implementation_kind != SHADOW_IMPLEMENTATION_VIRTUAL) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const DirectionalVirtualShadowDebugResult debug =
        ComputeVirtualDirectionalShadowDebugResult(
            shadow_instance.payload_index, world_pos, normal_ws, light_dir_ws);
    if (!debug.active) {
        return float3(1.0f, 0.0f, 1.0f);
    }
    if (!debug.mapped) {
        return float3(1.0f, 0.0f, 1.0f);
    }

    const float depth_delta_scale = saturate(abs(debug.depth_delta) * 2048.0f);
    const float3 agreement_color = float3(0.0f, 1.0f, 0.0f);
    const float3 disagreement_color = debug.depth_delta >= 0.0f
        ? float3(1.0f, 0.0f, 0.0f)
        : float3(0.0f, 0.35f, 1.0f);
    return lerp(agreement_color, disagreement_color, depth_delta_scale);
}

static inline float3 ComputeVirtualDirectionalShadowResolveDebugColor(
    float3 world_pos,
    float3 normal_ws,
    float3 light_dir_ws)
{
    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.sun_shadow_index == K_INVALID_BINDLESS_INDEX
        || shadow_bindings.shadow_instance_metadata_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.shadow_instance_metadata_slot)) {
        return float3(1.0f, 0.0f, 0.0f);
    }

    StructuredBuffer<ShadowInstanceMetadata> shadow_instances =
        ResourceDescriptorHeap[shadow_bindings.shadow_instance_metadata_slot];
    uint instance_count = 0u;
    uint instance_stride = 0u;
    shadow_instances.GetDimensions(instance_count, instance_stride);
    if (shadow_bindings.sun_shadow_index >= instance_count) {
        return float3(1.0f, 0.0f, 0.0f);
    }

    const ShadowInstanceMetadata shadow_instance =
        shadow_instances[shadow_bindings.sun_shadow_index];
    if (shadow_instance.domain != SHADOW_DOMAIN_DIRECTIONAL
        || shadow_instance.implementation_kind != SHADOW_IMPLEMENTATION_VIRTUAL) {
        return float3(1.0f, 0.0f, 0.0f);
    }

    const DirectionalVirtualShadowDebugResult debug =
        ComputeVirtualDirectionalShadowDebugResult(
            shadow_instance.payload_index,
            world_pos,
            normal_ws,
            light_dir_ws);
    if (!debug.active) {
        return float3(1.0f, 0.0f, 0.0f);
    }
    if (!debug.mapped) {
        if ((!debug.requested_page_valid || !debug.requested_page_requested_this_frame)
            && debug.unbiased_page_valid
            && debug.unbiased_page_requested_this_frame) {
            if (debug.resolved_clip_index != debug.requested_clip_index
                || !debug.biased_page_projects_in_clip) {
                return float3(0.0f, 0.0f, 1.0f);
            }
            return float3(1.0f, 1.0f, 1.0f);
        }
        if (!debug.requested_page_valid || !debug.requested_page_requested_this_frame) {
            return float3(1.0f, 0.0f, 1.0f);
        }
        if (!debug.requested_page_has_any_lod) {
            return float3(0.0f, 1.0f, 1.0f);
        }
        if (!debug.requested_page_has_current_lod) {
            return float3(1.0f, 0.5f, 0.0f);
        }
        return float3(1.0f, 0.0f, 0.0f);
    }
    if (debug.fallback_used) {
        return float3(1.0f, 1.0f, 0.0f);
    }
    return float3(0.0f, 1.0f, 0.0f);
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

    if (shadow_instance.domain == SHADOW_DOMAIN_DIRECTIONAL
        && shadow_instance.implementation_kind == SHADOW_IMPLEMENTATION_VIRTUAL) {
        return ComputeVirtualDirectionalShadowVisibility(
            shadow_instance.payload_index, world_pos, normal_ws, light_dir_ws);
    }

    return 1.0;
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_SHADOWHELPERS_HLSLI

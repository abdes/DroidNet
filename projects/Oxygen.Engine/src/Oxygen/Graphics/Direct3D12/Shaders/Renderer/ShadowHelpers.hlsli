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

static inline uint SelectDirectionalVirtualFilterRadiusTexels(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index);

static inline ShadowFrameBindings LoadResolvedShadowFrameBindings()
{
    const ViewFrameBindings view_bindings =
        LoadViewFrameBindings(bindless_view_frame_bindings_slot);
    return LoadShadowFrameBindings(view_bindings.shadow_frame_slot);
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

static inline float3 ComputeReceiverPlaneDepthBias(
    float2 uv,
    float receiver_depth)
{
    const float3 dcoord_dx = ddx(float3(uv, receiver_depth));
    const float3 dcoord_dy = ddy(float3(uv, receiver_depth));
    const float determinant =
        dcoord_dx.x * dcoord_dy.y - dcoord_dx.y * dcoord_dy.x;
    if (abs(determinant) <= 1.0e-6) {
        return 0.0.xxx;
    }

    float3 receiver_plane_depth_bias = 0.0.xxx;
    receiver_plane_depth_bias.x =
        dcoord_dy.y * dcoord_dx.z - dcoord_dx.y * dcoord_dy.z;
    receiver_plane_depth_bias.y =
        dcoord_dx.x * dcoord_dy.z - dcoord_dy.x * dcoord_dx.z;
    receiver_plane_depth_bias.xy /= determinant;
    return receiver_plane_depth_bias;
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

struct DirectionalVirtualClipRelativeTransform
{
    bool valid;
    float2 page_coord_scale;
    float2 page_coord_bias;
    float depth_scale;
    float depth_bias;
    float lod_scale;
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

static inline float2 TransformDirectionalRequestedPageCoordToResolvedClip(
    float2 requested_page_coord,
    DirectionalVirtualClipRelativeTransform transform)
{
    return requested_page_coord * transform.page_coord_scale
        + transform.page_coord_bias;
}

static inline float RemapDirectionalRequestedDepthToResolvedClip(
    float requested_depth,
    DirectionalVirtualClipRelativeTransform transform)
{
    return requested_depth * transform.depth_scale + transform.depth_bias;
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
    return max(1.0, (float)entry.fallback_lod_offset + 1.0);
}

static inline float2 ResolveDirectionalVirtualAtlasUvFromResolvedPageCoord(
    DirectionalVirtualShadowMetadata metadata,
    uint tile_x,
    uint tile_y,
    float2 resolved_page_coord,
    float2 pool_size,
    uint sampling_filter_radius_texels)
{
    const float guard_texels =
        (float)ResolveDirectionalVirtualGuardTexels(
            metadata.page_size_texels,
            sampling_filter_radius_texels);
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
    float2 resolved_page_coord = 0.0.xx;
    if (resolved_clip_index >= metadata.clip_level_count
        || !ProjectDirectionalVirtualClip(
            metadata, resolved_clip_index, light_space_xy, resolved_page_coord)) {
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

    resolved_page_coord = TransformDirectionalRequestedPageCoordToResolvedClip(
        requested_page_coord, transform);
    if (resolved_page_coord.x < 0.0 || resolved_page_coord.y < 0.0
        || resolved_page_coord.x >= metadata.pages_per_axis
        || resolved_page_coord.y >= metadata.pages_per_axis) {
        return false;
    }

    lookup.page_x = min((uint)resolved_page_coord.x, metadata.pages_per_axis - 1u);
    lookup.page_y = min((uint)resolved_page_coord.y, metadata.pages_per_axis - 1u);
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
    uint sampling_filter_radius_texels,
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
        pool_size,
        sampling_filter_radius_texels);
    return true;
}

static inline bool ResolveDirectionalVirtualAtlasUv(
    DirectionalVirtualShadowMetadata metadata,
    StructuredBuffer<uint> page_table,
    uint clip_index,
    float2 light_space_xy,
    float2 pool_size,
    out float2 atlas_uv)
{
    DirectionalVirtualResolvedPageLookup lookup =
        MakeInvalidDirectionalVirtualResolvedPageLookup();
    if (!TryResolveDirectionalVirtualPageLookup(
            metadata, page_table, clip_index, light_space_xy, lookup)) {
        atlas_uv = 0.0.xx;
        return false;
    }

    const uint resolved_clip_index = lookup.resolved_clip_index;
    const uint default_filter_radius_texels =
        SelectDirectionalVirtualFilterRadiusTexels(metadata, resolved_clip_index);
    return ResolveDirectionalVirtualAtlasUv(
        metadata,
        page_table,
        clip_index,
        light_space_xy,
        pool_size,
        default_filter_radius_texels,
        atlas_uv);
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
        pool_size,
        SelectDirectionalVirtualFilterRadiusTexels(metadata, clip_index));
}

static inline float ComputeDirectionalVirtualOptimalSlopeBias(
    DirectionalVirtualShadowMetadata metadata,
    DirectionalVirtualResolvedPageLookup lookup,
    float2 atlas_uv,
    float2 pool_size,
    uint sampling_filter_radius_texels,
    float3 receiver_plane_depth_bias)
{
    if (!lookup.valid || pool_size.x <= 0.0 || pool_size.y <= 0.0) {
        return 0.0;
    }

    const float guard_texels =
        (float)ResolveDirectionalVirtualGuardTexels(
            metadata.page_size_texels,
            sampling_filter_radius_texels);
    const float interior_min =
        guard_texels / max((float)metadata.page_size_texels, 1.0);
    const float interior_span = max(1.0 - 2.0 * interior_min, 1.0e-4);
    const float2 page_extent_uv =
        float2((float)metadata.page_size_texels / pool_size.x,
               (float)metadata.page_size_texels / pool_size.y);
    const float2 atlas_page_origin_uv =
        float2((float)(lookup.entry.tile_x * metadata.page_size_texels) / pool_size.x,
               (float)(lookup.entry.tile_y * metadata.page_size_texels) / pool_size.y);
    const float2 local_atlas_uv =
        saturate((atlas_uv - atlas_page_origin_uv) / max(page_extent_uv, 1.0e-6.xx));
    const float2 page_local_uv =
        saturate((local_atlas_uv - interior_min.xx) / interior_span.xx);
    const float2 texel_address_float =
        page_local_uv * max((float)metadata.page_size_texels, 1.0);
    const float2 texel_center =
        floor(texel_address_float) + 0.5.xx;
    const float2 texel_center_offset_uv =
        ((texel_center - texel_address_float) / max((float)metadata.page_size_texels, 1.0))
        * page_extent_uv;
    return ResolveDirectionalVirtualFallbackSlopeBiasScale(lookup.entry)
        * 2.0 * max(0.0, dot(receiver_plane_depth_bias.xy, texel_center_offset_uv));
}

static inline bool TryResolveDirectionalVirtualFilterSampleAtlasUv(
    DirectionalVirtualShadowMetadata metadata,
    StructuredBuffer<uint> page_table,
    DirectionalVirtualResolvedPageLookup center_lookup,
    float2 sample_light_space_xy,
    float2 pool_size,
    uint sampling_filter_radius_texels,
    out float2 sample_atlas_uv)
{
    sample_atlas_uv = 0.0.xx;
    if (!center_lookup.valid || center_lookup.requested_clip_index >= metadata.clip_level_count) {
        return false;
    }

    const DirectionalVirtualClipMetadata requested_clip =
        metadata.clip_metadata[center_lookup.requested_clip_index];
    const float requested_page_world =
        max(requested_clip.origin_page_scale.z, 1.0e-4);
    const float2 sample_requested_page_coord =
        (sample_light_space_xy - requested_clip.origin_page_scale.xy) / requested_page_world;
    if (sample_requested_page_coord.x >= 0.0
        && sample_requested_page_coord.y >= 0.0
        && sample_requested_page_coord.x < metadata.pages_per_axis
        && sample_requested_page_coord.y < metadata.pages_per_axis) {
        const DirectionalVirtualClipRelativeTransform transform =
            BuildDirectionalVirtualClipRelativeTransform(
                metadata,
                center_lookup.requested_clip_index,
                center_lookup.resolved_clip_index);
        if (transform.valid) {
            const float2 sample_resolved_page_coord =
                TransformDirectionalRequestedPageCoordToResolvedClip(
                    sample_requested_page_coord,
                    transform);
            if (sample_resolved_page_coord.x >= 0.0
                && sample_resolved_page_coord.y >= 0.0
                && sample_resolved_page_coord.x < metadata.pages_per_axis
                && sample_resolved_page_coord.y < metadata.pages_per_axis
                && (uint)sample_resolved_page_coord.x == center_lookup.page_x
                && (uint)sample_resolved_page_coord.y == center_lookup.page_y) {
                sample_atlas_uv = ResolveDirectionalVirtualAtlasUvFromResolvedPageCoord(
                    metadata,
                    center_lookup.entry.tile_x,
                    center_lookup.entry.tile_y,
                    sample_resolved_page_coord,
                    pool_size,
                    sampling_filter_radius_texels);
                return true;
            }
        }
    }

    return ResolveDirectionalVirtualAtlasUv(
        metadata,
        page_table,
        center_lookup.requested_clip_index,
        sample_light_space_xy,
        pool_size,
        sampling_filter_radius_texels,
        sample_atlas_uv);
}

static inline float SampleVirtualShadowComparisonTent3x3PageAware(
    DirectionalVirtualShadowMetadata metadata,
    StructuredBuffer<uint> page_table,
    Texture2D<float> shadow_texture,
    DirectionalVirtualResolvedPageLookup center_lookup,
    float2 center_atlas_uv,
    float2 receiver_light_space_xy,
    float logical_texel_world,
    uint sampling_filter_radius_texels,
    float receiver_depth,
    float3 receiver_plane_depth_bias)
{
    uint width = 0u;
    uint height = 0u;
    shadow_texture.GetDimensions(width, height);
    if (width == 0u || height == 0u) {
        return 1.0;
    }

    SamplerComparisonState shadow_sampler =
        SamplerDescriptorHeap[kShadowComparisonSamplerIndex];
    static const float kKernelWeights[3] = { 1.0, 2.0, 1.0 };

    float visibility = 0.0;
    float total_weight = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float weight = kKernelWeights[x + 1] * kKernelWeights[y + 1];
            const float2 sample_light_space_xy =
                receiver_light_space_xy
                + float2((float)x, (float)(-y)) * logical_texel_world;
            float2 sample_atlas_uv = center_atlas_uv;
            if (!TryResolveDirectionalVirtualFilterSampleAtlasUv(
                    metadata,
                    page_table,
                    center_lookup,
                    sample_light_space_xy,
                    float2(width, height),
                    sampling_filter_radius_texels,
                    sample_atlas_uv)) {
                sample_atlas_uv = center_atlas_uv;
            }
            const float adjusted_receiver_depth =
                receiver_depth + dot(sample_atlas_uv - center_atlas_uv, receiver_plane_depth_bias.xy);
            visibility += weight * shadow_texture.SampleCmpLevelZero(
                shadow_sampler, sample_atlas_uv, adjusted_receiver_depth);
            total_weight += weight;
        }
    }

    return visibility / max(total_weight, 1.0);
}

static inline float SampleVirtualShadowComparisonTent5x5PageAware(
    DirectionalVirtualShadowMetadata metadata,
    StructuredBuffer<uint> page_table,
    Texture2D<float> shadow_texture,
    DirectionalVirtualResolvedPageLookup center_lookup,
    float2 center_atlas_uv,
    float2 receiver_light_space_xy,
    float logical_texel_world,
    uint sampling_filter_radius_texels,
    float receiver_depth,
    float3 receiver_plane_depth_bias)
{
    uint width = 0u;
    uint height = 0u;
    shadow_texture.GetDimensions(width, height);
    if (width == 0u || height == 0u) {
        return 1.0;
    }

    SamplerComparisonState shadow_sampler =
        SamplerDescriptorHeap[kShadowComparisonSamplerIndex];
    static const float kKernelWeights[5] = { 1.0, 4.0, 6.0, 4.0, 1.0 };

    float visibility = 0.0;
    float total_weight = 0.0;
    [unroll]
    for (int y = -2; y <= 2; ++y) {
        [unroll]
        for (int x = -2; x <= 2; ++x) {
            const float weight = kKernelWeights[x + 2] * kKernelWeights[y + 2];
            const float2 sample_light_space_xy =
                receiver_light_space_xy
                + float2((float)x, (float)(-y)) * logical_texel_world;
            float2 sample_atlas_uv = center_atlas_uv;
            if (!TryResolveDirectionalVirtualFilterSampleAtlasUv(
                    metadata,
                    page_table,
                    center_lookup,
                    sample_light_space_xy,
                    float2(width, height),
                    sampling_filter_radius_texels,
                    sample_atlas_uv)) {
                sample_atlas_uv = center_atlas_uv;
            }
            const float adjusted_receiver_depth =
                receiver_depth + dot(sample_atlas_uv - center_atlas_uv, receiver_plane_depth_bias.xy);
            visibility += weight * shadow_texture.SampleCmpLevelZero(
                shadow_sampler, sample_atlas_uv, adjusted_receiver_depth);
            total_weight += weight;
        }
    }

    return visibility / max(total_weight, 1.0);
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

static inline uint SelectDirectionalVirtualFilterRadiusTexels(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index);

static inline float ComputeDirectionalVirtualLogicalTexelWorld(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    uint filter_radius_texels);

static inline float ComputeDirectionalVirtualFootprintBlendToFinerClip(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    float desired_world_footprint);

static inline bool SelectDirectionalVirtualClipForFootprint(
    DirectionalVirtualShadowMetadata metadata,
    float2 light_space_xy,
    float desired_world_footprint,
    out uint clip_index,
    out float2 page_coord)
{
    clip_index = 0u;
    page_coord = 0.0.xx;

    bool found_containing = false;
    uint finest_containing_clip = 0u;
    float2 finest_containing_page_coord = 0.0.xx;
    uint selected_clip = 0u;
    float2 selected_page_coord = 0.0.xx;

    [unroll]
    for (uint i = 0u; i < OXYGEN_MAX_VIRTUAL_DIRECTIONAL_CLIP_LEVELS; ++i) {
        if (i >= metadata.clip_level_count) {
            break;
        }

        float2 local_page_coord = 0.0.xx;
        if (!ProjectDirectionalVirtualClip(metadata, i, light_space_xy, local_page_coord)) {
            continue;
        }

        if (!found_containing) {
            found_containing = true;
            finest_containing_clip = i;
            finest_containing_page_coord = local_page_coord;
            selected_clip = i;
            selected_page_coord = local_page_coord;
        }

        const uint filter_radius_texels =
            SelectDirectionalVirtualFilterRadiusTexels(metadata, i);
        const float logical_texel_world =
            ComputeDirectionalVirtualLogicalTexelWorld(
                metadata, i, filter_radius_texels);
        if (logical_texel_world <= desired_world_footprint) {
            selected_clip = i;
            selected_page_coord = local_page_coord;
        } else {
            break;
        }
    }

    if (!found_containing) {
        return false;
    }

    clip_index = selected_clip;
    page_coord = selected_page_coord;
    return true;
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

static inline float ComputeDirectionalVirtualClipBlend(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    float2 page_coord)
{
    if (clip_index + 1u >= metadata.clip_level_count) {
        return 0.0;
    }

    const float pages_per_axis = max((float)metadata.pages_per_axis, 1.0);
    const float2 local = clamp(page_coord, 0.0.xx, pages_per_axis.xx);
    const float edge_distance = min(
        min(local.x, local.y),
        min(pages_per_axis - local.x, pages_per_axis - local.y));
    const float transition_width_pages = max(1.0, pages_per_axis * 0.125);
    return saturate(
        (transition_width_pages - edge_distance)
        / max(transition_width_pages, 1.0e-4));
}

static inline uint SelectDirectionalVirtualFilterRadiusTexels(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index)
{
    const float base_page_world =
        max(metadata.clip_metadata[0].origin_page_scale.z, 1.0e-4);
    const float clip_page_world =
        max(metadata.clip_metadata[clip_index].origin_page_scale.z, base_page_world);
    const float texel_ratio = clip_page_world / base_page_world;
    return texel_ratio > 2.5 ? 2u : 1u;
}

static inline float ComputeDirectionalVirtualLogicalTexelWorld(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    uint filter_radius_texels)
{
    if (clip_index >= metadata.clip_level_count) {
        return 1.0e-4;
    }

    const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[clip_index];
    const float page_world_size = max(clip.origin_page_scale.z, 1.0e-4);
    const float guard_texels =
        (float)ResolveDirectionalVirtualGuardTexels(
            metadata.page_size_texels, filter_radius_texels);
    const float logical_texel_count =
        max((float)metadata.page_size_texels - 2.0 * guard_texels, 1.0);
    return page_world_size / logical_texel_count;
}

static inline float ComputeDirectionalVirtualFootprintBlendToFinerClip(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index,
    float desired_world_footprint)
{
    if (clip_index == 0u || clip_index >= metadata.clip_level_count) {
        return 0.0;
    }

    const uint current_filter_radius_texels =
        SelectDirectionalVirtualFilterRadiusTexels(metadata, clip_index);
    const uint finer_filter_radius_texels =
        SelectDirectionalVirtualFilterRadiusTexels(metadata, clip_index - 1u);
    const float current_texel_world =
        ComputeDirectionalVirtualLogicalTexelWorld(
            metadata, clip_index, current_filter_radius_texels);
    const float finer_texel_world =
        ComputeDirectionalVirtualLogicalTexelWorld(
            metadata, clip_index - 1u, finer_filter_radius_texels);
    const float enter_finer_footprint = current_texel_world * 1.25;
    const float exit_finer_footprint =
        max(current_texel_world, finer_texel_world + 1.0e-5);
    if (enter_finer_footprint <= exit_finer_footprint) {
        return 0.0;
    }

    return saturate(
        (enter_finer_footprint - desired_world_footprint)
        / max(enter_finer_footprint - exit_finer_footprint, 1.0e-4));
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

    const uint requested_filter_radius_texels =
        SelectDirectionalVirtualFilterRadiusTexels(metadata, clip_index);
    const float3 light_view_pos =
        mul(metadata.light_view, float4(world_pos, 1.0)).xyz;
    DirectionalVirtualResolvedPageLookup lookup =
        MakeInvalidDirectionalVirtualResolvedPageLookup();
    if (!TryResolveDirectionalVirtualPageLookup(
            metadata, page_table, clip_index, light_view_pos.xy, lookup)) {
        return 1.0;
    }

    const DirectionalVirtualClipRelativeTransform clip_relative_transform =
        BuildDirectionalVirtualClipRelativeTransform(
            metadata, clip_index, lookup.resolved_clip_index);
    const bool using_coarse_sampling =
        !lookup.entry.current_lod_valid
        || lookup.resolved_clip_index != clip_index
        || clip_index + 3u >= metadata.clip_level_count;
    const uint effective_filter_radius_texels =
        using_coarse_sampling ? 1u : requested_filter_radius_texels;
    const float logical_texel_world = max(
        ComputeDirectionalVirtualLogicalTexelWorld(
            metadata, clip_index, effective_filter_radius_texels),
        1.0e-4);
    const float ndotl = saturate(dot(normal_ws, light_dir_ws));
    const float slope_factor = 1.0 - ndotl;
    // Page ownership and the full in-page filter footprint stay anchored to
    // the unbiased receiver XY. Bias only perturbs the depth comparison, so
    // it cannot translate the sampled shadow laterally off the caster base.
    // Guardrail: keep that grounded contact by doing the fine-page offset here
    // instead of reintroducing a large hardware raster depth bias upstream.
    resolved_clip_index = lookup.resolved_clip_index;
    resolved_page_coord = lookup.page_coord;
    const DirectionalVirtualClipMetadata requested_clip =
        metadata.clip_metadata[clip_index];
    const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[resolved_clip_index];

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
            effective_filter_radius_texels,
            atlas_uv)) {
        return 1.0;
    }
    const float requested_receiver_depth =
        light_view_pos.z * requested_clip.origin_page_scale.w
        + requested_clip.bias_reserved.x;
    float receiver_depth =
        RemapDirectionalRequestedDepthToResolvedClip(
            requested_receiver_depth, clip_relative_transform);
    if (using_coarse_sampling) {
        const float3 fallback_receiver_plane_depth_bias =
            ComputeReceiverPlaneDepthBias(atlas_uv, receiver_depth);
        const float fallback_slope_bias =
            ComputeDirectionalVirtualOptimalSlopeBias(
                metadata,
                lookup,
                atlas_uv,
                float2(pool_width, pool_height),
                effective_filter_radius_texels,
                fallback_receiver_plane_depth_bias);
        receiver_depth += fallback_slope_bias;
    } else {
        const float filter_bias_scale =
            effective_filter_radius_texels <= 1u ? 0.85 : 1.0;
        const float renderer_normal_bias = logical_texel_world
            * lerp(0.55, 1.5, slope_factor) * filter_bias_scale;
        const float renderer_constant_bias = logical_texel_world
            * lerp(0.03, 0.18, slope_factor) * filter_bias_scale;
        const float3 biased_world_pos =
            world_pos + normal_ws * (metadata.normal_bias + renderer_normal_bias)
            + light_dir_ws * (metadata.constant_bias + renderer_constant_bias);
        const float biased_light_view_depth =
            mul(metadata.light_view, float4(biased_world_pos, 1.0)).z;
        const float requested_biased_receiver_depth =
            biased_light_view_depth * requested_clip.origin_page_scale.w
            + requested_clip.bias_reserved.x;
        receiver_depth = RemapDirectionalRequestedDepthToResolvedClip(
            requested_biased_receiver_depth, clip_relative_transform);
    }
    const float3 receiver_plane_depth_bias =
        ComputeReceiverPlaneDepthBias(atlas_uv, receiver_depth);

    valid = true;
#if OXYGEN_SHADOW_USE_MANUAL_COMPARE_FALLBACK
    return 1.0;
#else
    if (using_coarse_sampling) {
        return SampleVirtualShadowComparisonPoint(
            physical_pool, atlas_uv, receiver_depth);
    }
    if (effective_filter_radius_texels <= 1u) {
        return SampleVirtualShadowComparisonTent3x3PageAware(
            metadata,
            page_table,
            physical_pool,
            lookup,
            atlas_uv,
            light_view_pos.xy,
            logical_texel_world,
            effective_filter_radius_texels,
            receiver_depth,
            receiver_plane_depth_bias);
    }
        return SampleVirtualShadowComparisonTent5x5PageAware(
            metadata,
            page_table,
            physical_pool,
            lookup,
            atlas_uv,
            light_view_pos.xy,
            logical_texel_world,
            effective_filter_radius_texels,
            receiver_depth,
            receiver_plane_depth_bias);
#endif
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

    const float3 unbiassed_light_view_pos =
        mul(metadata.light_view, float4(world_pos, 1.0)).xyz;
    const float2 light_view_dx = ddx(unbiassed_light_view_pos.xy);
    const float2 light_view_dy = ddy(unbiassed_light_view_pos.xy);
    const float desired_world_footprint = max(
        max(length(light_view_dx), length(light_view_dy)),
        1.0e-4);

    uint clip_index = 0u;
    float2 clip_page_coord = 0.0.xx;
    // Runtime request generation is footprint-driven, so directional shading
    // must start from the same clip family. Otherwise the shader chases finer
    // pages than the cache intentionally requested and quality collapses as the
    // view basis rotates.
    if (!SelectDirectionalVirtualClipForFootprint(
            metadata,
            unbiassed_light_view_pos.xy,
            desired_world_footprint,
            clip_index,
            clip_page_coord)) {
        return 1.0;
    }
    const float blend_to_finer = ComputeDirectionalVirtualFootprintBlendToFinerClip(
        metadata, clip_index, desired_world_footprint);

    StructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[shadow_bindings.virtual_shadow_page_table_slot];
    Texture2D<float> physical_pool =
        ResourceDescriptorHeap[shadow_bindings.virtual_shadow_physical_pool_slot];

    bool selected_valid = false;
    uint selected_sampled_clip = clip_index;
    float2 selected_sampled_page_coord = clip_page_coord;
    const float selected_visibility = SampleDirectionalVirtualShadowClipVisibility(
        metadata,
        page_table,
        physical_pool,
        clip_index,
        world_pos,
        normal_ws,
        light_dir_ws,
        selected_valid,
        selected_sampled_clip,
        selected_sampled_page_coord);

    bool active_valid = selected_valid;
    uint active_clip_index = selected_sampled_clip;
    float2 active_page_coord = selected_sampled_page_coord;
    float active_visibility = selected_visibility;

    bool finer_valid = false;
    uint finer_sampled_clip = clip_index;
    float2 finer_sampled_page_coord = 0.0.xx;
    float finer_visibility = 1.0;
    if (blend_to_finer > 0.0 && clip_index > 0u) {
        finer_visibility = SampleDirectionalVirtualShadowClipVisibility(
            metadata,
            page_table,
            physical_pool,
            clip_index - 1u,
            world_pos,
            normal_ws,
            light_dir_ws,
            finer_valid,
            finer_sampled_clip,
            finer_sampled_page_coord);
        if (!selected_valid && finer_valid) {
            active_valid = true;
            active_clip_index = finer_sampled_clip;
            active_page_coord = finer_sampled_page_coord;
            active_visibility = finer_visibility;
        }
    }

    bool used_coarser_fallback = false;
    if (!active_valid) {
        [loop]
        for (uint candidate_clip = clip_index + 1u;
             candidate_clip < metadata.clip_level_count;
             ++candidate_clip) {
            bool candidate_valid = false;
            uint candidate_sampled_clip = candidate_clip;
            float2 candidate_sampled_page_coord = 0.0.xx;
            const float candidate_visibility = SampleDirectionalVirtualShadowClipVisibility(
                metadata,
                page_table,
                physical_pool,
                candidate_clip,
                world_pos,
                normal_ws,
                light_dir_ws,
                candidate_valid,
                candidate_sampled_clip,
                candidate_sampled_page_coord);
            if (candidate_valid) {
                active_valid = true;
                active_clip_index = candidate_sampled_clip;
                active_page_coord = candidate_sampled_page_coord;
                active_visibility = candidate_visibility;
                used_coarser_fallback = true;
                break;
            }
        }
    }

    bool coarse_valid = false;
    float coarse_visibility = 1.0;
    const float blend_to_coarser = ComputeDirectionalVirtualClipBlend(
        metadata, active_clip_index, active_page_coord);
    if (active_valid
        && blend_to_coarser > 0.0
        && active_clip_index + 1u < metadata.clip_level_count) {
        uint coarse_sampled_clip = active_clip_index + 1u;
        float2 coarse_sampled_page_coord = 0.0.xx;
        coarse_visibility = SampleDirectionalVirtualShadowClipVisibility(
            metadata,
            page_table,
            physical_pool,
            active_clip_index + 1u,
            world_pos,
            normal_ws,
            light_dir_ws,
            coarse_valid,
            coarse_sampled_clip,
            coarse_sampled_page_coord);
    }

    if (!active_valid) {
        return 1.0;
    }

    // Audit Stage 1 bridge note: this selected/finer/coarser shaping is still
    // a migration quality bridge. The final contract should lean on
    // page-table-driven fallback continuity with less cross-band blending once
    // the authoritative page-management pipeline is complete.
    if (selected_valid && finer_valid && blend_to_finer > 0.0) {
        active_visibility = lerp(
            selected_visibility,
            finer_visibility,
            blend_to_finer);
        if (finer_sampled_clip <= selected_sampled_clip) {
            active_clip_index = finer_sampled_clip;
            active_page_coord = finer_sampled_page_coord;
        }
    }

    if (!used_coarser_fallback && coarse_valid && blend_to_coarser > 0.0) {
        active_visibility = lerp(
            active_visibility,
            coarse_visibility,
            blend_to_coarser);
    }

    return active_visibility;
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

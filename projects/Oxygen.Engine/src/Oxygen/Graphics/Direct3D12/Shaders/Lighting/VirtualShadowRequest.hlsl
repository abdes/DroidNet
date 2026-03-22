//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/LightingHelpers.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/ShadowHelpers.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VirtualShadowRequestPassConstants
{
    uint depth_texture_index;
    uint virtual_directional_shadow_metadata_index;
    uint request_words_uav_index;
    uint page_mark_flags_uav_index;
    uint request_word_count;
    uint total_page_count;
    float border_dilation_page_fraction;
    uint _pad0;
    uint2 pixel_stride;
    uint _pad_stride0;
    uint _pad_stride1;
    uint2 screen_dimensions;
    uint _pad1;
    uint _pad2;
    float4x4 inv_view_projection_matrix;
};

static const uint kInvalidRequestedPageIndex = 0xFFFFFFFFu;
static const uint kMaxRequestsPerLane = 9u;

static void RequestDirectionalVirtualPageByIndex(
    RWStructuredBuffer<uint> request_words,
    RWStructuredBuffer<uint> page_mark_flags,
    uint page_index)
{
    const uint word_index = page_index / 32u;
    const uint bit_mask = 1u << (page_index % 32u);
    InterlockedOr(request_words[word_index], bit_mask);
    InterlockedOr(page_mark_flags[page_index],
        OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME
        | OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY);
}

static void RequestDirectionalVirtualCoarsePageByIndex(
    RWStructuredBuffer<uint> request_words,
    RWStructuredBuffer<uint> page_mark_flags,
    uint page_index)
{
    const uint word_index = page_index / 32u;
    const uint bit_mask = 1u << (page_index % 32u);
    InterlockedOr(request_words[word_index], bit_mask);
    InterlockedOr(page_mark_flags[page_index], OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME);
}

static uint EncodeDirectionalVirtualPageIndex(
    DirectionalVirtualShadowMetadata metadata,
    VirtualShadowRequestPassConstants pass_constants,
    uint clip_index,
    int2 page_coords)
{
    if (page_coords.x < 0 || page_coords.y < 0
        || page_coords.x >= (int)metadata.pages_per_axis
        || page_coords.y >= (int)metadata.pages_per_axis) {
        return kInvalidRequestedPageIndex;
    }

    const uint pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis;
    const uint page_index =
        clip_index * pages_per_level
        + (uint)page_coords.y * metadata.pages_per_axis
        + (uint)page_coords.x;
    if (page_index >= pass_constants.total_page_count) {
        return kInvalidRequestedPageIndex;
    }

    const uint word_index = page_index / 32u;
    return word_index < pass_constants.request_word_count ? page_index : kInvalidRequestedPageIndex;
}

static bool AppendDirectionalVirtualPageIndex(
    uint page_index,
    inout uint count,
    inout uint page_indices[9])
{
    if (page_index == kInvalidRequestedPageIndex) {
        return false;
    }

    [unroll]
    for (uint i = 0u; i < kMaxRequestsPerLane; ++i) {
        if (i >= count) {
            break;
        }
        if (page_indices[i] == page_index) {
            return false;
        }
    }

    if (count >= kMaxRequestsPerLane) {
        return false;
    }

    page_indices[count++] = page_index;
    return true;
}

static uint BuildDirectionalVirtualPageNeighborhood(
    DirectionalVirtualShadowMetadata metadata,
    VirtualShadowRequestPassConstants pass_constants,
    uint clip_index,
    float2 page_coord,
    float2 page_dilation_offset,
    out uint page_indices[9])
{
    [unroll]
    for (uint i = 0u; i < 9u; ++i) {
        page_indices[i] = kInvalidRequestedPageIndex;
    }

    const int2 base_page = int2(
        min((uint)page_coord.x, metadata.pages_per_axis - 1u),
        min((uint)page_coord.y, metadata.pages_per_axis - 1u));
    uint count = 0u;

    AppendDirectionalVirtualPageIndex(
        EncodeDirectionalVirtualPageIndex(
            metadata, pass_constants, clip_index, base_page),
        count,
        page_indices);

    if (all(page_dilation_offset == 0.0.xx)) {
        return count;
    }

    AppendDirectionalVirtualPageIndex(
        EncodeDirectionalVirtualPageIndex(
            metadata,
            pass_constants,
            clip_index,
            int2(clamp(
                page_coord + page_dilation_offset,
                0.0.xx,
                float2(metadata.pages_per_axis - 1u, metadata.pages_per_axis - 1u)))),
        count,
        page_indices);
    AppendDirectionalVirtualPageIndex(
        EncodeDirectionalVirtualPageIndex(
            metadata,
            pass_constants,
            clip_index,
            int2(clamp(
                page_coord - page_dilation_offset,
                0.0.xx,
                float2(metadata.pages_per_axis - 1u, metadata.pages_per_axis - 1u)))),
        count,
        page_indices);

    return count;
}

static void MarkDirectionalVirtualCoarsePages(
    DirectionalVirtualShadowMetadata metadata,
    VirtualShadowRequestPassConstants pass_constants,
    RWStructuredBuffer<uint> request_words,
    RWStructuredBuffer<uint> page_mark_flags)
{
    if (metadata.clip_level_count == 0u || metadata.pages_per_axis == 0u
        || metadata.coarse_clip_mask == 0u) {
        return;
    }

    [loop]
    for (uint clip_index = 0u; clip_index < metadata.clip_level_count; ++clip_index) {
        if (((metadata.coarse_clip_mask >> clip_index) & 1u) == 0u) {
            continue;
        }

        const float2 clipmap_origin_light_xy =
            mul(
                metadata.light_view,
                float4(metadata.clipmap_selection_world_origin_lod_bias.xyz, 1.0f)).xy;
        float2 center_page_float = 0.0.xx;
        if (!ProjectDirectionalVirtualClip(
                metadata,
                clip_index,
                clipmap_origin_light_xy,
                center_page_float)) {
            continue;
        }

        const int low_page_x = int(floor(center_page_float.x - 0.5f));
        const int low_page_y = int(floor(center_page_float.y - 0.5f));
        const int high_page_x = int(ceil(center_page_float.x - 0.5f));
        const int high_page_y = int(ceil(center_page_float.y - 0.5f));
        const uint coarse_pages[4] = {
            EncodeDirectionalVirtualPageIndex(
                metadata, pass_constants, clip_index, int2(low_page_x, low_page_y)),
            EncodeDirectionalVirtualPageIndex(
                metadata, pass_constants, clip_index, int2(low_page_x, high_page_y)),
            EncodeDirectionalVirtualPageIndex(
                metadata, pass_constants, clip_index, int2(high_page_x, low_page_y)),
            EncodeDirectionalVirtualPageIndex(
                metadata, pass_constants, clip_index, int2(high_page_x, high_page_y))
        };

        [unroll]
        for (uint i = 0u; i < 4u; ++i) {
            if (coarse_pages[i] != kInvalidRequestedPageIndex) {
                RequestDirectionalVirtualCoarsePageByIndex(
                    request_words, page_mark_flags, coarse_pages[i]);
            }
        }
    }
}

static float3 ReconstructWorldPosition(
    VirtualShadowRequestPassConstants pass_constants,
    uint2 pixel_xy,
    float depth)
{
    const float2 pixel_center =
        (float2(pixel_xy) + float2(0.5, 0.5))
        / max(float2(pass_constants.screen_dimensions), float2(1.0, 1.0));
    float2 ndc = pixel_center * 2.0 - 1.0;
    ndc.y = -ndc.y;

    const float4 world_pos_h = mul(
        pass_constants.inv_view_projection_matrix,
        float4(ndc, depth, 1.0));
    return world_pos_h.xyz / max(abs(world_pos_h.w), 1.0e-6);
}

static bool TryLoadNeighborWorldPosition(
    VirtualShadowRequestPassConstants pass_constants,
    Texture2D<float> depth_texture,
    int2 pixel_xy,
    out float3 world_pos)
{
    world_pos = 0.0.xxx;
    if (pixel_xy.x < 0 || pixel_xy.y < 0
        || pixel_xy.x >= int(pass_constants.screen_dimensions.x)
        || pixel_xy.y >= int(pass_constants.screen_dimensions.y)) {
        return false;
    }

    const float neighbor_depth = depth_texture.Load(int3(pixel_xy, 0));
    if (neighbor_depth >= 1.0f) {
        return false;
    }

    world_pos = ReconstructWorldPosition(
        pass_constants, uint2(pixel_xy), neighbor_depth);
    return true;
}

static bool TryComputeDirectionalRequestNormal(
    VirtualShadowRequestPassConstants pass_constants,
    Texture2D<float> depth_texture,
    uint2 pixel_xy,
    float3 world_pos,
    out float3 surface_normal_ws)
{
    surface_normal_ws = 0.0.xxx;

    float3 right_world_pos = 0.0.xxx;
    float3 left_world_pos = 0.0.xxx;
    float3 down_world_pos = 0.0.xxx;
    float3 up_world_pos = 0.0.xxx;
    const bool has_right = TryLoadNeighborWorldPosition(
        pass_constants, depth_texture, int2(pixel_xy) + int2(1, 0),
        right_world_pos);
    const bool has_left = TryLoadNeighborWorldPosition(
        pass_constants, depth_texture, int2(pixel_xy) + int2(-1, 0),
        left_world_pos);
    const bool has_down = TryLoadNeighborWorldPosition(
        pass_constants, depth_texture, int2(pixel_xy) + int2(0, 1),
        down_world_pos);
    const bool has_up = TryLoadNeighborWorldPosition(
        pass_constants, depth_texture, int2(pixel_xy) + int2(0, -1),
        up_world_pos);

    // Only trust centered neighborhoods. One-sided samples near silhouettes or
    // disocclusions are too view-dependent to drive virtual page residency.
    if (!has_right || !has_left || !has_down || !has_up) {
        return false;
    }

    const float3 offset_right = right_world_pos - world_pos;
    const float3 offset_left = world_pos - left_world_pos;
    const float3 offset_down = down_world_pos - world_pos;
    const float3 offset_up = world_pos - up_world_pos;

    const float offset_right_len_sq = dot(offset_right, offset_right);
    const float offset_left_len_sq = dot(offset_left, offset_left);
    const float offset_down_len_sq = dot(offset_down, offset_down);
    const float offset_up_len_sq = dot(offset_up, offset_up);
    const float min_offset_len_sq = min(min(offset_right_len_sq, offset_left_len_sq),
        min(offset_down_len_sq, offset_up_len_sq));
    const float max_offset_len_sq = max(max(offset_right_len_sq, offset_left_len_sq),
        max(offset_down_len_sq, offset_up_len_sq));
    if (min_offset_len_sq <= 1.0e-8f || max_offset_len_sq > min_offset_len_sq * 9.0f) {
        return false;
    }

    const float3 tangent_x = right_world_pos - left_world_pos;
    const float3 tangent_y = down_world_pos - up_world_pos;

    float3 geo_normal_ws = cross(tangent_x, tangent_y);
    const float geo_normal_len_sq = dot(geo_normal_ws, geo_normal_ws);
    if (geo_normal_len_sq <= 1.0e-8f) {
        return false;
    }

    geo_normal_ws *= rsqrt(geo_normal_len_sq);
    const float3 view_dir_ws = camera_position - world_pos;
    if (dot(geo_normal_ws, view_dir_ws) < 0.0f) {
        geo_normal_ws = -geo_normal_ws;
    }

    surface_normal_ws = geo_normal_ws;
    return true;
}

static bool ShouldSuppressDirectionalRequestDilation(float3 surface_normal_ws)
{
    const float3 sun_dir_ws = GetSunDirectionWS();
    const float sun_dir_len_sq = dot(sun_dir_ws, sun_dir_ws);
    if (sun_dir_len_sq <= 1.0e-8f) {
        return false;
    }

    const float3 safe_sun_dir_ws = sun_dir_ws * rsqrt(sun_dir_len_sq);
    // Keep the selected base page authoritative for correctness. Only suppress
    // optional local dilation on strongly backfacing, high-confidence samples.
    return dot(surface_normal_ws, safe_sun_dir_ws) <= -0.25f;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CS(
    uint3 dispatch_thread_id : SV_DispatchThreadID,
    uint group_index : SV_GroupIndex)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VirtualShadowRequestPassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IN_GLOBAL_SRV(pass_constants.depth_texture_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.virtual_directional_shadow_metadata_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.request_words_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.page_mark_flags_uav_index)) {
        return;
    }

    uint local_page_indices[9];
    [unroll]
    for (uint page_slot = 0u; page_slot < kMaxRequestsPerLane; ++page_slot) {
        local_page_indices[page_slot] = kInvalidRequestedPageIndex;
    }

    const uint2 pixel_xy =
        dispatch_thread_id.xy * max(pass_constants.pixel_stride, uint2(1u, 1u));
    const bool pixel_in_bounds =
        pixel_xy.x < pass_constants.screen_dimensions.x
        && pixel_xy.y < pass_constants.screen_dimensions.y;

    StructuredBuffer<DirectionalVirtualShadowMetadata> metadata_buffer =
        ResourceDescriptorHeap[pass_constants.virtual_directional_shadow_metadata_index];
    uint metadata_count = 0u;
    uint metadata_stride = 0u;
    metadata_buffer.GetDimensions(metadata_count, metadata_stride);

    Texture2D<float> depth_texture = ResourceDescriptorHeap[pass_constants.depth_texture_index];
    RWStructuredBuffer<uint> request_words =
        ResourceDescriptorHeap[pass_constants.request_words_uav_index];
    RWStructuredBuffer<uint> page_mark_flags =
        ResourceDescriptorHeap[pass_constants.page_mark_flags_uav_index];
    if (dispatch_thread_id.x == 0u && dispatch_thread_id.y == 0u && metadata_count > 0u) {
        MarkDirectionalVirtualCoarsePages(
            metadata_buffer[0], pass_constants, request_words, page_mark_flags);
    }

    if (pixel_in_bounds && metadata_count > 0u) {
        const DirectionalVirtualShadowMetadata metadata = metadata_buffer[0];
        if (metadata.clip_level_count == 0u || metadata.pages_per_axis == 0u) {
            return;
        }

        const float depth = depth_texture.Load(int3(pixel_xy, 0));
        if (depth < 1.0f) {
            const float3 world_pos =
                ReconstructWorldPosition(pass_constants, pixel_xy, depth);
            float request_dilation_fraction = pass_constants.border_dilation_page_fraction;
            float3 surface_normal_ws = 0.0.xxx;
            if (TryComputeDirectionalRequestNormal(
                    pass_constants,
                    depth_texture,
                    pixel_xy,
                    world_pos,
                    surface_normal_ws)
                && ShouldSuppressDirectionalRequestDilation(surface_normal_ws)) {
                request_dilation_fraction = 0.0f;
            }
            const float3 light_view_pos = mul(metadata.light_view, float4(world_pos, 1.0)).xyz;

            uint clip_index = 0u;
            float2 page_coord = 0.0.xx;
            if (SelectDirectionalVirtualRequestedClip(
                    metadata,
                    world_pos,
                    light_view_pos.xy,
                    clip_index,
                    page_coord)) {
                float2 request_page_coord = 0.0.xx;
                if (ProjectDirectionalVirtualClip(
                        metadata, clip_index, light_view_pos.xy, request_page_coord)) {
                    const float2 page_dilation_dither = float2(
                        (group_index & 1u) != 0u ? 1.0f : -1.0f,
                        (group_index & 2u) != 0u ? 1.0f : -1.0f);
                    const float2 page_dilation_offset =
                        request_dilation_fraction
                            * page_dilation_dither;
                    const uint page_count = BuildDirectionalVirtualPageNeighborhood(
                        metadata,
                        pass_constants,
                        clip_index,
                        request_page_coord,
                        page_dilation_offset,
                        local_page_indices);
                    [unroll]
                    for (uint page_slot = 0u; page_slot < kMaxRequestsPerLane; ++page_slot) {
                        if (page_slot >= page_count) {
                            break;
                        }
                        const uint page_index = local_page_indices[page_slot];
                        if (page_index != kInvalidRequestedPageIndex) {
                            RequestDirectionalVirtualPageByIndex(
                                request_words, page_mark_flags, page_index);
                        }
                    }
                }
            }
        }
    }
}

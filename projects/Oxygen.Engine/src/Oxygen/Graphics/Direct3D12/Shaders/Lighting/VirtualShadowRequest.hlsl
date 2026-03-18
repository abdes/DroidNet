//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
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
    uint border_dilation_texels;
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

static uint BuildDirectionalVirtualPageNeighborhood(
    DirectionalVirtualShadowMetadata metadata,
    VirtualShadowRequestPassConstants pass_constants,
    uint clip_index,
    float2 page_coord,
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
    page_indices[count++] =
        EncodeDirectionalVirtualPageIndex(metadata, pass_constants, clip_index, base_page);

    const float border_guard_pages = min(
        0.49,
        (float)pass_constants.border_dilation_texels
            / max((float)metadata.page_size_texels, 1.0));
    if (border_guard_pages <= 0.0) {
        return count;
    }

    const float2 page_frac = frac(page_coord);
    const bool near_left = page_frac.x <= border_guard_pages;
    const bool near_right = page_frac.x >= (1.0 - border_guard_pages);
    const bool near_top = page_frac.y <= border_guard_pages;
    const bool near_bottom = page_frac.y >= (1.0 - border_guard_pages);

    if (near_left) {
        page_indices[count++] = EncodeDirectionalVirtualPageIndex(
            metadata, pass_constants, clip_index, base_page + int2(-1, 0));
    }
    if (near_right) {
        page_indices[count++] = EncodeDirectionalVirtualPageIndex(
            metadata, pass_constants, clip_index, base_page + int2(1, 0));
    }
    if (near_top) {
        page_indices[count++] = EncodeDirectionalVirtualPageIndex(
            metadata, pass_constants, clip_index, base_page + int2(0, -1));
    }
    if (near_bottom) {
        page_indices[count++] = EncodeDirectionalVirtualPageIndex(
            metadata, pass_constants, clip_index, base_page + int2(0, 1));
    }
    if (near_left && near_top) {
        page_indices[count++] = EncodeDirectionalVirtualPageIndex(
            metadata, pass_constants, clip_index, base_page + int2(-1, -1));
    }
    if (near_left && near_bottom) {
        page_indices[count++] = EncodeDirectionalVirtualPageIndex(
            metadata, pass_constants, clip_index, base_page + int2(-1, 1));
    }
    if (near_right && near_top) {
        page_indices[count++] = EncodeDirectionalVirtualPageIndex(
            metadata, pass_constants, clip_index, base_page + int2(1, -1));
    }
    if (near_right && near_bottom) {
        page_indices[count++] = EncodeDirectionalVirtualPageIndex(
            metadata, pass_constants, clip_index, base_page + int2(1, 1));
    }

    return count;
}

static uint ResolveDirectionalCoarseClipCount(uint clip_level_count)
{
    if (clip_level_count == 0u) {
        return 0u;
    }

    return min(min(max((clip_level_count + 2u) / 3u, 2u), 4u), clip_level_count);
}

static void MarkDirectionalVirtualCoarsePages(
    DirectionalVirtualShadowMetadata metadata,
    VirtualShadowRequestPassConstants pass_constants,
    RWStructuredBuffer<uint> request_words,
    RWStructuredBuffer<uint> page_mark_flags)
{
    if (metadata.clip_level_count == 0u || metadata.pages_per_axis == 0u) {
        return;
    }

    const uint coarse_clip_count = ResolveDirectionalCoarseClipCount(metadata.clip_level_count);
    const uint coarse_begin =
        metadata.clip_level_count > coarse_clip_count
            ? (metadata.clip_level_count - coarse_clip_count)
            : 0u;

    const float center_page = 0.5 * (float)metadata.pages_per_axis;
    const int low_page = int(floor(center_page - 0.5));
    const int high_page = int(ceil(center_page - 0.5));

    [loop]
    for (uint clip_index = coarse_begin; clip_index < metadata.clip_level_count; ++clip_index) {
        const uint coarse_pages[4] = {
            EncodeDirectionalVirtualPageIndex(
                metadata, pass_constants, clip_index, int2(low_page, low_page)),
            EncodeDirectionalVirtualPageIndex(
                metadata, pass_constants, clip_index, int2(low_page, high_page)),
            EncodeDirectionalVirtualPageIndex(
                metadata, pass_constants, clip_index, int2(high_page, low_page)),
            EncodeDirectionalVirtualPageIndex(
                metadata, pass_constants, clip_index, int2(high_page, high_page))
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

[shader("compute")]
[numthreads(8, 8, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
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
    if (metadata_count > 0u && dispatch_thread_id.x == 0u && dispatch_thread_id.y == 0u) {
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
            const float3 light_view_pos = mul(metadata.light_view, float4(world_pos, 1.0)).xyz;

            const float camera_dist = length(world_pos - camera_position);
            const float base_page_world =
                max(metadata.clip_metadata[0].origin_page_scale.z, 1.0e-4);
            const float base_texel_world =
                base_page_world / max((float)metadata.page_size_texels, 1.0);
            const float desired_world_footprint = max(
                camera_dist * base_texel_world, 1.0e-4);

            uint clip_index = 0u;
            float2 page_coord = 0.0.xx;
            if (SelectDirectionalVirtualClipForFootprint(
                    metadata,
                    light_view_pos.xy,
                    desired_world_footprint,
                    clip_index,
                    page_coord)) {
                float2 request_page_coord = 0.0.xx;
                if (ProjectDirectionalVirtualClip(
                        metadata, clip_index, light_view_pos.xy, request_page_coord)) {
                    const uint page_count = BuildDirectionalVirtualPageNeighborhood(
                        metadata,
                        pass_constants,
                        clip_index,
                        request_page_coord,
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

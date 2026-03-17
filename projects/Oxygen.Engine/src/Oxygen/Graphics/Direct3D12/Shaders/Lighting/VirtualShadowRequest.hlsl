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
    uint stats_uav_index;
    uint request_word_count;
    uint total_page_count;
    uint _pad0;
    uint2 screen_dimensions;
    uint _pad1;
    uint _pad2;
    float4x4 inv_view_projection_matrix;
};

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
        || !BX_IN_GLOBAL_SRV(pass_constants.page_mark_flags_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.stats_uav_index)) {
        return;
    }

    if (dispatch_thread_id.x >= pass_constants.screen_dimensions.x
        || dispatch_thread_id.y >= pass_constants.screen_dimensions.y) {
        return;
    }

    StructuredBuffer<DirectionalVirtualShadowMetadata> metadata_buffer =
        ResourceDescriptorHeap[pass_constants.virtual_directional_shadow_metadata_index];
    uint metadata_count = 0u;
    uint metadata_stride = 0u;
    metadata_buffer.GetDimensions(metadata_count, metadata_stride);
    if (metadata_count == 0u) {
        return;
    }

    const DirectionalVirtualShadowMetadata metadata = metadata_buffer[0];
    if (metadata.clip_level_count == 0u || metadata.pages_per_axis == 0u) {
        return;
    }

    Texture2D<float> depth_texture = ResourceDescriptorHeap[pass_constants.depth_texture_index];
    RWStructuredBuffer<uint> request_words =
        ResourceDescriptorHeap[pass_constants.request_words_uav_index];
    RWStructuredBuffer<uint> page_mark_flags =
        ResourceDescriptorHeap[pass_constants.page_mark_flags_uav_index];
    RWStructuredBuffer<uint> stats =
        ResourceDescriptorHeap[pass_constants.stats_uav_index];
    const float depth =
        depth_texture.Load(int3(dispatch_thread_id.xy, 0));
    if (depth >= 1.0f) {
        return;
    }
    InterlockedAdd(stats[0], 1u);

    const float3 world_pos =
        ReconstructWorldPosition(pass_constants, dispatch_thread_id.xy, depth);
    const float3 light_view_pos = mul(metadata.light_view, float4(world_pos, 1.0)).xyz;

    // ---- Distance-based clip selection (RC3) ----
    //
    // Clip level is chosen by camera distance alone.  DO NOT replace this
    // with ddx/ddy, neighbor-reconstruction, or any derivative-based
    // footprint — those are surface-angle-dependent and cause wrong clip
    // selection on flat receivers (floors, walls) lit at grazing angles.
    //
    // This formula MUST stay identical to the one in ShadowHelpers.hlsli
    // ComputeVirtualDirectionalShadowVisibility().  If they disagree,
    // the visibility shader will chase pages the request shader never
    // requested, causing page faults and shadow breakup.
    //
    // See ShadowHelpers.hlsli for the full derivation and UE5 reference.
    //
    const float camera_dist = length(world_pos - camera_position);
    const float base_page_world =
        max(metadata.clip_metadata[0].origin_page_scale.z, 1.0e-4);
    const float base_texel_world =
        base_page_world / max((float)metadata.page_size_texels, 1.0);
    const float desired_world_footprint = max(
        camera_dist * base_texel_world, 1.0e-4);

    uint clip_index = 0u;
    float2 page_coord = 0.0.xx;
    if (!SelectDirectionalVirtualClipForFootprint(
            metadata,
            light_view_pos.xy,
            desired_world_footprint,
            clip_index,
            page_coord)) {
        InterlockedAdd(stats[2], 1u);
        return;
    }
    InterlockedAdd(stats[1], 1u);
    if (clip_index < OXYGEN_MAX_VIRTUAL_DIRECTIONAL_CLIP_LEVELS) {
        InterlockedAdd(stats[4u + clip_index], 1u);
    }

    const uint pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis;

    // Request only the footprint-selected clip. Page-table fallback handles
    // coarser continuity transparently; prefetching a finer clip wastes
    // physical tile budget and caused eviction cascades.
    uint request_clips[1];
    uint num_request_clips = 0u;
    request_clips[num_request_clips++] = clip_index;

    [loop]
    for (uint i = 0u; i < num_request_clips; ++i) {
        const uint request_clip = request_clips[i];
        float2 request_page_coord = 0.0.xx;
        if (!ProjectDirectionalVirtualClip(
                metadata, request_clip, light_view_pos.xy, request_page_coord)) {
            continue;
        }
        InterlockedAdd(stats[3], 1u);
        if (request_clip < OXYGEN_MAX_VIRTUAL_DIRECTIONAL_CLIP_LEVELS) {
            InterlockedAdd(
                stats[4u + OXYGEN_MAX_VIRTUAL_DIRECTIONAL_CLIP_LEVELS + request_clip],
                1u);
        }

        const uint page_x =
            min((uint)request_page_coord.x, metadata.pages_per_axis - 1u);
        const uint page_y =
            min((uint)request_page_coord.y, metadata.pages_per_axis - 1u);
        const uint page_index =
            request_clip * pages_per_level + page_y * metadata.pages_per_axis + page_x;
        if (page_index >= pass_constants.total_page_count) {
            continue;
        }
        const uint word_index = page_index / 32u;
        if (word_index >= pass_constants.request_word_count) {
            continue;
        }

        const uint bit_mask = 1u << (page_index % 32u);
        InterlockedOr(request_words[word_index], bit_mask);
        InterlockedOr(page_mark_flags[page_index],
            OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME
            | OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY);
    }
}

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
    uint request_words_uav_index;
    uint request_word_count;
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

static float2 SelectShortestDirectionalFootprintDelta(
    bool negative_valid,
    float2 negative_delta,
    bool positive_valid,
    float2 positive_delta)
{
    if (negative_valid && positive_valid) {
        return length(negative_delta) <= length(positive_delta)
            ? negative_delta
            : positive_delta;
    }
    if (negative_valid) {
        return negative_delta;
    }
    if (positive_valid) {
        return positive_delta;
    }
    return 0.0.xx;
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
        || !BX_IN_GLOBAL_SRV(pass_constants.request_words_uav_index)) {
        return;
    }

    if (dispatch_thread_id.x >= pass_constants.screen_dimensions.x
        || dispatch_thread_id.y >= pass_constants.screen_dimensions.y) {
        return;
    }

    const ShadowFrameBindings shadow_bindings = LoadResolvedShadowFrameBindings();
    if (shadow_bindings.virtual_directional_shadow_metadata_slot == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(shadow_bindings.virtual_directional_shadow_metadata_slot)) {
        return;
    }

    StructuredBuffer<DirectionalVirtualShadowMetadata> metadata_buffer =
        ResourceDescriptorHeap[shadow_bindings.virtual_directional_shadow_metadata_slot];
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
    const float depth =
        depth_texture.Load(int3(dispatch_thread_id.xy, 0));
    if (depth >= 1.0f) {
        return;
    }

    const float3 world_pos =
        ReconstructWorldPosition(pass_constants, dispatch_thread_id.xy, depth);
    const float3 light_view_pos = mul(metadata.light_view, float4(world_pos, 1.0)).xyz;
    const uint2 left_xy = uint2(dispatch_thread_id.x > 0u
            ? dispatch_thread_id.x - 1u
            : dispatch_thread_id.x,
        dispatch_thread_id.y);
    const uint2 right_xy = uint2(
        min(dispatch_thread_id.x + 1u, pass_constants.screen_dimensions.x - 1u),
        dispatch_thread_id.y);
    const uint2 up_xy = uint2(
        dispatch_thread_id.x,
        dispatch_thread_id.y > 0u
            ? dispatch_thread_id.y - 1u
            : dispatch_thread_id.y);
    const uint2 down_xy = uint2(
        dispatch_thread_id.x,
        min(dispatch_thread_id.y + 1u, pass_constants.screen_dimensions.y - 1u));
    const float left_depth = depth_texture.Load(int3(left_xy, 0));
    const float right_depth = depth_texture.Load(int3(right_xy, 0));
    const float up_depth = depth_texture.Load(int3(up_xy, 0));
    const float down_depth = depth_texture.Load(int3(down_xy, 0));
    const bool left_valid =
        left_depth < 1.0f && left_xy.x != dispatch_thread_id.x;
    const float3 right_world_pos = right_depth < 1.0f
        ? ReconstructWorldPosition(pass_constants, right_xy, right_depth)
        : world_pos;
    const bool right_valid =
        right_depth < 1.0f && right_xy.x != dispatch_thread_id.x;
    const float3 left_world_pos = left_valid
        ? ReconstructWorldPosition(pass_constants, left_xy, left_depth)
        : world_pos;
    const bool up_valid =
        up_depth < 1.0f && up_xy.y != dispatch_thread_id.y;
    const float3 down_world_pos = down_depth < 1.0f
        ? ReconstructWorldPosition(pass_constants, down_xy, down_depth)
        : world_pos;
    const bool down_valid =
        down_depth < 1.0f && down_xy.y != dispatch_thread_id.y;
    const float3 up_world_pos = up_valid
        ? ReconstructWorldPosition(pass_constants, up_xy, up_depth)
        : world_pos;
    const float2 light_view_dx = SelectShortestDirectionalFootprintDelta(
        left_valid,
        light_view_pos.xy - mul(metadata.light_view, float4(left_world_pos, 1.0)).xy,
        right_valid,
        mul(metadata.light_view, float4(right_world_pos, 1.0)).xy - light_view_pos.xy);
    const float2 light_view_dy = SelectShortestDirectionalFootprintDelta(
        up_valid,
        light_view_pos.xy - mul(metadata.light_view, float4(up_world_pos, 1.0)).xy,
        down_valid,
        mul(metadata.light_view, float4(down_world_pos, 1.0)).xy - light_view_pos.xy);
    const float desired_world_footprint = max(
        max(length(light_view_dx), length(light_view_dy)),
        1.0e-4);

    uint clip_index = 0u;
    float2 page_coord = 0.0.xx;
    if (!SelectDirectionalVirtualClipForFootprint(
            metadata,
            light_view_pos.xy,
            desired_world_footprint,
            clip_index,
            page_coord)) {
        return;
    }

    const float prefetch_finer =
        ComputeDirectionalVirtualFootprintBlendToFinerClip(
            metadata, clip_index, desired_world_footprint);

    const uint pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis;
    uint request_begin_clip = clip_index;
    if (prefetch_finer > 0.0f && clip_index > 0u) {
        request_begin_clip = clip_index - 1u;
    }
    [loop]
    for (uint request_clip = request_begin_clip;
         request_clip < metadata.clip_level_count;
         ++request_clip) {
        float2 request_page_coord = 0.0.xx;
        if (!ProjectDirectionalVirtualClip(
                metadata, request_clip, light_view_pos.xy, request_page_coord)) {
            continue;
        }

        const uint page_x =
            min((uint)request_page_coord.x, metadata.pages_per_axis - 1u);
        const uint page_y =
            min((uint)request_page_coord.y, metadata.pages_per_axis - 1u);
        const uint page_index =
            request_clip * pages_per_level + page_y * metadata.pages_per_axis + page_x;
        const uint word_index = page_index / 32u;
        if (word_index >= pass_constants.request_word_count) {
            continue;
        }

        const uint bit_mask = 1u << (page_index % 32u);
        InterlockedOr(request_words[word_index], bit_mask);
    }
}

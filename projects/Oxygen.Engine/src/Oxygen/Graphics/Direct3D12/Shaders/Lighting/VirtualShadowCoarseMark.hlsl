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

struct VirtualShadowCoarseMarkPassConstants
{
    uint depth_texture_index;
    uint request_words_uav_index;
    uint request_word_count;
    uint coarse_backbone_begin;
    uint coarse_clip_mask;

    uint2 screen_dimensions;
    uint _pad1;

    float4x4 inv_view_projection_matrix;
};

static float3 ReconstructWorldPosition(
    VirtualShadowCoarseMarkPassConstants pass_constants,
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

    ConstantBuffer<VirtualShadowCoarseMarkPassConstants> pass_constants =
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
    if (metadata.clip_level_count == 0u || metadata.pages_per_axis == 0u
        || pass_constants.coarse_backbone_begin >= metadata.clip_level_count) {
        return;
    }

    Texture2D<float> depth_texture = ResourceDescriptorHeap[pass_constants.depth_texture_index];
    RWStructuredBuffer<uint> request_words =
        ResourceDescriptorHeap[pass_constants.request_words_uav_index];
    const float depth = depth_texture.Load(int3(dispatch_thread_id.xy, 0));
    if (depth >= 1.0f) {
        return;
    }

    const float3 world_pos =
        ReconstructWorldPosition(pass_constants, dispatch_thread_id.xy, depth);
    const float3 light_view_pos = mul(metadata.light_view, float4(world_pos, 1.0)).xyz;
    const uint pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis;

    [loop]
    for (uint clip_index = pass_constants.coarse_backbone_begin;
         clip_index < metadata.clip_level_count;
         ++clip_index) {
        if ((pass_constants.coarse_clip_mask & (1u << clip_index)) == 0u) {
            continue;
        }

        float2 request_page_coord = 0.0.xx;
        if (!ProjectDirectionalVirtualClip(
                metadata, clip_index, light_view_pos.xy, request_page_coord)) {
            continue;
        }

        const uint page_x =
            min((uint)request_page_coord.x, metadata.pages_per_axis - 1u);
        const uint page_y =
            min((uint)request_page_coord.y, metadata.pages_per_axis - 1u);
        const uint page_index =
            clip_index * pages_per_level + page_y * metadata.pages_per_axis + page_x;
        const uint word_index = page_index / 32u;
        if (word_index >= pass_constants.request_word_count) {
            continue;
        }

        const uint bit_mask = 1u << (page_index % 32u);
        InterlockedOr(request_words[word_index], bit_mask);
    }
}

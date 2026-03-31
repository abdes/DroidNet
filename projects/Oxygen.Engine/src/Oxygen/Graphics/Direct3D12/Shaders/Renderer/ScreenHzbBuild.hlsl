//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Renderer-level screen-space HZB construction.
//
// This pass consumes the main-view depth texture produced by DepthPrePass and
// builds reversed-Z closest+furthest screen pyramids used by downstream
// scene-depth consumers. The current frame writes into scratch textures first,
// then the CPU side copies each reduced level into the persistent per-view HZB
// pyramids.

#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint SCREEN_HZB_THREAD_GROUP_SIZE = 8u;

struct ScreenHzbBuildPassConstants
{
    uint source_closest_texture_index;
    uint source_furthest_texture_index;
    uint destination_closest_texture_uav_index;
    uint destination_furthest_texture_uav_index;
    uint source_width;
    uint source_height;
    uint destination_width;
    uint destination_height;
    uint source_texel_step;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

[shader("compute")]
[numthreads(SCREEN_HZB_THREAD_GROUP_SIZE, SCREEN_HZB_THREAD_GROUP_SIZE, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<ScreenHzbBuildPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.destination_width
        || dispatch_thread_id.y >= pass_constants.destination_height) {
        return;
    }

    if (!BX_IsValidSlot(pass_constants.source_closest_texture_index)
        || !BX_IsValidSlot(pass_constants.source_furthest_texture_index)
        || !BX_IsValidSlot(pass_constants.destination_closest_texture_uav_index)
        || !BX_IsValidSlot(pass_constants.destination_furthest_texture_uav_index)) {
        return;
    }

    Texture2D<float> source_closest_texture
        = ResourceDescriptorHeap[pass_constants.source_closest_texture_index];
    Texture2D<float> source_furthest_texture
        = ResourceDescriptorHeap[pass_constants.source_furthest_texture_index];
    RWTexture2D<float> destination_closest_texture
        = ResourceDescriptorHeap[pass_constants.destination_closest_texture_uav_index];
    RWTexture2D<float> destination_furthest_texture
        = ResourceDescriptorHeap[pass_constants.destination_furthest_texture_uav_index];

    if (pass_constants.source_texel_step <= 1u) {
        const float depth
            = source_closest_texture.Load(int3(dispatch_thread_id.xy, 0)).r;
        destination_closest_texture[dispatch_thread_id.xy] = depth;
        destination_furthest_texture[dispatch_thread_id.xy] = depth;
        return;
    }

    const uint2 source_dimensions
        = uint2(pass_constants.source_width, pass_constants.source_height);
    const uint2 base_coord = dispatch_thread_id.xy * pass_constants.source_texel_step;

    float closest_depth = 0.0f;
    float furthest_depth = 1.0f;
    [unroll]
    for (uint offset_y = 0u; offset_y < 2u; ++offset_y) {
        [unroll]
        for (uint offset_x = 0u; offset_x < 2u; ++offset_x) {
            const uint2 sample_coord = min(
                base_coord + uint2(offset_x, offset_y),
                source_dimensions - 1u);
            closest_depth = max(closest_depth,
                source_closest_texture.Load(int3(sample_coord, 0)).r);
            furthest_depth = min(furthest_depth,
                source_furthest_texture.Load(int3(sample_coord, 0)).r);
        }
    }

    destination_closest_texture[dispatch_thread_id.xy] = closest_depth;
    destination_furthest_texture[dispatch_thread_id.xy] = furthest_depth;
}

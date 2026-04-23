//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Vortex Stage 5 screen-space HZB construction.
//
// Builds closest and furthest reversed-Z pyramids from the current scene-depth
// product, writing each reduced mip into scratch UAVs before copying into the
// persistent per-view history textures owned by ScreenHzbModule.

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VORTEX_SCREEN_HZB_THREAD_GROUP_SIZE = 8u;

struct ScreenHzbBuildPassConstants
{
    uint source_closest_texture_index;
    uint source_furthest_texture_index;
    uint destination_closest_texture_uav_index;
    uint destination_furthest_texture_uav_index;
    uint source_width;
    uint source_height;
    uint source_origin_x;
    uint source_origin_y;
    uint destination_width;
    uint destination_height;
    uint source_texel_step;
    uint _pad0;
};

[shader("compute")]
[numthreads(VORTEX_SCREEN_HZB_THREAD_GROUP_SIZE, VORTEX_SCREEN_HZB_THREAD_GROUP_SIZE, 1)]
void VortexScreenHzbBuildCS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    ConstantBuffer<ScreenHzbBuildPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.destination_width
        || dispatch_thread_id.y >= pass_constants.destination_height)
    {
        return;
    }

    const bool build_closest
        = pass_constants.source_closest_texture_index != K_INVALID_BINDLESS_INDEX
        && pass_constants.destination_closest_texture_uav_index != K_INVALID_BINDLESS_INDEX;
    const bool build_furthest
        = pass_constants.source_furthest_texture_index != K_INVALID_BINDLESS_INDEX
        && pass_constants.destination_furthest_texture_uav_index != K_INVALID_BINDLESS_INDEX;
    if (!build_closest && !build_furthest)
    {
        return;
    }

    const uint2 source_dimensions
        = uint2(pass_constants.source_width, pass_constants.source_height);
    const uint sample_step = max(pass_constants.source_texel_step, 1u);
    const uint2 max_source_origin = uint2(
        source_dimensions.x > sample_step ? source_dimensions.x - sample_step : 0u,
        source_dimensions.y > sample_step ? source_dimensions.y - sample_step : 0u);
    const uint2 source_origin = uint2(
        pass_constants.source_origin_x, pass_constants.source_origin_y);
    const uint2 base_coord
        = source_origin + min(dispatch_thread_id.xy * sample_step, max_source_origin);
    const uint2 max_source_coord = source_origin + source_dimensions - 1u;

    if (build_closest)
    {
        Texture2D<float> source_closest_texture
            = ResourceDescriptorHeap[pass_constants.source_closest_texture_index];
        RWTexture2D<float> destination_closest_texture
            = ResourceDescriptorHeap[pass_constants.destination_closest_texture_uav_index];
        float closest_depth = 0.0f;
        [unroll]
        for (uint offset_y = 0u; offset_y < 2u; ++offset_y)
        {
            [unroll]
            for (uint offset_x = 0u; offset_x < 2u; ++offset_x)
            {
                const uint2 sample_coord = min(
                    base_coord + uint2(offset_x, offset_y),
                    max_source_coord);
                closest_depth = max(closest_depth,
                    source_closest_texture.Load(int3(sample_coord, 0)).r);
            }
        }
        destination_closest_texture[dispatch_thread_id.xy] = closest_depth;
    }

    if (build_furthest)
    {
        Texture2D<float> source_furthest_texture
            = ResourceDescriptorHeap[pass_constants.source_furthest_texture_index];
        RWTexture2D<float> destination_furthest_texture
            = ResourceDescriptorHeap[pass_constants.destination_furthest_texture_uav_index];
        float furthest_depth = 1.0f;
        [unroll]
        for (uint offset_y = 0u; offset_y < 2u; ++offset_y)
        {
            [unroll]
            for (uint offset_x = 0u; offset_x < 2u; ++offset_x)
            {
                const uint2 sample_coord = min(
                    base_coord + uint2(offset_x, offset_y),
                    max_source_coord);
                furthest_depth = min(furthest_depth,
                    source_furthest_texture.Load(int3(sample_coord, 0)).r);
            }
        }
        destination_furthest_texture[dispatch_thread_id.xy] = furthest_depth;
    }
}

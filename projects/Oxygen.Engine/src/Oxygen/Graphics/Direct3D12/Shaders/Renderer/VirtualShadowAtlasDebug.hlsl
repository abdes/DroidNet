//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VirtualShadowAtlasDebugPassConstants
{
    uint source_texture_index;
    uint tile_state_buffer_index;
    uint output_texture_uav_index;
    uint stats_uav_index;
    uint2 atlas_dimensions;
    uint atlas_tiles_per_axis;
    uint page_size_texels;
};

static float3 ResolveAtlasDebugColor(
    uint2 pixel_xy,
    uint atlas_tiles_per_axis,
    uint page_size_texels,
    uint tile_state,
    float depth)
{
    const uint2 local_xy = page_size_texels > 0u
        ? uint2(pixel_xy.x % page_size_texels, pixel_xy.y % page_size_texels)
        : 0u.xx;
    const bool page_border =
        page_size_texels > 0u
        && (local_xy.x <= 1u
            || local_xy.y <= 1u
            || local_xy.x + 2u >= page_size_texels
            || local_xy.y + 2u >= page_size_texels);
    const bool cleared = depth >= 0.99999;
    const float checker = (((pixel_xy.x >> 4u) + (pixel_xy.y >> 4u)) & 1u) != 0u
        ? 1.0
        : 0.0;

    float3 color = 0.0.xxx;
    if (cleared) {
        color = lerp(float3(0.05, 0.05, 0.06), float3(0.10, 0.10, 0.12), checker);
    } else {
        const float occupancy = saturate(1.0 - depth);
        const float heat = pow(occupancy, 0.35);
        color = lerp(float3(0.05, 0.12, 0.24), float3(0.88, 0.96, 1.00), heat);
    }

    if (page_border) {
        if (tile_state == 3u) {
            color = float3(1.00, 1.00, 0.00);
        } else if (tile_state == 2u) {
            color = float3(0.10, 0.95, 0.15);
        } else if (tile_state == 1u) {
            color = float3(0.52, 0.52, 0.58);
        } else if (atlas_tiles_per_axis > 0u) {
            color = float3(0.08, 0.32, 1.00);
        }
    }

    return color;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VirtualShadowAtlasDebugPassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IN_GLOBAL_SRV(pass_constants.source_texture_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.tile_state_buffer_index)
        || !BX_IsValidSlot(pass_constants.output_texture_uav_index)
        || !BX_IsValidSlot(pass_constants.stats_uav_index)) {
        return;
    }

    if (dispatch_thread_id.x >= pass_constants.atlas_dimensions.x
        || dispatch_thread_id.y >= pass_constants.atlas_dimensions.y) {
        return;
    }

    Texture2D<float> source_texture =
        ResourceDescriptorHeap[pass_constants.source_texture_index];
    StructuredBuffer<uint> tile_states =
        ResourceDescriptorHeap[pass_constants.tile_state_buffer_index];
    RWTexture2D<float4> output_texture =
        ResourceDescriptorHeap[pass_constants.output_texture_uav_index];
    RWStructuredBuffer<uint> stats_buffer =
        ResourceDescriptorHeap[pass_constants.stats_uav_index];

    const float depth = source_texture.Load(int3(dispatch_thread_id.xy, 0));
    const uint page_size_texels = max(pass_constants.page_size_texels, 1u);
    const uint2 tile_xy = dispatch_thread_id.xy / page_size_texels;
    uint tile_state = 0u;
    if (tile_xy.x < pass_constants.atlas_tiles_per_axis
        && tile_xy.y < pass_constants.atlas_tiles_per_axis) {
        const uint tile_index
            = tile_xy.y * pass_constants.atlas_tiles_per_axis + tile_xy.x;
        tile_state = tile_states[tile_index];
    }
    const float3 color = ResolveAtlasDebugColor(
        dispatch_thread_id.xy,
        pass_constants.atlas_tiles_per_axis,
        page_size_texels,
        tile_state,
        depth);
    InterlockedAdd(stats_buffer[0], 1u);
    if (depth < 0.99999f) {
        InterlockedAdd(stats_buffer[1], 1u);
    }
    InterlockedMin(stats_buffer[2], asuint(depth));
    InterlockedMax(stats_buffer[3], asuint(depth));
    if (tile_state != 0u) {
        InterlockedAdd(stats_buffer[4], 1u);
    }
    output_texture[dispatch_thread_id.xy] = float4(color, 1.0);
}

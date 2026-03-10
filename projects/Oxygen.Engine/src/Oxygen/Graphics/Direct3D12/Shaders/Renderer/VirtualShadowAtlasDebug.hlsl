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
    uint output_texture_uav_index;
    uint2 atlas_dimensions;
    uint page_size_texels;
    uint _pad0;
};

static float3 ResolveAtlasDebugColor(
    uint2 pixel_xy,
    uint page_size_texels,
    float depth)
{
    const bool page_border =
        page_size_texels > 0u
        && ((pixel_xy.x % page_size_texels) == 0u
            || (pixel_xy.y % page_size_texels) == 0u);
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
        color = cleared
            ? float3(0.18, 0.24, 0.18)
            : float3(0.10, 0.95, 0.15);
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
        || !BX_IsValidSlot(pass_constants.output_texture_uav_index)) {
        return;
    }

    if (dispatch_thread_id.x >= pass_constants.atlas_dimensions.x
        || dispatch_thread_id.y >= pass_constants.atlas_dimensions.y) {
        return;
    }

    Texture2D<float> source_texture =
        ResourceDescriptorHeap[pass_constants.source_texture_index];
    RWTexture2D<float4> output_texture =
        ResourceDescriptorHeap[pass_constants.output_texture_uav_index];

    const float depth = source_texture.Load(int3(dispatch_thread_id.xy, 0));
    const float3 color = ResolveAtlasDebugColor(
        dispatch_thread_id.xy,
        max(pass_constants.page_size_texels, 1u),
        depth);
    output_texture[dispatch_thread_id.xy] = float4(color, 1.0);
}

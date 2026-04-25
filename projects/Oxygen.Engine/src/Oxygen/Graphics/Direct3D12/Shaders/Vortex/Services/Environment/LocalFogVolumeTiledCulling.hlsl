//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessAbi.hlsl"

#include "Vortex/Contracts/View/ViewConstants.hlsli"

#include "Vortex/Contracts/Scene/ScreenHzbBindings.hlsli"
#include "Vortex/Contracts/View/ViewFrameBindings.hlsli"
#include "Vortex/Services/Environment/LocalFogVolumeCommon.hlsli"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

[shader("compute")]
[numthreads(8, 8, 1)]
void VortexLocalFogVolumeTiledCullingCS(uint3 dispatch_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX)
    {
        return;
    }

    StructuredBuffer<LocalFogTiledCullingPassConstants> pass_constants_buffer
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const LocalFogTiledCullingPassConstants pass = pass_constants_buffer[0];
    if (pass.instance_buffer_slot == K_INVALID_BINDLESS_INDEX
        || pass.instance_culling_buffer_slot == K_INVALID_BINDLESS_INDEX
        || pass.tile_data_texture_slot == K_INVALID_BINDLESS_INDEX
        || pass.occupied_tile_buffer_slot == K_INVALID_BINDLESS_INDEX
        || pass.indirect_args_buffer_slot == K_INVALID_BINDLESS_INDEX
        || pass.instance_count == 0u
        || dispatch_id.x >= pass.tile_resolution_x
        || dispatch_id.y >= pass.tile_resolution_y)
    {
        return;
    }

    const uint2 tile_coord = dispatch_id.xy;
    RWTexture2DArray<uint> tile_data_texture
        = ResourceDescriptorHeap[pass.tile_data_texture_slot];
    RWStructuredBuffer<uint> occupied_tiles
        = ResourceDescriptorHeap[pass.occupied_tile_buffer_slot];
    RWStructuredBuffer<uint> indirect_args
        = ResourceDescriptorHeap[pass.indirect_args_buffer_slot];
    StructuredBuffer<LocalFogVolumeCullingData> culling_instances
        = ResourceDescriptorHeap[pass.instance_culling_buffer_slot];

    Texture2D<float> furthest_hzb = ResourceDescriptorHeap[0];
    ScreenHzbFrameBindingsData screen_hzb = MakeInvalidScreenHzbBindings();
    const bool use_hzb = pass.use_hzb != 0u;
    if (use_hzb)
    {
        const ViewFrameBindingsData view_bindings
            = LoadVortexViewFrameBindings(bindless_view_frame_bindings_slot);
        screen_hzb = LoadScreenHzbBindings(view_bindings.screen_hzb_frame_slot);
        if (!IsScreenHzbAvailable(screen_hzb)
            || !IsScreenHzbFurthestValid(screen_hzb)
            || screen_hzb.furthest_srv == INVALID_BINDLESS_INDEX)
        {
            return;
        }
        furthest_hzb = ResourceDescriptorHeap[screen_hzb.furthest_srv];
    }

    const float2 tile_min_lerp = float2(tile_coord)
        / float2(max(pass.tile_resolution_x, 1u), max(pass.tile_resolution_y, 1u))
        * pass.view_to_tile_space_ratio;
    float2 tile_max_lerp = float2(tile_coord + 1u)
        / float2(max(pass.tile_resolution_x, 1u), max(pass.tile_resolution_y, 1u))
        * pass.view_to_tile_space_ratio;
    tile_max_lerp = min(tile_max_lerp, 1.0f.xx);

    float4 tile_planes[5];
    tile_planes[0] = lerp(pass.left_plane, pass.right_plane, tile_min_lerp.xxxx);
    tile_planes[1] = lerp(pass.left_plane, pass.right_plane, tile_max_lerp.xxxx) * -1.0f;
    tile_planes[2] = lerp(-pass.top_plane, -pass.bottom_plane, tile_min_lerp.yyyy);
    tile_planes[3] = lerp(-pass.top_plane, -pass.bottom_plane, tile_max_lerp.yyyy) * -1.0f;
    tile_planes[4] = pass.near_plane;

    uint tile_count = 0u;
    [loop]
    for (uint instance_index = 0u; instance_index < pass.instance_count; ++instance_index)
    {
        if (tile_count >= pass.max_instances_per_tile)
        {
            break;
        }

        const LocalFogVolumeCullingData culling_instance
            = culling_instances[instance_index];
        bool outside = false;
        [unroll]
        for (uint plane_index = 0u; plane_index < 5u; ++plane_index)
        {
            if (LocalFogSphereOutsidePlane(tile_planes[plane_index],
                    culling_instance.sphere_world))
            {
                outside = true;
                break;
            }
        }

        if (!outside && use_hzb)
        {
            const LocalFogClipSphere clip_sphere
                = BuildLocalFogClipSphere(culling_instance.sphere_world);
            outside = LocalFogIsOccludedByHzb(furthest_hzb, screen_hzb, clip_sphere);
        }

        if (!outside)
        {
            tile_data_texture[uint3(tile_coord, 1u + tile_count)] = instance_index;
            tile_count++;
        }
    }

    tile_data_texture[uint3(tile_coord, 0u)] = tile_count;
    if (tile_count == 0u)
    {
        return;
    }

    uint write_index = 0u;
    InterlockedAdd(indirect_args[1], 1u, write_index);
    occupied_tiles[write_index] = PackLocalFogTile(tile_coord);
}

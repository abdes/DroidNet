//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMSHADOWHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMSHADOWHELPERS_HLSLI

#include "Vortex/Services/Shadows/Vsm/VsmPageRequestProjection.hlsli"
#include "Vortex/Services/Shadows/Vsm/VsmPageTable.hlsli"

struct VsmProjectedShadowSample
{
    float2 atlas_uv;
    float receiver_depth;
    uint physical_page_index;
    uint atlas_slice;
    uint2 atlas_texel_origin;
    bool valid;
};

static float VsmReceiverDepthBias(VsmProjectionData projection)
{
    return max(projection.receiver_depth_range_pad.z, 0.0);
}

static bool VsmTryProjectMappedSample(
    VsmPageRequestProjection projection,
    StructuredBuffer<VsmShaderPageTableEntry> page_table,
    uint page_table_entry_count,
    float3 world_position_ws,
    uint tiles_per_axis,
    uint page_size_texels,
    out VsmProjectedShadowSample sample)
{
    sample.atlas_uv = 0.0.xx;
    sample.receiver_depth = 0.0;
    sample.physical_page_index = 0u;
    sample.atlas_slice = 0u;
    sample.atlas_texel_origin = 0u.xx;
    sample.valid = false;

    if (projection.map_id == 0u || projection.pages_x == 0u || projection.pages_y == 0u
        || projection.map_pages_x == 0u || projection.map_pages_y == 0u
        || projection.level_count == 0u
        || page_size_texels == 0u || tiles_per_axis == 0u) {
        return false;
    }

    if (projection.projection.clipmap_level >= projection.level_count
        || projection.page_offset_x > projection.map_pages_x
        || projection.page_offset_y > projection.map_pages_y
        || projection.pages_x > projection.map_pages_x - projection.page_offset_x
        || projection.pages_y > projection.map_pages_y - projection.page_offset_y) {
        return false;
    }

    const float4 world = float4(world_position_ws, 1.0);
    const float4 view = mul(projection.projection.view_matrix, world);
    const float4 clip = mul(projection.projection.projection_matrix, view);
    if (abs(clip.w) <= 1.0e-6 || clip.w < 0.0) {
        return false;
    }

    const float3 ndc = clip.xyz / clip.w;
    if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0
        || ndc.z < 0.0 || ndc.z > 1.0) {
        return false;
    }

    const float2 uv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
    const uint local_page_x = min((uint)(uv.x * (float)projection.pages_x), projection.pages_x - 1u);
    const uint local_page_y = min((uint)(uv.y * (float)projection.pages_y), projection.pages_y - 1u);
    const uint page_x = projection.page_offset_x + local_page_x;
    const uint page_y = projection.page_offset_y + local_page_y;
    const uint pages_per_level = projection.map_pages_x * projection.map_pages_y;
    const uint page_table_index = projection.first_page_table_entry
        + projection.projection.clipmap_level * pages_per_level
        + page_y * projection.map_pages_x
        + page_x;
    if (page_table_index >= page_table_entry_count) {
        return false;
    }

    const VsmShaderPageTableEntry entry = page_table[page_table_index];
    if (!VsmIsPageTableEntryMapped(entry)) {
        return false;
    }

    const uint physical_page_index = VsmDecodePageTablePhysicalPage(entry);
    const uint tiles_per_slice = tiles_per_axis * tiles_per_axis;
    const uint atlas_slice = physical_page_index / tiles_per_slice;
    const uint in_slice_index = physical_page_index % tiles_per_slice;
    const uint tile_x = in_slice_index % tiles_per_axis;
    const uint tile_y = in_slice_index / tiles_per_axis;
    const uint2 atlas_texel_origin = uint2(tile_x, tile_y) * page_size_texels;

    const float2 page_uv = frac(float2(
        uv.x * (float)projection.pages_x,
        uv.y * (float)projection.pages_y));
    const float atlas_extent = (float)(tiles_per_axis * page_size_texels);
    sample.atlas_uv = (float2(tile_x, tile_y) * (float)page_size_texels
        + page_uv * (float)page_size_texels) / atlas_extent;
    sample.receiver_depth = saturate(ndc.z + VsmReceiverDepthBias(projection.projection));
    sample.physical_page_index = physical_page_index;
    sample.atlas_slice = atlas_slice;
    sample.atlas_texel_origin = atlas_texel_origin;
    sample.valid = true;
    return true;
}

static bool VsmTryProjectDirectionalSampleWithFallback(
    StructuredBuffer<VsmPageRequestProjection> projections,
    uint projection_count,
    StructuredBuffer<VsmShaderPageTableEntry> page_table,
    uint page_table_entry_count,
    float3 world_position_ws,
    float receiver_view_depth,
    uint tiles_per_axis,
    uint page_size_texels,
    out VsmProjectedShadowSample sample)
{
    sample.atlas_uv = 0.0.xx;
    sample.receiver_depth = 0.0;
    sample.physical_page_index = 0u;
    sample.atlas_slice = 0u;
    sample.atlas_texel_origin = 0u.xx;
    sample.valid = false;

    uint finest_level = 0xffffffffu;
    bool found_mapped_projection = false;

    [loop]
    for (uint i = 0u; i < projection_count; ++i) {
        const VsmPageRequestProjection projection = projections[i];
        if (projection.projection.light_type != VSM_PROJECTION_LIGHT_TYPE_DIRECTIONAL) {
            continue;
        }
        if (receiver_view_depth + 1.0e-3 < projection.projection.receiver_depth_range_pad.x
            || receiver_view_depth - 1.0e-3 > projection.projection.receiver_depth_range_pad.y) {
            continue;
        }

        VsmProjectedShadowSample candidate_sample;
        if (!VsmTryProjectMappedSample(projection, page_table,
                page_table_entry_count, world_position_ws, tiles_per_axis,
                page_size_texels, candidate_sample)) {
            continue;
        }

        if (!found_mapped_projection
            || projection.projection.clipmap_level < finest_level) {
            sample = candidate_sample;
            finest_level = projection.projection.clipmap_level;
            found_mapped_projection = true;
        }
    }

    return found_mapped_projection;
}

static float VsmSampleVisibilityPcf2x2(
    Texture2DArray<float> shadow_texture,
    float2 atlas_uv,
    float receiver_depth,
    uint atlas_slice,
    uint2 atlas_texel_origin,
    uint page_size_texels)
{
    uint width = 0u;
    uint height = 0u;
    uint layers = 0u;
    shadow_texture.GetDimensions(width, height, layers);
    if (width == 0u || height == 0u || atlas_slice >= layers
        || page_size_texels == 0u) {
        return 1.0;
    }

    const float2 pixel = atlas_uv * float2(width, height);
    const int2 base = int2(floor(pixel - 0.5.xx));
    const int2 min_coord = int2(atlas_texel_origin);
    const int2 max_coord = min(
        min_coord + int2((int)page_size_texels - 1, (int)page_size_texels - 1),
        int2((int)width - 1, (int)height - 1));

    float visibility = 0.0;
    [unroll]
    for (int y = 0; y < 2; ++y) {
        [unroll]
        for (int x = 0; x < 2; ++x) {
            const int2 coord = clamp(base + int2(x, y), min_coord, max_coord);
            const float stored_depth = shadow_texture.Load(int4(coord, (int)atlas_slice, 0));
            visibility += receiver_depth >= stored_depth ? 1.0 : 0.0;
        }
    }

    return visibility * 0.25;
}

static float3 VsmReconstructWorldPosition(
    float2 screen_uv, float depth, float4x4 inverse_view_projection)
{
    const float4 clip = float4(
        screen_uv.x * 2.0 - 1.0,
        1.0 - screen_uv.y * 2.0,
        depth,
        1.0);
    const float4 world = mul(inverse_view_projection, clip);
    return world.xyz / max(world.w, 1.0e-6);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMSHADOWHELPERS_HLSLI

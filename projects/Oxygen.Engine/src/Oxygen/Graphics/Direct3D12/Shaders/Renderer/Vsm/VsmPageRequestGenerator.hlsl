//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Stage 5 page-request generation.
//
// The pass reads the main-view depth buffer, reconstructs visible world-space
// points, projects them into every active VSM map, and marks the per-frame
// request-flag buffer. Local-light entries can be pruned against the clustered
// light grid so large light sets do not explode page demand for pixels they do
// not affect.

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/Vsm/VsmPageRequestFlags.hlsli"
#include "Renderer/Vsm/VsmPageRequestProjection.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_PAGE_REQUEST_THREAD_GROUP_SIZE = 8u;

struct VsmPageRequestGeneratorPassConstants
{
    uint depth_texture_index;
    uint projection_buffer_index;
    uint page_request_flags_uav_index;
    uint cluster_grid_index;
    uint light_index_list_index;
    uint2 screen_dimensions;
    uint projection_count;
    uint virtual_page_count;
    float4x4 inverse_view_projection;
    uint cluster_dim_x;
    uint cluster_dim_y;
    uint tile_size_px;
    uint enable_light_grid_pruning;
};

static float3 VsmReconstructWorldPosition(
    const uint2 pixel_coord, const float depth, const uint2 screen_dimensions,
    const float4x4 inverse_view_projection)
{
    const float2 uv
        = (float2(pixel_coord) + 0.5f) / float2(screen_dimensions);
    const float2 ndc_xy = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 clip = float4(ndc_xy, depth, 1.0f);
    const float4 world = mul(inverse_view_projection, clip);
    return world.xyz / max(world.w, 1.0e-6f);
}

static bool VsmClusterContainsLight(const StructuredBuffer<uint2> cluster_grid,
    const StructuredBuffer<uint> light_index_list, const uint cluster_index,
    const uint light_index)
{
    const uint2 cluster = cluster_grid[cluster_index];
    const uint list_offset = cluster.x;
    const uint list_count = cluster.y;
    for (uint i = 0u; i < list_count; ++i) {
        if (light_index_list[list_offset + i] == light_index) {
            return true;
        }
    }
    return false;
}

static bool VsmTryProjectToPage(const VsmPageRequestProjection projection,
    const float3 world_position_ws, out uint fine_page_index,
    out uint coarse_page_index)
{
    fine_page_index = 0u;
    coarse_page_index = 0u;

    if (projection.map_id == 0u || projection.pages_x == 0u
        || projection.pages_y == 0u || projection.map_pages_x == 0u
        || projection.map_pages_y == 0u || projection.level_count == 0u) {
        return false;
    }

    if (projection.page_offset_x > projection.map_pages_x
        || projection.page_offset_y > projection.map_pages_y
        || projection.pages_x > projection.map_pages_x - projection.page_offset_x
        || projection.pages_y > projection.map_pages_y - projection.page_offset_y) {
        return false;
    }

    const float4 world = float4(world_position_ws, 1.0f);
    const float4 view = mul(projection.projection.view_matrix, world);
    const float4 clip = mul(projection.projection.projection_matrix, view);
    if (abs(clip.w) <= 1.0e-6f || clip.w < 0.0f) {
        return false;
    }

    const float3 ndc = clip.xyz / clip.w;
    if (ndc.x < -1.0f || ndc.x > 1.0f || ndc.y < -1.0f || ndc.y > 1.0f
        || ndc.z < 0.0f || ndc.z > 1.0f) {
        return false;
    }

    const float2 uv = float2(
        ndc.x * 0.5f + 0.5f,
        0.5f - ndc.y * 0.5f);
    const uint local_page_x
        = min((uint)(uv.x * projection.pages_x), projection.pages_x - 1u);
    const uint local_page_y
        = min((uint)(uv.y * projection.pages_y), projection.pages_y - 1u);
    const uint page_x = projection.page_offset_x + local_page_x;
    const uint page_y = projection.page_offset_y + local_page_y;
    const uint pages_per_level = projection.map_pages_x * projection.map_pages_y;
    fine_page_index = projection.first_page_table_entry
        + projection.projection.clipmap_level * pages_per_level
        + page_y * projection.map_pages_x + page_x;

    const uint coarse_level = min(projection.coarse_level, projection.level_count - 1u);
    coarse_page_index = projection.first_page_table_entry
        + coarse_level * pages_per_level
        + page_y * projection.map_pages_x + page_x;
    return true;
}

[shader("compute")]
[numthreads(VSM_PAGE_REQUEST_THREAD_GROUP_SIZE, VSM_PAGE_REQUEST_THREAD_GROUP_SIZE, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    // These slots come from dynamically allocated shader-visible descriptors.
    // They are valid absolute heap indices, but they do not live inside the
    // generated static bindless subranges used by BX_IN_* domain checks.
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmPageRequestGeneratorPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const uint2 screen_dimensions = pass_constants.screen_dimensions;
    if (dispatch_thread_id.x >= screen_dimensions.x
        || dispatch_thread_id.y >= screen_dimensions.y
        || pass_constants.virtual_page_count == 0u
        || pass_constants.projection_count == 0u) {
        return;
    }

    if (!BX_IsValidSlot(pass_constants.depth_texture_index)
        || !BX_IsValidSlot(pass_constants.projection_buffer_index)
        || !BX_IsValidSlot(pass_constants.page_request_flags_uav_index)) {
        return;
    }

    Texture2D<float> depth_tex = ResourceDescriptorHeap[pass_constants.depth_texture_index];
    StructuredBuffer<VsmPageRequestProjection> projections
        = ResourceDescriptorHeap[pass_constants.projection_buffer_index];
    RWStructuredBuffer<VsmShaderPageRequestFlags> request_flags
        = ResourceDescriptorHeap[pass_constants.page_request_flags_uav_index];

    const float depth = depth_tex.Load(int3(dispatch_thread_id.xy, 0)).r;
    if (depth <= 0.0f) {
        return;
    }

    const float3 world_position_ws = VsmReconstructWorldPosition(
        dispatch_thread_id.xy, depth, screen_dimensions,
        pass_constants.inverse_view_projection);

    const bool has_light_grid
        = BX_IsValidSlot(pass_constants.cluster_grid_index)
        && BX_IsValidSlot(pass_constants.light_index_list_index)
        && pass_constants.cluster_dim_x != 0u
        && pass_constants.cluster_dim_y != 0u;

    uint cluster_index = 0u;
    if (has_light_grid) {
        const uint2 cluster_xy = min(
            dispatch_thread_id.xy / max(pass_constants.tile_size_px, 1u),
            uint2(pass_constants.cluster_dim_x - 1u, pass_constants.cluster_dim_y - 1u));
        cluster_index = cluster_xy.y * pass_constants.cluster_dim_x + cluster_xy.x;
    }

    for (uint i = 0u; i < pass_constants.projection_count; ++i) {
        const VsmPageRequestProjection projection = projections[i];
        if (pass_constants.enable_light_grid_pruning != 0u
            && projection.light_index != VSM_INVALID_LIGHT_INDEX
            && has_light_grid
            ) {
            StructuredBuffer<uint2> cluster_grid
                = ResourceDescriptorHeap[pass_constants.cluster_grid_index];
            StructuredBuffer<uint> light_index_list
                = ResourceDescriptorHeap[pass_constants.light_index_list_index];
            if (!VsmClusterContainsLight(
                    cluster_grid, light_index_list, cluster_index,
                    projection.light_index)) {
                continue;
            }
        }

        uint fine_page_index = 0u;
        uint coarse_page_index = 0u;
        if (!VsmTryProjectToPage(
                projection, world_position_ws, fine_page_index, coarse_page_index)) {
            continue;
        }

        if (fine_page_index < pass_constants.virtual_page_count) {
            InterlockedOr(
                request_flags[fine_page_index].bits, VSM_PAGE_REQUEST_FLAG_REQUIRED);
        }
        if (projection.coarse_level > 0u
            && coarse_page_index < pass_constants.virtual_page_count) {
            InterlockedOr(request_flags[coarse_page_index].bits,
                VSM_PAGE_REQUEST_FLAG_REQUIRED | VSM_PAGE_REQUEST_FLAG_COARSE);
        }
    }
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Phase H shadow-space HZB update.
//
// The pass is deliberately split into four entry points:
// - select pages whose derived HZB data must be rebuilt
// - initialize a scratch rect when previous HZB contents cannot be preserved
// - rebuild page-local HZB mips only for selected pages
// - rebuild top HZB mips globally from the page-top mip

#include "Renderer/Vsm/VsmPhysicalPageMeta.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_HZB_SELECT_THREAD_GROUP_SIZE = 64u;
static const uint VSM_HZB_BUILD_THREAD_GROUP_SIZE = 8u;

struct VsmHzbSelectPagesPassConstants
{
    uint dirty_flags_uav_index;
    uint physical_meta_uav_index;
    uint selected_pages_uav_index;
    uint selected_page_count_uav_index;
    uint physical_page_count;
    uint force_rebuild_all_allocated_pages;
    uint tiles_per_axis;
    uint dynamic_slice_index;
};

struct VsmHzbScratchInitPassConstants
{
    uint destination_hzb_uav_index;
    uint destination_width;
    uint destination_height;
    float clear_depth;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct VsmHzbPerPageBuildPassConstants
{
    uint shadow_depth_srv_index;
    uint source_hzb_srv_index;
    uint destination_hzb_uav_index;
    uint selected_pages_srv_index;
    uint selected_page_count_srv_index;
    uint page_size_texels;
    uint tiles_per_axis;
    uint destination_page_extent_texels;
    uint source_is_shadow_depth;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

struct VsmHzbPrepareDispatchArgsPassConstants
{
    uint selected_page_count_srv_index;
    uint dispatch_args_uav_index;
    uint thread_group_count_x;
    uint thread_group_count_y;
};

struct VsmHzbTopBuildPassConstants
{
    uint source_hzb_srv_index;
    uint destination_hzb_uav_index;
    uint destination_width;
    uint destination_height;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    uint _pad3;
};

static float VsmMaxReduce4(const float a, const float b, const float c, const float d)
{
    return max(max(a, b), max(c, d));
}

[shader("compute")]
[numthreads(VSM_HZB_SELECT_THREAD_GROUP_SIZE, 1, 1)]
void CS_SelectPages(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmHzbSelectPagesPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    const uint physical_page_index = dispatch_thread_id.x;
    if (physical_page_index >= pass_constants.physical_page_count) {
        return;
    }

    if (!BX_IsValidSlot(pass_constants.dirty_flags_uav_index)
        || !BX_IsValidSlot(pass_constants.physical_meta_uav_index)
        || !BX_IsValidSlot(pass_constants.selected_pages_uav_index)
        || !BX_IsValidSlot(pass_constants.selected_page_count_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> dirty_flags
        = ResourceDescriptorHeap[pass_constants.dirty_flags_uav_index];
    RWStructuredBuffer<VsmPhysicalPageMeta> physical_meta
        = ResourceDescriptorHeap[pass_constants.physical_meta_uav_index];
    RWStructuredBuffer<uint> selected_pages
        = ResourceDescriptorHeap[pass_constants.selected_pages_uav_index];
    RWStructuredBuffer<uint> selected_page_count
        = ResourceDescriptorHeap[pass_constants.selected_page_count_uav_index];

    const uint tiles_per_slice = pass_constants.tiles_per_axis * pass_constants.tiles_per_axis;
    if (tiles_per_slice == 0u) {
        return;
    }
    const uint page_slice = physical_page_index / tiles_per_slice;
    if (page_slice != pass_constants.dynamic_slice_index) {
        return;
    }

    const uint dirty_bits = dirty_flags[physical_page_index];
    VsmPhysicalPageMeta meta = physical_meta[physical_page_index];
    const bool force_rebuild = pass_constants.force_rebuild_all_allocated_pages != 0u;
    const bool needs_rebuild = meta.is_allocated != 0u
        && (force_rebuild || dirty_bits != 0u || meta.is_dirty != 0u || meta.view_uncached != 0u);

    if (dirty_bits != 0u) {
        dirty_flags[physical_page_index] = 0u;
    }

    if (!needs_rebuild) {
        return;
    }

    meta.is_dirty = 0u;
    meta.view_uncached = 0u;
    physical_meta[physical_page_index] = meta;

    uint selected_page_list_index = 0u;
    InterlockedAdd(selected_page_count[0], 1u, selected_page_list_index);
    selected_pages[selected_page_list_index] = physical_page_index;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void CS_PrepareDispatchArgs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }
    if (dispatch_thread_id.x != 0u || dispatch_thread_id.y != 0u || dispatch_thread_id.z != 0u) {
        return;
    }

    ConstantBuffer<VsmHzbPrepareDispatchArgsPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (!BX_IsValidSlot(pass_constants.selected_page_count_srv_index)
        || !BX_IsValidSlot(pass_constants.dispatch_args_uav_index)) {
        return;
    }

    StructuredBuffer<uint> selected_page_count
        = ResourceDescriptorHeap[pass_constants.selected_page_count_srv_index];
    RWStructuredBuffer<uint> dispatch_args
        = ResourceDescriptorHeap[pass_constants.dispatch_args_uav_index];

    dispatch_args[0] = pass_constants.thread_group_count_x;
    dispatch_args[1] = pass_constants.thread_group_count_y;
    dispatch_args[2] = selected_page_count[0];
}

[shader("compute")]
[numthreads(VSM_HZB_BUILD_THREAD_GROUP_SIZE, VSM_HZB_BUILD_THREAD_GROUP_SIZE, 1)]
void CS_ClearScratchRect(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmHzbScratchInitPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.destination_width
        || dispatch_thread_id.y >= pass_constants.destination_height
        || !BX_IsValidSlot(pass_constants.destination_hzb_uav_index)) {
        return;
    }

    RWTexture2D<float> destination_hzb
        = ResourceDescriptorHeap[pass_constants.destination_hzb_uav_index];
    destination_hzb[dispatch_thread_id.xy] = pass_constants.clear_depth;
}

[shader("compute")]
[numthreads(VSM_HZB_BUILD_THREAD_GROUP_SIZE, VSM_HZB_BUILD_THREAD_GROUP_SIZE, 1)]
void CS_BuildPerPage(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmHzbPerPageBuildPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (pass_constants.destination_page_extent_texels == 0u
        || pass_constants.tiles_per_axis == 0u
        || !BX_IsValidSlot(pass_constants.destination_hzb_uav_index)
        || !BX_IsValidSlot(pass_constants.selected_pages_srv_index)
        || !BX_IsValidSlot(pass_constants.selected_page_count_srv_index)) {
        return;
    }

    StructuredBuffer<uint> selected_pages
        = ResourceDescriptorHeap[pass_constants.selected_pages_srv_index];
    StructuredBuffer<uint> selected_page_count
        = ResourceDescriptorHeap[pass_constants.selected_page_count_srv_index];
    RWTexture2D<float> destination_hzb
        = ResourceDescriptorHeap[pass_constants.destination_hzb_uav_index];

    const uint selected_page_list_index = dispatch_thread_id.z;
    const uint selected_page_count_value = selected_page_count[0];
    if (selected_page_list_index >= selected_page_count_value) {
        return;
    }

    const uint local_x = dispatch_thread_id.x;
    const uint local_y = dispatch_thread_id.y;
    if (local_x >= pass_constants.destination_page_extent_texels
        || local_y >= pass_constants.destination_page_extent_texels) {
        return;
    }

    const uint physical_page_index = selected_pages[selected_page_list_index];
    const uint tiles_per_slice = pass_constants.tiles_per_axis * pass_constants.tiles_per_axis;
    const uint in_slice_page_index = physical_page_index % tiles_per_slice;
    const uint tile_x = in_slice_page_index % pass_constants.tiles_per_axis;
    const uint tile_y = in_slice_page_index / pass_constants.tiles_per_axis;

    const uint destination_base_x
        = tile_x * pass_constants.destination_page_extent_texels;
    const uint destination_base_y
        = tile_y * pass_constants.destination_page_extent_texels;
    const uint2 destination_texel
        = uint2(destination_base_x + local_x, destination_base_y + local_y);

    const uint source_page_extent_texels = pass_constants.destination_page_extent_texels * 2u;
    const uint source_base_x = tile_x * source_page_extent_texels;
    const uint source_base_y = tile_y * source_page_extent_texels;
    const uint2 source_texel = uint2(source_base_x + local_x * 2u,
        source_base_y + local_y * 2u);

    float reduced_depth = 0.0f;
    if (pass_constants.source_is_shadow_depth != 0u) {
        if (!BX_IsValidSlot(pass_constants.shadow_depth_srv_index)) {
            return;
        }
        Texture2DArray<float> shadow_depth
            = ResourceDescriptorHeap[pass_constants.shadow_depth_srv_index];
        reduced_depth = VsmMaxReduce4(
            shadow_depth[uint3(source_texel.x, source_texel.y, 0u)],
            shadow_depth[uint3(source_texel.x + 1u, source_texel.y, 0u)],
            shadow_depth[uint3(source_texel.x, source_texel.y + 1u, 0u)],
            shadow_depth[uint3(source_texel.x + 1u, source_texel.y + 1u, 0u)]);
    } else {
        if (!BX_IsValidSlot(pass_constants.source_hzb_srv_index)) {
            return;
        }
        Texture2D<float> source_hzb
            = ResourceDescriptorHeap[pass_constants.source_hzb_srv_index];
        reduced_depth = VsmMaxReduce4(
            source_hzb[source_texel],
            source_hzb[uint2(source_texel.x + 1u, source_texel.y)],
            source_hzb[uint2(source_texel.x, source_texel.y + 1u)],
            source_hzb[uint2(source_texel.x + 1u, source_texel.y + 1u)]);
    }

    destination_hzb[destination_texel] = reduced_depth;
}

[shader("compute")]
[numthreads(VSM_HZB_BUILD_THREAD_GROUP_SIZE, VSM_HZB_BUILD_THREAD_GROUP_SIZE, 1)]
void CS_BuildTopLevels(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmHzbTopBuildPassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (dispatch_thread_id.x >= pass_constants.destination_width
        || dispatch_thread_id.y >= pass_constants.destination_height
        || !BX_IsValidSlot(pass_constants.source_hzb_srv_index)
        || !BX_IsValidSlot(pass_constants.destination_hzb_uav_index)) {
        return;
    }

    Texture2D<float> source_hzb
        = ResourceDescriptorHeap[pass_constants.source_hzb_srv_index];
    RWTexture2D<float> destination_hzb
        = ResourceDescriptorHeap[pass_constants.destination_hzb_uav_index];

    const uint2 source_texel = dispatch_thread_id.xy * 2u;
    destination_hzb[dispatch_thread_id.xy] = VsmMaxReduce4(
        source_hzb[source_texel],
        source_hzb[uint2(source_texel.x + 1u, source_texel.y)],
        source_hzb[uint2(source_texel.x, source_texel.y + 1u)],
        source_hzb[uint2(source_texel.x + 1u, source_texel.y + 1u)]);
}

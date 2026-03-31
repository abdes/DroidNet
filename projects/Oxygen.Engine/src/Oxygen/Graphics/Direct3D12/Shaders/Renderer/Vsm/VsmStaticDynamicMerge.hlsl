//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Phase G static/dynamic merge.
//
// Stage 13 should only touch pages that were rasterized into the static slice
// during the current frame. The caller now supplies those page candidates
// explicitly, and the shader validates the current-frame dirty/meta state for
// each page before composing static depth back into the lighting-visible
// dynamic page.

#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/Vsm/VsmPhysicalPageMeta.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_STATIC_DYNAMIC_MERGE_GROUP_SIZE = 8u;
static const uint VSM_RENDERED_PAGE_DIRTY_STATIC_RASTERIZED = 1u << 1u;

struct VsmStaticDynamicMergePassConstants
{
    uint static_shadow_srv_index;
    uint dynamic_shadow_uav_index;
    uint dirty_flags_srv_index;
    uint physical_meta_srv_index;
    uint page_size_texels;
    uint tiles_per_axis;
    uint logical_page_index;
    uint logical_page_count;
    uint dynamic_slice_index;
    uint static_slice_index;
    uint _pad0;
    uint _pad1;
};

[shader("compute")]
[numthreads(VSM_STATIC_DYNAMIC_MERGE_GROUP_SIZE, VSM_STATIC_DYNAMIC_MERGE_GROUP_SIZE, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (!BX_IsValidSlot(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VsmStaticDynamicMergePassConstants> pass_constants
        = ResourceDescriptorHeap[g_PassConstantsIndex];
    if (pass_constants.page_size_texels == 0u
        || pass_constants.tiles_per_axis == 0u
        || pass_constants.logical_page_count == 0u
        || !BX_IsValidSlot(pass_constants.static_shadow_srv_index)
        || !BX_IsValidSlot(pass_constants.dynamic_shadow_uav_index)
        || !BX_IsValidSlot(pass_constants.dirty_flags_srv_index)
        || !BX_IsValidSlot(pass_constants.physical_meta_srv_index)) {
        return;
    }

    const uint local_x = dispatch_thread_id.x;
    const uint local_y = dispatch_thread_id.y;
    if (local_x >= pass_constants.page_size_texels
        || local_y >= pass_constants.page_size_texels) {
        return;
    }

    const uint logical_page_index = pass_constants.logical_page_index;
    if (logical_page_index >= pass_constants.logical_page_count) {
        return;
    }

    const uint dynamic_physical_page_index
        = pass_constants.dynamic_slice_index * pass_constants.logical_page_count
        + logical_page_index;
    const uint static_physical_page_index
        = pass_constants.static_slice_index * pass_constants.logical_page_count
        + logical_page_index;

    StructuredBuffer<uint> dirty_flags
        = ResourceDescriptorHeap[pass_constants.dirty_flags_srv_index];
    StructuredBuffer<VsmPhysicalPageMeta> physical_meta
        = ResourceDescriptorHeap[pass_constants.physical_meta_srv_index];
    Texture2DArray<float> static_shadow
        = ResourceDescriptorHeap[pass_constants.static_shadow_srv_index];
    RWTexture2D<float> dynamic_shadow
        = ResourceDescriptorHeap[pass_constants.dynamic_shadow_uav_index];

    const uint static_dirty = dirty_flags[static_physical_page_index];
    if ((static_dirty & VSM_RENDERED_PAGE_DIRTY_STATIC_RASTERIZED) == 0u) {
        return;
    }

    const VsmPhysicalPageMeta dynamic_meta
        = physical_meta[dynamic_physical_page_index];
    const VsmPhysicalPageMeta static_meta
        = physical_meta[static_physical_page_index];
    const bool can_merge = dynamic_meta.is_allocated != 0u
        && static_meta.is_allocated != 0u
        && static_meta.static_invalidated == 0u;
    if (!can_merge) {
        return;
    }

    const uint tile_x = logical_page_index % pass_constants.tiles_per_axis;
    const uint tile_y = logical_page_index / pass_constants.tiles_per_axis;
    const uint atlas_x = tile_x * pass_constants.page_size_texels + local_x;
    const uint atlas_y = tile_y * pass_constants.page_size_texels + local_y;

    const float static_depth = static_shadow[uint3(atlas_x, atlas_y, 0u)];
    const uint2 page_texel = uint2(local_x, local_y);
    const float dynamic_depth = dynamic_shadow[page_texel];
    dynamic_shadow[page_texel] = min(static_depth, dynamic_depth);
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Phase G static/dynamic merge.
//
// Inputs:
// - slice 1 SRV: static cached depth
// - scratch UAV: dynamic lighting-visible depth copied out of slice 0
// - transient dirty flags buffer from raster publication
// - persistent physical-page metadata
//
// Behavior:
// - operate only on dirty physical pages
// - skip pages whose static cache is invalidated
// - composite static cached depth into scratch using the fixed
//   slice1 -> slice0 direction
// - static recache remains a separate path; this pass never refreshes slice 1

#include "Renderer/Vsm/VsmPhysicalPageMeta.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_STATIC_DYNAMIC_MERGE_GROUP_SIZE = 8u;

struct VsmStaticDynamicMergePassConstants
{
    uint static_shadow_srv_index;
    uint dynamic_shadow_uav_index;
    uint dirty_flags_srv_index;
    uint physical_meta_srv_index;
    uint page_size_texels;
    uint tiles_per_axis;
    uint physical_page_count;
    uint _pad0;
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
        || dispatch_thread_id.z >= pass_constants.physical_page_count) {
        return;
    }

    if (!BX_IsValidSlot(pass_constants.static_shadow_srv_index)
        || !BX_IsValidSlot(pass_constants.dynamic_shadow_uav_index)
        || !BX_IsValidSlot(pass_constants.dirty_flags_srv_index)
        || !BX_IsValidSlot(pass_constants.physical_meta_srv_index)) {
        return;
    }

    StructuredBuffer<uint> dirty_flags
        = ResourceDescriptorHeap[pass_constants.dirty_flags_srv_index];
    StructuredBuffer<VsmPhysicalPageMeta> physical_meta
        = ResourceDescriptorHeap[pass_constants.physical_meta_srv_index];
    Texture2DArray<float> static_shadow
        = ResourceDescriptorHeap[pass_constants.static_shadow_srv_index];
    RWTexture2D<float> dynamic_shadow
        = ResourceDescriptorHeap[pass_constants.dynamic_shadow_uav_index];

    const uint physical_page_index = dispatch_thread_id.z;
    if (dirty_flags[physical_page_index] == 0u) {
        return;
    }

    const VsmPhysicalPageMeta meta = physical_meta[physical_page_index];
    if (meta.is_allocated == 0u || meta.static_invalidated != 0u) {
        return;
    }

    const uint local_x = dispatch_thread_id.x;
    const uint local_y = dispatch_thread_id.y;
    if (local_x >= pass_constants.page_size_texels
        || local_y >= pass_constants.page_size_texels) {
        return;
    }

    const uint tiles_per_slice = pass_constants.tiles_per_axis * pass_constants.tiles_per_axis;
    const uint in_slice_index = physical_page_index % tiles_per_slice;
    const uint tile_x = in_slice_index % pass_constants.tiles_per_axis;
    const uint tile_y = in_slice_index / pass_constants.tiles_per_axis;
    const uint atlas_x = tile_x * pass_constants.page_size_texels + local_x;
    const uint atlas_y = tile_y * pass_constants.page_size_texels + local_y;

    const uint3 static_page_texel = uint3(atlas_x, atlas_y, 0u);
    const uint2 dynamic_page_texel = uint2(atlas_x, atlas_y);
    const float static_depth = static_shadow[static_page_texel];
    const float dynamic_depth = dynamic_shadow[dynamic_page_texel];
    dynamic_shadow[dynamic_page_texel] = min(static_depth, dynamic_depth);
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/Vsm/VsmShadowHelpers.hlsli"

static const uint VSM_PROJECTION_THREAD_GROUP_SIZE = 8u;

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VsmProjectionClearPassConstants
{
    uint shadow_mask_uav_index;
    uint output_width;
    uint output_height;
    uint _pad0;
};

struct VsmProjectionCompositePassConstants
{
    uint scene_depth_srv_index;
    uint shadow_mask_uav_index;
    uint projection_buffer_srv_index;
    uint page_table_buffer_srv_index;
    uint shadow_texture_srv_index;
    uint page_table_entry_count;
    uint projection_count;
    uint output_width;
    uint output_height;
    uint page_size_texels;
    uint tiles_per_axis;
    uint dynamic_slice_index;
    float4x4 inverse_view_projection;
};

static VsmProjectionClearPassConstants LoadClearPassConstants()
{
    ConstantBuffer<VsmProjectionClearPassConstants> constants_buffer =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    return constants_buffer;
}

static VsmProjectionCompositePassConstants LoadCompositePassConstants()
{
    ConstantBuffer<VsmProjectionCompositePassConstants> constants_buffer =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    return constants_buffer;
}

[numthreads(VSM_PROJECTION_THREAD_GROUP_SIZE, VSM_PROJECTION_THREAD_GROUP_SIZE, 1)]
void CS_ClearShadowMask(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const VsmProjectionClearPassConstants pass_constants = LoadClearPassConstants();
    if (dispatch_thread_id.x >= pass_constants.output_width
        || dispatch_thread_id.y >= pass_constants.output_height
        || pass_constants.shadow_mask_uav_index == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    RWTexture2D<float> shadow_mask = ResourceDescriptorHeap[pass_constants.shadow_mask_uav_index];
    shadow_mask[dispatch_thread_id.xy] = 1.0;
}

[numthreads(VSM_PROJECTION_THREAD_GROUP_SIZE, VSM_PROJECTION_THREAD_GROUP_SIZE, 1)]
void CS_ProjectDirectional(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const VsmProjectionCompositePassConstants pass_constants = LoadCompositePassConstants();
    if (dispatch_thread_id.x >= pass_constants.output_width
        || dispatch_thread_id.y >= pass_constants.output_height
        || pass_constants.projection_count == 0u
        || pass_constants.scene_depth_srv_index == K_INVALID_BINDLESS_INDEX
        || pass_constants.shadow_mask_uav_index == K_INVALID_BINDLESS_INDEX
        || pass_constants.projection_buffer_srv_index == K_INVALID_BINDLESS_INDEX
        || pass_constants.page_table_buffer_srv_index == K_INVALID_BINDLESS_INDEX
        || pass_constants.shadow_texture_srv_index == K_INVALID_BINDLESS_INDEX) {
        return;
    }

    Texture2D<float> scene_depth = ResourceDescriptorHeap[pass_constants.scene_depth_srv_index];
    RWTexture2D<float> shadow_mask = ResourceDescriptorHeap[pass_constants.shadow_mask_uav_index];
    StructuredBuffer<VsmPageRequestProjection> projections =
        ResourceDescriptorHeap[pass_constants.projection_buffer_srv_index];
    StructuredBuffer<VsmShaderPageTableEntry> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_buffer_srv_index];
    Texture2DArray<float> shadow_texture =
        ResourceDescriptorHeap[pass_constants.shadow_texture_srv_index];

    const float depth = scene_depth.Load(int3(dispatch_thread_id.xy, 0));
    if (depth >= 1.0) {
        return;
    }

    const float2 screen_uv = (float2(dispatch_thread_id.xy) + 0.5)
        / float2(pass_constants.output_width, pass_constants.output_height);
    const float3 world_position_ws = VsmReconstructWorldPosition(
        screen_uv, depth, pass_constants.inverse_view_projection);

    float best_visibility = 1.0;
    uint best_level = 0xffffffffu;

    [loop]
    for (uint i = 0u; i < pass_constants.projection_count; ++i) {
        const VsmPageRequestProjection projection = projections[i];
        if (projection.projection.light_type != VSM_PROJECTION_LIGHT_TYPE_DIRECTIONAL) {
            continue;
        }

        VsmProjectedShadowSample sample;
        if (!VsmTryProjectMappedSample(projection, page_table,
                pass_constants.page_table_entry_count, world_position_ws,
                pass_constants.tiles_per_axis, pass_constants.page_size_texels, sample)) {
            continue;
        }

        const float visibility = VsmSampleVisibilityPcf2x2(
            shadow_texture, sample.atlas_uv, sample.receiver_depth,
            pass_constants.dynamic_slice_index);
        if (projection.projection.clipmap_level < best_level) {
            best_level = projection.projection.clipmap_level;
            best_visibility = visibility;
        }
    }

    shadow_mask[dispatch_thread_id.xy] *= best_visibility;
}

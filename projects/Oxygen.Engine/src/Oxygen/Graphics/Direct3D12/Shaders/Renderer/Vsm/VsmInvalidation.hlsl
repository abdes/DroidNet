//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/Vsm/VsmInvalidationWorkItem.hlsli"
#include "Renderer/Vsm/VsmPageRequestProjection.hlsli"
#include "Renderer/Vsm/VsmPageTable.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

static const uint VSM_INVALIDATION_SCOPE_DYNAMIC_ONLY = 0u;
static const uint VSM_INVALIDATION_SCOPE_STATIC_ONLY = 1u;
static const uint VSM_INVALIDATION_SCOPE_STATIC_AND_DYNAMIC = 2u;

static const uint VSM_PHYSICAL_META_STATIC_INVALIDATED_OFFSET = 16u;
static const uint VSM_PHYSICAL_META_DYNAMIC_INVALIDATED_OFFSET = 20u;
static const uint VSM_PHYSICAL_META_STRIDE_BYTES = 56u;

struct VsmInvalidationPassConstants
{
    uint projection_records_srv_index;
    uint work_items_srv_index;
    uint page_table_srv_index;
    uint physical_meta_uav_index;
    uint projection_record_count;
    uint work_item_count;
    uint page_table_entry_count;
    uint physical_page_count;
};

ConstantBuffer<VsmInvalidationPassConstants> GetPassConstants()
{
    ConstantBuffer<VsmInvalidationPassConstants> pass =
        ResourceDescriptorHeap[g_PassConstantsIndex];
    return pass;
}

static bool VsmTryProjectSphereToPageRect(
    const VsmPageRequestProjection projection,
    const float4 world_bounding_sphere,
    out uint2 page_min,
    out uint2 page_max)
{
    page_min = uint2(0u, 0u);
    page_max = uint2(0u, 0u);

    if (projection.map_id == 0u || projection.pages_x == 0u || projection.pages_y == 0u
        || projection.map_pages_x == 0u || projection.map_pages_y == 0u
        || world_bounding_sphere.w <= 0.0f) {
        return false;
    }

    const float3 center_ws = world_bounding_sphere.xyz;
    const float radius = world_bounding_sphere.w;

    float2 ndc_min = float2(1.0e9f, 1.0e9f);
    float2 ndc_max = float2(-1.0e9f, -1.0e9f);
    bool any_valid_point = false;

    const float3 sample_offsets[9] = {
        float3(0.0f, 0.0f, 0.0f),
        float3(-1.0f, -1.0f, -1.0f),
        float3( 1.0f, -1.0f, -1.0f),
        float3(-1.0f,  1.0f, -1.0f),
        float3( 1.0f,  1.0f, -1.0f),
        float3(-1.0f, -1.0f,  1.0f),
        float3( 1.0f, -1.0f,  1.0f),
        float3(-1.0f,  1.0f,  1.0f),
        float3( 1.0f,  1.0f,  1.0f)
    };

    [unroll]
    for (uint i = 0u; i < 9u; ++i) {
        const float3 sample_ws = center_ws + sample_offsets[i] * radius;
        const float4 world = float4(sample_ws, 1.0f);
        const float4 view = mul(projection.projection.view_matrix, world);
        const float4 clip = mul(projection.projection.projection_matrix, view);
        if (abs(clip.w) <= 1.0e-6f || clip.w < 0.0f) {
            continue;
        }

        const float3 ndc = clip.xyz / clip.w;
        ndc_min = min(ndc_min, ndc.xy);
        ndc_max = max(ndc_max, ndc.xy);
        any_valid_point = true;
    }

    if (!any_valid_point) {
        return false;
    }

    if (ndc_max.x < -1.0f || ndc_min.x > 1.0f
        || ndc_max.y < -1.0f || ndc_min.y > 1.0f) {
        return false;
    }

    ndc_min = clamp(ndc_min, -1.0f.xx, 1.0f.xx);
    ndc_max = clamp(ndc_max, -1.0f.xx, 1.0f.xx);

    const float2 uv_min = float2(
        ndc_min.x * 0.5f + 0.5f,
        0.5f - ndc_max.y * 0.5f);
    const float2 uv_max = float2(
        ndc_max.x * 0.5f + 0.5f,
        0.5f - ndc_min.y * 0.5f);

    const uint local_page_min_x = min(
        (uint)floor(saturate(uv_min.x) * (float)projection.pages_x),
        projection.pages_x - 1u);
    const uint local_page_min_y = min(
        (uint)floor(saturate(uv_min.y) * (float)projection.pages_y),
        projection.pages_y - 1u);
    const uint local_page_max_x = min(
        (uint)floor(saturate(uv_max.x) * (float)projection.pages_x),
        projection.pages_x - 1u);
    const uint local_page_max_y = min(
        (uint)floor(saturate(uv_max.y) * (float)projection.pages_y),
        projection.pages_y - 1u);

    page_min = uint2(
        projection.page_offset_x + min(local_page_min_x, local_page_max_x),
        projection.page_offset_y + min(local_page_min_y, local_page_max_y));
    page_max = uint2(
        projection.page_offset_x + max(local_page_min_x, local_page_max_x),
        projection.page_offset_y + max(local_page_min_y, local_page_max_y));

    return true;
}

[numthreads(64, 1, 1)]
void CS(const uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const ConstantBuffer<VsmInvalidationPassConstants> pass = GetPassConstants();
    if (dispatch_thread_id.x >= pass.work_item_count) {
        return;
    }

    StructuredBuffer<VsmPageRequestProjection> projections =
        ResourceDescriptorHeap[pass.projection_records_srv_index];
    StructuredBuffer<VsmShaderInvalidationWorkItem> work_items =
        ResourceDescriptorHeap[pass.work_items_srv_index];
    StructuredBuffer<VsmShaderPageTableEntry> page_table =
        ResourceDescriptorHeap[pass.page_table_srv_index];
    RWByteAddressBuffer physical_meta =
        ResourceDescriptorHeap[pass.physical_meta_uav_index];

    const VsmShaderInvalidationWorkItem work_item = work_items[dispatch_thread_id.x];
    if (work_item.projection_index >= pass.projection_record_count) {
        return;
    }
    const VsmPageRequestProjection projection = projections[work_item.projection_index];

    uint2 page_min = uint2(0u, 0u);
    uint2 page_max = uint2(0u, 0u);
    if (!VsmTryProjectSphereToPageRect(
            projection, work_item.world_bounding_sphere, page_min, page_max)) {
        return;
    }

    const uint pages_per_level = projection.map_pages_x * projection.map_pages_y;
    const uint level_page_offset = projection.projection.clipmap_level * pages_per_level;

    for (uint page_y = page_min.y; page_y <= page_max.y; ++page_y) {
        for (uint page_x = page_min.x; page_x <= page_max.x; ++page_x) {
            const uint page_table_index = projection.first_page_table_entry
                + level_page_offset
                + page_y * projection.map_pages_x
                + page_x;
            if (page_table_index >= pass.page_table_entry_count) {
                continue;
            }

            const VsmShaderPageTableEntry entry = page_table[page_table_index];
            if (!VsmIsPageTableEntryMapped(entry)) {
                continue;
            }

            const uint physical_page_index = VsmDecodePageTablePhysicalPage(entry);
            if (physical_page_index >= pass.physical_page_count) {
                continue;
            }

            const uint base_offset
                = physical_page_index * VSM_PHYSICAL_META_STRIDE_BYTES;
            uint ignored_previous = 0u;

            if (work_item.scope == VSM_INVALIDATION_SCOPE_DYNAMIC_ONLY
                || work_item.scope == VSM_INVALIDATION_SCOPE_STATIC_AND_DYNAMIC) {
                physical_meta.InterlockedOr(
                    base_offset + VSM_PHYSICAL_META_DYNAMIC_INVALIDATED_OFFSET,
                    1u, ignored_previous);
            }

            if (work_item.scope == VSM_INVALIDATION_SCOPE_STATIC_ONLY
                || work_item.scope == VSM_INVALIDATION_SCOPE_STATIC_AND_DYNAMIC) {
                physical_meta.InterlockedOr(
                    base_offset + VSM_PHYSICAL_META_STATIC_INVALIDATED_OFFSET,
                    1u, ignored_previous);
            }
        }
    }
}

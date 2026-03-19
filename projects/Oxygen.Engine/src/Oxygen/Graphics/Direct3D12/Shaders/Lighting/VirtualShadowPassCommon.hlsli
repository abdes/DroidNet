//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_LIGHTING_VIRTUALSHADOWPASSCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_LIGHTING_VIRTUALSHADOWPASSCOMMON_HLSLI

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/DrawHelpers.hlsli"
#include "Renderer/DirectionalVirtualShadowMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/ShadowFrameBindings.hlsli"
#include "Renderer/VirtualShadowPageAccess.hlsli"
#include "Lighting/VirtualShadowPageListHelpers.hlsli"
#include "Lighting/VirtualShadowTypes.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VirtualShadowPassConstants
{
    uint request_words_srv_index;
    uint page_mark_flags_srv_index;
    uint draw_bounds_srv_index;
    uint previous_shadow_caster_bounds_srv_index;
    uint current_shadow_caster_bounds_srv_index;
    uint schedule_uav_index;
    uint schedule_lookup_uav_index;
    uint schedule_count_uav_index;
    uint clear_args_uav_index;
    uint draw_args_uav_index;
    uint draw_page_ranges_uav_index;
    uint draw_page_indices_uav_index;
    uint draw_page_counter_uav_index;
    uint page_table_uav_index;
    uint page_flags_uav_index;
    uint dirty_page_flags_uav_index;
    uint physical_page_metadata_srv_index;
    uint physical_page_metadata_uav_index;
    uint physical_page_lists_srv_index;
    uint physical_page_lists_uav_index;
    uint resolve_stats_uav_index;
    uint _pad0;
    uint _pad1;
    uint _pad2;
    float4x4 current_light_view_matrix;
    float4x4 previous_light_view_matrix;
    uint shadow_caster_bound_count;
    uint request_word_count;
    uint total_page_count;
    uint schedule_capacity;
    uint pages_per_axis;
    uint clip_level_count;
    uint pages_per_level;
    uint physical_page_capacity;
    uint atlas_tiles_per_axis;
    uint draw_count;
    uint draw_page_list_capacity;
    uint reset_page_management_state;
    uint global_dirty_resident_contents;
    uint phase;
    uint target_clip_index;
    int4 clip_grid_origin_x_packed[3];
    int4 clip_grid_origin_y_packed[3];
    float4 clip_origin_x_packed[3];
    float4 clip_origin_y_packed[3];
    float4 clip_page_world_packed[3];
};

static int LoadPackedInt(int4 packed_values[3], uint index)
{
    const uint packed_index = min(index / 4u, 2u);
    const uint lane = index % 4u;
    if (lane == 0u) { return packed_values[packed_index].x; }
    if (lane == 1u) { return packed_values[packed_index].y; }
    if (lane == 2u) { return packed_values[packed_index].z; }
    return packed_values[packed_index].w;
}

static float LoadPackedFloat(float4 packed_values[3], uint index)
{
    const uint packed_index = min(index / 4u, 2u);
    const uint lane = index % 4u;
    if (lane == 0u) { return packed_values[packed_index].x; }
    if (lane == 1u) { return packed_values[packed_index].y; }
    if (lane == 2u) { return packed_values[packed_index].z; }
    return packed_values[packed_index].w;
}

static bool ShadowCasterBoundOverlapsResidentPage(
    VirtualShadowPassConstants pass_constants,
    float4 bound,
    float4x4 light_view_matrix,
    uint clip_index,
    int resident_grid_x,
    int resident_grid_y)
{
    const float radius = max(0.0f, bound.w);
    if (radius <= 0.0f || clip_index >= pass_constants.clip_level_count) {
        return false;
    }

    const float page_world_size =
        max(LoadPackedFloat(pass_constants.clip_page_world_packed, clip_index), 1.0e-4f);
    const float3 center_ls =
        mul(light_view_matrix, float4(bound.xyz, 1.0f)).xyz;
    const int min_grid_x = int(floor((center_ls.x - radius) / page_world_size));
    const int max_grid_x = int(ceil((center_ls.x + radius) / page_world_size) - 1.0f);
    const int min_grid_y = int(floor((center_ls.y - radius) / page_world_size));
    const int max_grid_y = int(ceil((center_ls.y + radius) / page_world_size) - 1.0f);

    return resident_grid_x >= min_grid_x && resident_grid_x <= max_grid_x
        && resident_grid_y >= min_grid_y && resident_grid_y <= max_grid_y;
}

static bool IsPageRequestedThisFrame(
    StructuredBuffer<uint> request_words,
    uint request_word_count,
    uint global_page_index)
{
    const uint word_index = global_page_index / 32u;
    if (word_index >= request_word_count) {
        return false;
    }

    const uint bit_mask = 1u << (global_page_index % 32u);
    return (request_words[word_index] & bit_mask) != 0u;
}

static uint ResolveDirectionalVirtualGuardTexels(
    uint page_size_texels,
    uint filter_radius_texels)
{
    const uint max_guard_texels = max(1u, page_size_texels / 4u);
    return min(max_guard_texels, max(1u, filter_radius_texels));
}

static bool ScheduledPageOverlapsBoundingSphere(
    DirectionalVirtualShadowMetadata metadata,
    uint4 schedule_entry,
    float4 world_bounding_sphere)
{
    if (world_bounding_sphere.w <= 0.0f) {
        return true;
    }

    if (metadata.clip_level_count == 0u || metadata.pages_per_axis == 0u) {
        return false;
    }

    const uint pages_per_level = metadata.pages_per_axis * metadata.pages_per_axis;
    if (pages_per_level == 0u) {
        return false;
    }

    const uint global_page_index = schedule_entry.x;
    const uint clip_index = global_page_index / pages_per_level;
    if (clip_index >= metadata.clip_level_count) {
        return false;
    }

    const uint local_page_index = global_page_index % pages_per_level;
    const uint page_x = local_page_index % metadata.pages_per_axis;
    const uint page_y = local_page_index / metadata.pages_per_axis;
    const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[clip_index];
    const float page_world_size = max(clip.origin_page_scale.z, 1.0e-4);
    const uint filter_guard_texels =
        ResolveDirectionalVirtualGuardTexels(metadata.page_size_texels, 1u);
    const float interior_texels = max(
        1.0,
        float(metadata.page_size_texels) - float(filter_guard_texels * 2u));
    const float page_guard_world =
        page_world_size * (float(filter_guard_texels) / interior_texels);

    const float left =
        clip.origin_page_scale.x + float(page_x) * page_world_size - page_guard_world;
    const float right = left + page_world_size + 2.0 * page_guard_world;
    const float bottom =
        clip.origin_page_scale.y + float(page_y) * page_world_size - page_guard_world;
    const float top = bottom + page_world_size + 2.0 * page_guard_world;

    const float4 center_ls = mul(metadata.light_view, float4(world_bounding_sphere.xyz, 1.0));
    const float radius = world_bounding_sphere.w;
    const float clip_depth = center_ls.z * clip.origin_page_scale.w + clip.bias_reserved.x;
    const float clip_radius_z = abs(clip.origin_page_scale.w) * radius;
    const float clip_padding = 1.0e-3;

    return center_ls.x + radius >= (left - clip_padding)
        && center_ls.x - radius <= (right + clip_padding)
        && center_ls.y + radius >= (bottom - clip_padding)
        && center_ls.y - radius <= (top + clip_padding)
        && clip_depth + clip_radius_z >= (0.0 - clip_padding)
        && clip_depth - clip_radius_z <= (1.0 + clip_padding);
}

static bool DrawBoundingSphereOverlapsClip(
    DirectionalVirtualShadowMetadata metadata,
    float4 world_bounding_sphere,
    uint clip_index,
    float4 center_ls,
    out int min_page_x,
    out int max_page_x,
    out int min_page_y,
    out int max_page_y)
{
    min_page_x = 0;
    max_page_x = -1;
    min_page_y = 0;
    max_page_y = -1;

    if (world_bounding_sphere.w <= 0.0f
        || clip_index >= metadata.clip_level_count
        || metadata.pages_per_axis == 0u) {
        return false;
    }

    const DirectionalVirtualClipMetadata clip = metadata.clip_metadata[clip_index];
    const float radius = world_bounding_sphere.w;
    const float clip_depth = center_ls.z * clip.origin_page_scale.w + clip.bias_reserved.x;
    const float clip_radius_z = abs(clip.origin_page_scale.w) * radius;
    const float clip_padding = 1.0e-3;
    if (clip_depth + clip_radius_z < (0.0 - clip_padding)
        || clip_depth - clip_radius_z > (1.0 + clip_padding)) {
        return false;
    }

    const float page_world_size = max(clip.origin_page_scale.z, 1.0e-4);
    const float sphere_min_x = center_ls.x - radius;
    const float sphere_max_x = center_ls.x + radius;
    const float sphere_min_y = center_ls.y - radius;
    const float sphere_max_y = center_ls.y + radius;

    min_page_x = int(floor((sphere_min_x - clip.origin_page_scale.x) / page_world_size)) - 1;
    max_page_x = int(floor((sphere_max_x - clip.origin_page_scale.x) / page_world_size)) + 1;
    min_page_y = int(floor((sphere_min_y - clip.origin_page_scale.y) / page_world_size)) - 1;
    max_page_y = int(floor((sphere_max_y - clip.origin_page_scale.y) / page_world_size)) + 1;

    const int max_page_coord = int(metadata.pages_per_axis) - 1;
    min_page_x = clamp(min_page_x, 0, max_page_coord);
    max_page_x = clamp(max_page_x, 0, max_page_coord);
    min_page_y = clamp(min_page_y, 0, max_page_coord);
    max_page_y = clamp(max_page_y, 0, max_page_coord);
    return min_page_x <= max_page_x && min_page_y <= max_page_y;
}

#endif

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/DrawHelpers.hlsli"
#include "Renderer/DirectionalVirtualShadowMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/ShadowFrameBindings.hlsli"
#include "Renderer/VirtualShadowPageAccess.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

#ifndef OXYGEN_VSM_RESOLVE_ENTRY_POINT
#define OXYGEN_VSM_RESOLVE_ENTRY_POINT CS
#endif

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VirtualShadowResolvePassConstants
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

struct VirtualShadowPhysicalPageMetadata
{
    uint64_t resident_key;
    uint page_flags;
    uint packed_atlas_tile_coords;
};

struct VirtualShadowPhysicalPageListEntry
{
    uint64_t resident_key;
    uint physical_page_index;
    uint page_flags;
};

struct VirtualShadowResolveStats
{
    uint pending_raster_page_count;
    uint allocated_page_count;
    uint requested_page_list_count;
    uint dirty_page_list_count;
    uint clean_page_list_count;
    uint available_page_list_count;
    uint reserved0;
    uint reserved1;
};

static const uint kResolvePhaseClear = 0u;
static const uint kResolvePhaseMarkDirtyInvalidation = 1u;
static const uint kResolvePhasePopulateCurrent = 2u;
static const uint kResolvePhaseMaterializeRequested = 3u;
static const uint kResolvePhasePopulateFallback = 4u;
static const uint kResolvePhasePropagateHierarchy = 5u;
static const uint kResolvePhaseSchedule = 6u;
static const uint kResolvePhaseBuildClearArgs = 7u;
static const uint kResolvePhaseBuildDrawArgs = 8u;
static const uint kPhysicalPageListRequested = 0u;
static const uint kPhysicalPageListDirty = 1u;
static const uint kPhysicalPageListClean = 2u;
static const uint kPhysicalPageListAvailable = 3u;

static const uint kVirtualResidentPageCoordBits = 28u;
static const uint64_t kVirtualResidentPageCoordMask = (1ull << kVirtualResidentPageCoordBits) - 1ull;
static const uint kVirtualResidentPageCoordSignBit = (1u << (kVirtualResidentPageCoordBits - 1u));
static const uint kPassMaskOpaque = (1u << 2u);
static const uint kPassMaskMasked = (1u << 3u);
static const uint kPassMaskShadowCaster = (1u << 9u);
struct DrawIndirectArgs
{
    uint vertex_count_per_instance;
    uint instance_count;
    uint start_vertex_location;
    uint start_instance_location;
};

struct DrawIndirectCommand
{
    uint draw_index;
    DrawIndirectArgs draw_args;
};

struct DrawPageRange
{
    uint offset;
    uint count;
    uint _pad0;
    uint _pad1;
};

static DrawIndirectArgs MakeZeroDrawIndirectArgs()
{
    DrawIndirectArgs args;
    args.vertex_count_per_instance = 0u;
    args.instance_count = 0u;
    args.start_vertex_location = 0u;
    args.start_instance_location = 0u;
    return args;
}

static DrawIndirectArgs MakeDrawIndirectArgs(
    uint vertex_count_per_instance,
    uint instance_count)
{
    DrawIndirectArgs args;
    args.vertex_count_per_instance = vertex_count_per_instance;
    args.instance_count = instance_count;
    args.start_vertex_location = 0u;
    args.start_instance_location = 0u;
    return args;
}

static DrawIndirectCommand MakeZeroDrawIndirectCommand(uint draw_index)
{
    DrawIndirectCommand command;
    command.draw_index = draw_index;
    command.draw_args = MakeZeroDrawIndirectArgs();
    return command;
}

static DrawIndirectCommand MakeDrawIndirectCommand(
    uint draw_index,
    uint vertex_count_per_instance,
    uint instance_count)
{
    DrawIndirectCommand command;
    command.draw_index = draw_index;
    command.draw_args = MakeDrawIndirectArgs(
        vertex_count_per_instance,
        instance_count);
    return command;
}

static bool DrawIndirectInstanceCountOverflows(
    uint instance_count,
    uint page_count)
{
    if (instance_count == 0u || page_count == 0u) {
        return false;
    }

    const uint alo = instance_count & 0xFFFFu;
    const uint ahi = instance_count >> 16u;
    const uint blo = page_count & 0xFFFFu;
    const uint bhi = page_count >> 16u;

    const uint low_product = alo * blo;
    const uint cross0 = ahi * blo;
    const uint cross1 = alo * bhi;
    const uint high_product = ahi * bhi;

    uint middle = cross0;
    uint carry = 0u;

    middle += cross1;
    if (middle < cross1) {
        carry = 1u;
    }

    const uint low_product_high = low_product >> 16u;
    const uint middle_before_low = middle;
    middle += low_product_high;
    if (middle < middle_before_low) {
        carry = 1u;
    }

    return high_product != 0u
        || carry != 0u
        || (middle >> 16u) != 0u;
}

static DrawPageRange MakeZeroDrawPageRange()
{
    DrawPageRange range;
    range.offset = 0u;
    range.count = 0u;
    range._pad0 = 0u;
    range._pad1 = 0u;
    return range;
}

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

static int DecodeVirtualResidentPageCoord(uint64_t encoded)
{
    uint value = uint(encoded & kVirtualResidentPageCoordMask);
    if ((value & kVirtualResidentPageCoordSignBit) != 0u) {
        value |= uint(~kVirtualResidentPageCoordMask);
    }
    return int(value);
}

static uint DecodeVirtualResidentPageKeyClipLevel(uint64_t resident_key)
{
    return uint(resident_key >> 56ull);
}

static int DecodeVirtualResidentPageKeyGridX(uint64_t resident_key)
{
    return DecodeVirtualResidentPageCoord(resident_key >> 28ull);
}

static int DecodeVirtualResidentPageKeyGridY(uint64_t resident_key)
{
    return DecodeVirtualResidentPageCoord(resident_key);
}

static uint DecodePackedTileX(uint packed_atlas_tile_coords)
{
    return packed_atlas_tile_coords & 0xFFFFu;
}

static uint DecodePackedTileY(uint packed_atlas_tile_coords)
{
    return (packed_atlas_tile_coords >> 16u) & 0xFFFFu;
}

static uint PackAtlasTileCoords(uint tile_x, uint tile_y)
{
    return (tile_x & 0xFFFFu) | ((tile_y & 0xFFFFu) << 16u);
}

static uint PackAtlasTileCoordsFromPhysicalPageIndex(
    VirtualShadowResolvePassConstants pass_constants,
    uint physical_page_index)
{
    const uint atlas_tiles_per_axis = max(pass_constants.atlas_tiles_per_axis, 1u);
    const uint tile_x = physical_page_index % atlas_tiles_per_axis;
    const uint tile_y = physical_page_index / atlas_tiles_per_axis;
    return PackAtlasTileCoords(tile_x, tile_y);
}

static uint64_t EncodeVirtualResidentPageCoord(int value)
{
    return uint64_t(uint(value)) & kVirtualResidentPageCoordMask;
}

static uint64_t PackVirtualResidentPageKey(
    uint clip_level,
    int grid_x,
    int grid_y)
{
    return (uint64_t(clip_level) << 56ull)
        | (EncodeVirtualResidentPageCoord(grid_x) << 28ull)
        | EncodeVirtualResidentPageCoord(grid_y);
}

static bool ShadowCasterBoundOverlapsResidentPage(
    VirtualShadowResolvePassConstants pass_constants,
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

static uint PhysicalPageListStart(
    VirtualShadowResolvePassConstants pass_constants,
    uint list_index)
{
    return list_index * pass_constants.physical_page_capacity;
}

static VirtualShadowPhysicalPageListEntry MakePhysicalPageListEntry(
    uint64_t resident_key,
    uint physical_page_index,
    uint page_flags)
{
    VirtualShadowPhysicalPageListEntry entry;
    entry.resident_key = resident_key;
    entry.physical_page_index = physical_page_index;
    entry.page_flags = page_flags;
    return entry;
}

static uint SelectDirectionalVirtualFilterRadiusTexels(
    DirectionalVirtualShadowMetadata metadata,
    uint clip_index)
{
    const float base_page_world =
        max(metadata.clip_metadata[0].origin_page_scale.z, 1.0e-4);
    const float clip_page_world =
        max(metadata.clip_metadata[clip_index].origin_page_scale.z, base_page_world);
    const float texel_ratio = clip_page_world / base_page_world;
    return texel_ratio > 2.5 ? 2u : 1u;
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
    const uint filter_guard_texels = ResolveDirectionalVirtualGuardTexels(
        metadata.page_size_texels,
        SelectDirectionalVirtualFilterRadiusTexels(metadata, clip_index));
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

static void VirtualShadowResolveCSImpl(uint3 dispatch_thread_id)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VirtualShadowResolvePassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];

    if (!BX_IN_GLOBAL_SRV(pass_constants.dirty_page_flags_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_metadata_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_metadata_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_lists_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_lists_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.resolve_stats_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.page_table_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.page_flags_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_lookup_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_count_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.clear_args_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.draw_args_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.draw_page_ranges_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.draw_page_indices_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.draw_page_counter_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_uav_index];
    RWStructuredBuffer<uint> page_flags =
        ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    RWStructuredBuffer<uint> schedule_lookup =
        ResourceDescriptorHeap[pass_constants.schedule_lookup_uav_index];
    RWStructuredBuffer<uint> dirty_page_flags =
        ResourceDescriptorHeap[pass_constants.dirty_page_flags_uav_index];
    StructuredBuffer<VirtualShadowPhysicalPageMetadata> physical_page_metadata =
        ResourceDescriptorHeap[pass_constants.physical_page_metadata_srv_index];
    RWStructuredBuffer<VirtualShadowPhysicalPageMetadata> physical_page_metadata_uav =
        ResourceDescriptorHeap[pass_constants.physical_page_metadata_uav_index];
    StructuredBuffer<VirtualShadowPhysicalPageListEntry> physical_page_lists =
        ResourceDescriptorHeap[pass_constants.physical_page_lists_srv_index];
    RWStructuredBuffer<VirtualShadowPhysicalPageListEntry> physical_page_lists_uav =
        ResourceDescriptorHeap[pass_constants.physical_page_lists_uav_index];
    const bool has_shadow_caster_bounds =
        pass_constants.shadow_caster_bound_count > 0u
        && BX_IN_GLOBAL_SRV(pass_constants.previous_shadow_caster_bounds_srv_index)
        && BX_IN_GLOBAL_SRV(pass_constants.current_shadow_caster_bounds_srv_index);
    RWStructuredBuffer<VirtualShadowResolveStats> resolve_stats =
        ResourceDescriptorHeap[pass_constants.resolve_stats_uav_index];
    RWStructuredBuffer<uint4> schedule =
        ResourceDescriptorHeap[pass_constants.schedule_uav_index];
    RWStructuredBuffer<uint> schedule_count =
        ResourceDescriptorHeap[pass_constants.schedule_count_uav_index];
    RWStructuredBuffer<DrawIndirectArgs> clear_args =
        ResourceDescriptorHeap[pass_constants.clear_args_uav_index];
    RWStructuredBuffer<DrawIndirectCommand> draw_args =
        ResourceDescriptorHeap[pass_constants.draw_args_uav_index];
    RWStructuredBuffer<DrawPageRange> draw_page_ranges =
        ResourceDescriptorHeap[pass_constants.draw_page_ranges_uav_index];
    RWStructuredBuffer<uint> draw_page_indices =
        ResourceDescriptorHeap[pass_constants.draw_page_indices_uav_index];
    RWStructuredBuffer<uint> draw_page_counter =
        ResourceDescriptorHeap[pass_constants.draw_page_counter_uav_index];
    const bool has_page_mark_flags =
        BX_IN_GLOBAL_SRV(pass_constants.page_mark_flags_srv_index);

    const uint thread_index = dispatch_thread_id.x;

#if defined(OXYGEN_VSM_RESOLVE_PAGE_MANAGEMENT_ONLY)
    if (pass_constants.phase > kResolvePhaseSchedule) {
        return;
    }
#elif defined(OXYGEN_VSM_RESOLVE_BUILD_CLEAR_ARGS_ONLY)
    if (pass_constants.phase != kResolvePhaseBuildClearArgs) {
        return;
    }
#elif defined(OXYGEN_VSM_RESOLVE_BUILD_DRAW_ARGS_ONLY)
    if (pass_constants.phase != kResolvePhaseBuildDrawArgs) {
        return;
    }
#endif

    if (pass_constants.phase == kResolvePhaseClear) {
        if (thread_index < pass_constants.total_page_count) {
            page_table[thread_index] = 0u;
            page_flags[thread_index] = 0u;
        }
        if (thread_index < pass_constants.physical_page_capacity * 3u) {
            dirty_page_flags[thread_index] = 0u;
        }
        if (pass_constants.reset_page_management_state != 0u
            && thread_index < pass_constants.physical_page_capacity) {
            const uint packed_tile_coords =
                PackAtlasTileCoordsFromPhysicalPageIndex(pass_constants, thread_index);
            physical_page_metadata_uav[thread_index].resident_key = 0ull;
            physical_page_metadata_uav[thread_index].page_flags = 0u;
            physical_page_metadata_uav[thread_index].packed_atlas_tile_coords =
                packed_tile_coords;
        }
        if (thread_index == 0u) {
            resolve_stats[0].pending_raster_page_count = 0u;
            resolve_stats[0].requested_page_list_count = 0u;
            resolve_stats[0].dirty_page_list_count = 0u;
            resolve_stats[0].clean_page_list_count = 0u;
            resolve_stats[0].available_page_list_count = 0u;
            resolve_stats[0].allocated_page_count = 0u;
            resolve_stats[0].reserved0 = 0u;
            resolve_stats[0].reserved1 = 0u;
        }
        return;
    }

    if (pass_constants.phase == kResolvePhaseMarkDirtyInvalidation) {
        if (thread_index >= pass_constants.physical_page_capacity) {
            return;
        }

        if (pass_constants.global_dirty_resident_contents != 0u) {
            dirty_page_flags[thread_index] = 1u;
            dirty_page_flags[thread_index + pass_constants.physical_page_capacity] = 1u;
            dirty_page_flags[thread_index + 2u * pass_constants.physical_page_capacity] = 1u;
            return;
        }

        if (!has_shadow_caster_bounds) {
            return;
        }

        const VirtualShadowPhysicalPageMetadata metadata =
            physical_page_metadata[thread_index];
        if (!VirtualShadowPageHasFlag(
                metadata.page_flags, OXYGEN_VSM_PAGE_FLAG_ALLOCATED)) {
            return;
        }

        const uint64_t resident_key = metadata.resident_key;
        const uint clip_index = DecodeVirtualResidentPageKeyClipLevel(resident_key);
        const int grid_x = DecodeVirtualResidentPageKeyGridX(resident_key);
        const int grid_y = DecodeVirtualResidentPageKeyGridY(resident_key);
        StructuredBuffer<float4> previous_shadow_caster_bounds =
            ResourceDescriptorHeap[pass_constants.previous_shadow_caster_bounds_srv_index];
        StructuredBuffer<float4> current_shadow_caster_bounds =
            ResourceDescriptorHeap[pass_constants.current_shadow_caster_bounds_srv_index];

        for (uint bound_index = 0u;
             bound_index < pass_constants.shadow_caster_bound_count;
             ++bound_index) {
            const float4 previous_bound = previous_shadow_caster_bounds[bound_index];
            const float4 current_bound = current_shadow_caster_bounds[bound_index];
            if (all(previous_bound == current_bound)) {
                continue;
            }

            const bool overlaps_previous =
                ShadowCasterBoundOverlapsResidentPage(
                    pass_constants,
                    previous_bound,
                    pass_constants.previous_light_view_matrix,
                    clip_index,
                    grid_x,
                    grid_y);
            const bool overlaps_current =
                ShadowCasterBoundOverlapsResidentPage(
                    pass_constants,
                    current_bound,
                    pass_constants.current_light_view_matrix,
                    clip_index,
                    grid_x,
                    grid_y);
            if (!overlaps_previous && !overlaps_current) {
                continue;
            }

            dirty_page_flags[thread_index] = 1u;
            dirty_page_flags[thread_index + pass_constants.physical_page_capacity] = 1u;
            dirty_page_flags[thread_index + 2u * pass_constants.physical_page_capacity] = 1u;
            break;
        }
        return;
    }

    if (pass_constants.phase == kResolvePhasePopulateCurrent) {
        if (thread_index >= pass_constants.physical_page_capacity) {
            return;
        }

        const uint requested_list_start =
            PhysicalPageListStart(pass_constants, kPhysicalPageListRequested);
        const uint dirty_list_start =
            PhysicalPageListStart(pass_constants, kPhysicalPageListDirty);
        const uint clean_list_start =
            PhysicalPageListStart(pass_constants, kPhysicalPageListClean);
        const uint available_list_start =
            PhysicalPageListStart(pass_constants, kPhysicalPageListAvailable);

        VirtualShadowPhysicalPageMetadata metadata = physical_page_metadata[thread_index];
        const uint packed_tile_coords =
            PackAtlasTileCoordsFromPhysicalPageIndex(pass_constants, thread_index);
        const bool allocated =
            VirtualShadowPageHasFlag(metadata.page_flags, OXYGEN_VSM_PAGE_FLAG_ALLOCATED);
        if (!allocated) {
            uint available_index = 0u;
            InterlockedAdd(resolve_stats[0].available_page_list_count, 1u, available_index);
            if (available_index < pass_constants.physical_page_capacity) {
                physical_page_lists_uav[available_list_start + available_index] =
                    MakePhysicalPageListEntry(0ull, thread_index, 0u);
            }
            return;
        }

        const uint64_t resident_key = metadata.resident_key;
        const uint clip_index = DecodeVirtualResidentPageKeyClipLevel(resident_key);
        if (clip_index >= pass_constants.clip_level_count) {
            metadata.resident_key = 0ull;
            metadata.page_flags = 0u;
            metadata.packed_atlas_tile_coords = packed_tile_coords;
            physical_page_metadata_uav[thread_index] = metadata;
            uint available_index = 0u;
            InterlockedAdd(resolve_stats[0].available_page_list_count, 1u, available_index);
            if (available_index < pass_constants.physical_page_capacity) {
                physical_page_lists_uav[available_list_start + available_index] =
                    MakePhysicalPageListEntry(0ull, thread_index, 0u);
            }
            return;
        }

        const int local_page_x = DecodeVirtualResidentPageKeyGridX(resident_key)
            - LoadPackedInt(pass_constants.clip_grid_origin_x_packed, clip_index);
        const int local_page_y = DecodeVirtualResidentPageKeyGridY(resident_key)
            - LoadPackedInt(pass_constants.clip_grid_origin_y_packed, clip_index);
        if (local_page_x < 0 || local_page_y < 0
            || local_page_x >= int(pass_constants.pages_per_axis)
            || local_page_y >= int(pass_constants.pages_per_axis)) {
            metadata.resident_key = 0ull;
            metadata.page_flags = 0u;
            metadata.packed_atlas_tile_coords = packed_tile_coords;
            physical_page_metadata_uav[thread_index] = metadata;
            uint available_index = 0u;
            InterlockedAdd(resolve_stats[0].available_page_list_count, 1u, available_index);
            if (available_index < pass_constants.physical_page_capacity) {
                physical_page_lists_uav[available_list_start + available_index] =
                    MakePhysicalPageListEntry(0ull, thread_index, 0u);
            }
            return;
        }

        const uint local_page_index
            = uint(local_page_y) * pass_constants.pages_per_axis + uint(local_page_x);
        const uint global_page_index = clip_index * pass_constants.pages_per_level + local_page_index;
        if (global_page_index >= pass_constants.total_page_count) {
            return;
        }

        uint mark_flags = 0u;
        if (has_page_mark_flags) {
            StructuredBuffer<uint> page_mark_flags =
                ResourceDescriptorHeap[pass_constants.page_mark_flags_srv_index];
            mark_flags = page_mark_flags[global_page_index];
        }
        const bool dynamic_uncached =
            dirty_page_flags[thread_index + pass_constants.physical_page_capacity] != 0u;
        const bool static_uncached =
            dirty_page_flags[thread_index + 2u * pass_constants.physical_page_capacity] != 0u;
        const bool detail_geometry =
            VirtualShadowPageHasFlag(mark_flags, OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY);
        const bool used_this_frame =
            VirtualShadowPageHasFlag(mark_flags, OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME);
        const uint published_flags = OXYGEN_VSM_PAGE_FLAG_ALLOCATED
            | (dynamic_uncached ? OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED : 0u)
            | (static_uncached ? OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED : 0u)
            | (detail_geometry ? OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY : 0u)
            | (used_this_frame ? OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME : 0u);
        page_table[global_page_index] = PackVirtualShadowPageTableEntry(
            DecodePackedTileX(packed_tile_coords),
            DecodePackedTileY(packed_tile_coords),
            0u,
            true,
            true,
            used_this_frame);
        page_flags[global_page_index] = published_flags;
        metadata.page_flags = published_flags;
        metadata.packed_atlas_tile_coords = packed_tile_coords;
        physical_page_metadata_uav[thread_index] = metadata;

        uint list_index = 0u;
        uint list_start = clean_list_start;
        if (used_this_frame) {
            InterlockedAdd(resolve_stats[0].requested_page_list_count, 1u, list_index);
            list_start = requested_list_start;
        } else if (dynamic_uncached || static_uncached) {
            InterlockedAdd(resolve_stats[0].dirty_page_list_count, 1u, list_index);
            list_start = dirty_list_start;
        } else {
            InterlockedAdd(resolve_stats[0].clean_page_list_count, 1u, list_index);
            list_start = clean_list_start;
        }
        if (list_index < pass_constants.physical_page_capacity) {
            physical_page_lists_uav[list_start + list_index] =
                MakePhysicalPageListEntry(resident_key, thread_index, published_flags);
        }
        return;
    }

    if (pass_constants.phase == kResolvePhaseMaterializeRequested) {
        if (thread_index >= pass_constants.total_page_count) {
            return;
        }

        const bool has_request_words =
            pass_constants.request_word_count > 0u
            && BX_IN_GLOBAL_SRV(pass_constants.request_words_srv_index);
        bool requested_this_frame = false;
        if (has_request_words) {
            StructuredBuffer<uint> request_words =
                ResourceDescriptorHeap[pass_constants.request_words_srv_index];
            requested_this_frame = IsPageRequestedThisFrame(
                request_words, pass_constants.request_word_count, thread_index);
        }

        uint mark_flags = 0u;
        if (has_page_mark_flags) {
            StructuredBuffer<uint> page_mark_flags =
                ResourceDescriptorHeap[pass_constants.page_mark_flags_srv_index];
            mark_flags = page_mark_flags[thread_index];
            requested_this_frame = requested_this_frame
                || VirtualShadowPageHasFlag(
                    mark_flags, OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME);
        }

        // UE-style VSM demand is authored as current-frame per-page request
        // flags. The request-word bitset is only an auxiliary compact view.
        if (!requested_this_frame) {
            return;
        }

        const VirtualShadowPageTableEntry existing_entry =
            DecodeVirtualShadowPageTableEntry(page_table[thread_index]);
        if (VirtualShadowPageTableEntryHasCurrentLod(existing_entry)) {
            return;
        }

        const uint clip_index = thread_index / pass_constants.pages_per_level;
        if (clip_index >= pass_constants.clip_level_count) {
            return;
        }

        const uint local_page_index = thread_index % pass_constants.pages_per_level;
        const uint page_x = local_page_index % pass_constants.pages_per_axis;
        const uint page_y = local_page_index / pass_constants.pages_per_axis;
        const int grid_x =
            LoadPackedInt(pass_constants.clip_grid_origin_x_packed, clip_index)
            + int(page_x);
        const int grid_y =
            LoadPackedInt(pass_constants.clip_grid_origin_y_packed, clip_index)
            + int(page_y);

        const uint available_page_count = resolve_stats[0].available_page_list_count;
        if (available_page_count == 0u) {
            return;
        }

        uint allocation_ordinal = 0u;
        InterlockedAdd(
            resolve_stats[0].allocated_page_count,
            1u,
            allocation_ordinal);
        if (allocation_ordinal >= available_page_count) {
            return;
        }

        const uint available_list_start =
            PhysicalPageListStart(pass_constants, kPhysicalPageListAvailable);
        const uint available_list_index =
            available_list_start + (available_page_count - 1u - allocation_ordinal);
        const VirtualShadowPhysicalPageListEntry available_entry =
            physical_page_lists[available_list_index];
        const uint physical_page_index = available_entry.physical_page_index;
        if (physical_page_index == 0xFFFFFFFFu
            || physical_page_index >= pass_constants.physical_page_capacity) {
            return;
        }

        const uint published_flags =
            OXYGEN_VSM_PAGE_FLAG_ALLOCATED
            | OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED
            | OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED
            | (mark_flags & (OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY
                | OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME));
        const uint packed_tile_coords =
            PackAtlasTileCoordsFromPhysicalPageIndex(pass_constants, physical_page_index);

        physical_page_metadata_uav[physical_page_index].resident_key =
            PackVirtualResidentPageKey(clip_index, grid_x, grid_y);
        physical_page_metadata_uav[physical_page_index].page_flags = published_flags;
        physical_page_metadata_uav[physical_page_index].packed_atlas_tile_coords =
            packed_tile_coords;

        page_table[thread_index] = PackVirtualShadowPageTableEntry(
            DecodePackedTileX(packed_tile_coords),
            DecodePackedTileY(packed_tile_coords),
            0u,
            true,
            true,
            true);
        page_flags[thread_index] = published_flags;
        uint requested_list_index = 0u;
        InterlockedAdd(resolve_stats[0].requested_page_list_count, 1u, requested_list_index);
        if (requested_list_index < pass_constants.physical_page_capacity) {
            const uint requested_list_start =
                PhysicalPageListStart(pass_constants, kPhysicalPageListRequested);
            physical_page_lists_uav[requested_list_start + requested_list_index] =
                MakePhysicalPageListEntry(
                    PackVirtualResidentPageKey(clip_index, grid_x, grid_y),
                    physical_page_index,
                    published_flags);
        }
        return;
    }

    if (pass_constants.phase == kResolvePhasePopulateFallback) {
        if (thread_index >= pass_constants.pages_per_level
            || pass_constants.pages_per_level == 0u
            || pass_constants.pages_per_axis == 0u) {
            return;
        }

        const uint clip_index = pass_constants.target_clip_index;
        if (clip_index + 1u >= pass_constants.clip_level_count) {
            return;
        }

        const uint global_page_index = clip_index * pass_constants.pages_per_level + thread_index;
        if (global_page_index >= pass_constants.total_page_count) {
            return;
        }

        const uint current_packed_entry = page_table[global_page_index];
        if (VirtualShadowPageTableEntryHasCurrentLod(DecodeVirtualShadowPageTableEntry(current_packed_entry))) {
            return;
        }

        const uint local_page_index = thread_index;
        const uint page_x = local_page_index % pass_constants.pages_per_axis;
        const uint page_y = local_page_index / pass_constants.pages_per_axis;

        const float clip_page_world = LoadPackedFloat(pass_constants.clip_page_world_packed, clip_index);
        const float clip_origin_x = LoadPackedFloat(pass_constants.clip_origin_x_packed, clip_index);
        const float clip_origin_y = LoadPackedFloat(pass_constants.clip_origin_y_packed, clip_index);
        const float page_center_x = clip_origin_x + (float(page_x) + 0.5f) * clip_page_world;
        const float page_center_y = clip_origin_y + (float(page_y) + 0.5f) * clip_page_world;

        for (uint candidate_clip = clip_index + 1u;
             candidate_clip < pass_constants.clip_level_count;
             ++candidate_clip) {
            const float candidate_page_world =
                LoadPackedFloat(pass_constants.clip_page_world_packed, candidate_clip);
            const float candidate_page_x_f =
                (page_center_x - LoadPackedFloat(pass_constants.clip_origin_x_packed, candidate_clip))
                / candidate_page_world;
            const float candidate_page_y_f =
                (page_center_y - LoadPackedFloat(pass_constants.clip_origin_y_packed, candidate_clip))
                / candidate_page_world;
            if (candidate_page_x_f < 0.0f || candidate_page_y_f < 0.0f
                || candidate_page_x_f >= float(pass_constants.pages_per_axis)
                || candidate_page_y_f >= float(pass_constants.pages_per_axis)) {
                continue;
            }

            const uint candidate_page_x = min(
                pass_constants.pages_per_axis - 1u,
                uint(max(0.0f, floor(candidate_page_x_f))));
            const uint candidate_page_y = min(
                pass_constants.pages_per_axis - 1u,
                uint(max(0.0f, floor(candidate_page_y_f))));
            const uint candidate_global_page_index =
                candidate_clip * pass_constants.pages_per_level
                + candidate_page_y * pass_constants.pages_per_axis
                + candidate_page_x;
            if (candidate_global_page_index >= pass_constants.total_page_count) {
                continue;
            }

            const uint candidate_packed_entry = page_table[candidate_global_page_index];
            const VirtualShadowPageTableEntry candidate_entry =
                DecodeVirtualShadowPageTableEntry(candidate_packed_entry);
            if (!VirtualShadowPageTableEntryHasAnyLod(candidate_entry)) {
                continue;
            }

            const uint resolved_fallback_clip = ResolveVirtualShadowFallbackClipIndex(
                candidate_clip, pass_constants.clip_level_count, candidate_entry);
            if (resolved_fallback_clip <= clip_index
                || resolved_fallback_clip >= pass_constants.clip_level_count) {
                continue;
            }

            // Phase 6 guardrail: coarse fallback must be emitted from actual
            // mapped coarse pages in page management, not from later
            // hierarchy-policy recovery in the sampling path.
            if (!VirtualShadowPageTableEntryHasCurrentLod(candidate_entry)) {
                continue;
            }

            page_table[global_page_index] = PackVirtualShadowPageTableEntry(
                candidate_entry.tile_x,
                candidate_entry.tile_y,
                resolved_fallback_clip - clip_index,
                false,
                true,
                false);
            return;
        }
        return;
    }

    if (pass_constants.phase == kResolvePhasePropagateHierarchy) {
        if (thread_index >= pass_constants.pages_per_level
            || pass_constants.pages_per_level == 0u
            || pass_constants.pages_per_axis == 0u) {
            return;
        }

        const uint fine_clip = pass_constants.target_clip_index;
        if (fine_clip + 1u >= pass_constants.clip_level_count) {
            return;
        }

        const uint fine_local_page_index = thread_index;
        const uint fine_page_x = fine_local_page_index % pass_constants.pages_per_axis;
        const uint fine_page_y = fine_local_page_index / pass_constants.pages_per_axis;
        const uint fine_global_page_index =
            fine_clip * pass_constants.pages_per_level + fine_local_page_index;
        if (fine_global_page_index >= pass_constants.total_page_count) {
            return;
        }

        const uint fine_flags = page_flags[fine_global_page_index];
        if (fine_flags == 0u) {
            return;
        }

        const uint parent_clip = fine_clip + 1u;
        const float fine_page_world = LoadPackedFloat(pass_constants.clip_page_world_packed, fine_clip);
        const float fine_origin_x = LoadPackedFloat(pass_constants.clip_origin_x_packed, fine_clip);
        const float fine_origin_y = LoadPackedFloat(pass_constants.clip_origin_y_packed, fine_clip);
        const float parent_page_world = LoadPackedFloat(pass_constants.clip_page_world_packed, parent_clip);
        const float parent_origin_x = LoadPackedFloat(pass_constants.clip_origin_x_packed, parent_clip);
        const float parent_origin_y = LoadPackedFloat(pass_constants.clip_origin_y_packed, parent_clip);

        const float page_center_x = fine_origin_x + (float(fine_page_x) + 0.5f) * fine_page_world;
        const float page_center_y = fine_origin_y + (float(fine_page_y) + 0.5f) * fine_page_world;
        const float parent_page_x_f = (page_center_x - parent_origin_x) / parent_page_world;
        const float parent_page_y_f = (page_center_y - parent_origin_y) / parent_page_world;
        if (parent_page_x_f < 0.0f || parent_page_y_f < 0.0f
            || parent_page_x_f >= float(pass_constants.pages_per_axis)
            || parent_page_y_f >= float(pass_constants.pages_per_axis)) {
            return;
        }

        const uint parent_page_x = uint(floor(parent_page_x_f));
        const uint parent_page_y = uint(floor(parent_page_y_f));
        const uint parent_global_page_index =
            parent_clip * pass_constants.pages_per_level
            + parent_page_y * pass_constants.pages_per_axis
            + parent_page_x;
        if (parent_global_page_index >= pass_constants.total_page_count) {
            return;
        }

        const VirtualShadowPageTableEntry parent_entry =
            DecodeVirtualShadowPageTableEntry(page_table[parent_global_page_index]);
        if (!VirtualShadowPageTableEntryHasCurrentLod(parent_entry)) {
            return;
        }

        InterlockedOr(
            page_flags[parent_global_page_index],
            MakeVirtualShadowHierarchyFlags(fine_flags));
        return;
    }

    if (pass_constants.phase == kResolvePhaseSchedule) {
        if (thread_index >= pass_constants.total_page_count) {
            return;
        }

        schedule_lookup[thread_index] = 0xFFFFFFFFu;

        const uint packed_entry = page_table[thread_index];
        const uint published_flags = page_flags[thread_index];
        const uint uncached_flags =
            OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED | OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED;
        if ((packed_entry & OXYGEN_VSM_PAGE_TABLE_CURRENT_LOD_VALID_BIT) != 0u
            && (published_flags & uncached_flags) != 0u) {
            uint output_index = 0u;
            InterlockedAdd(resolve_stats[0].pending_raster_page_count, 1u, output_index);
            InterlockedAdd(schedule_count[0], 1u, output_index);
            if (output_index < pass_constants.schedule_capacity) {
                const uint tile_x = packed_entry & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK;
                const uint tile_y =
                    (packed_entry >> OXYGEN_VSM_PAGE_TABLE_TILE_Y_SHIFT)
                    & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK;
                schedule[output_index] = uint4(thread_index, packed_entry, tile_x, tile_y);
                schedule_lookup[thread_index] = output_index;
            }
        }
        return;
    }

    if (pass_constants.phase == kResolvePhaseBuildClearArgs) {
        if (thread_index != 0u) {
            return;
        }

        draw_page_counter[0] = 0u;
        clear_args[0] = MakeDrawIndirectArgs(6u, schedule_count[0]);
        return;
    }

    if (pass_constants.phase == kResolvePhaseBuildDrawArgs) {
        if (thread_index >= pass_constants.draw_count) {
            return;
        }

        const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();
        if (!BX_IN_GLOBAL_SRV(draw_bindings.draw_metadata_slot)) {
            draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
            draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
            return;
        }

        StructuredBuffer<DrawMetadata> draw_metadata =
            ResourceDescriptorHeap[draw_bindings.draw_metadata_slot];
        const DrawMetadata meta = draw_metadata[thread_index];
        const bool has_draw_bounds = BX_IN_GLOBAL_SRV(pass_constants.draw_bounds_srv_index);
        float4 draw_bound = float4(0.0, 0.0, 0.0, 0.0);
        if (has_draw_bounds) {
            StructuredBuffer<float4> draw_bounds =
                ResourceDescriptorHeap[pass_constants.draw_bounds_srv_index];
            draw_bound = draw_bounds[thread_index];
        }
        const ViewFrameBindings view_bindings =
            LoadViewFrameBindings(bindless_view_frame_bindings_slot);
        const ShadowFrameBindings shadow_bindings =
            LoadShadowFrameBindings(view_bindings.shadow_frame_slot);
        if (!BX_IN_GLOBAL_SRV(shadow_bindings.virtual_directional_shadow_metadata_slot)) {
            draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
            draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
            return;
        }
        StructuredBuffer<DirectionalVirtualShadowMetadata> metadata_buffer =
            ResourceDescriptorHeap[shadow_bindings.virtual_directional_shadow_metadata_slot];
        const DirectionalVirtualShadowMetadata directional_metadata = metadata_buffer[0];
        const bool is_shadow_caster = (meta.flags & kPassMaskShadowCaster) != 0u;
        const bool is_shadow_surface =
            (meta.flags & (kPassMaskOpaque | kPassMaskMasked)) != 0u;
        const uint vertex_count = meta.is_indexed != 0u ? meta.index_count : meta.vertex_count;
        const uint scheduled_page_count = schedule_count[0];
        const bool invalid_draw = vertex_count == 0u || meta.instance_count == 0u;

        if (!is_shadow_caster || !is_shadow_surface || invalid_draw) {
            draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
            draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
            return;
        }

        uint overlapping_page_count = 0u;
        if (!has_draw_bounds || draw_bound.w <= 0.0f) {
            for (uint scheduled_index = 0u;
                 scheduled_index < scheduled_page_count;
                 ++scheduled_index) {
                if (ScheduledPageOverlapsBoundingSphere(
                        directional_metadata,
                        schedule[scheduled_index],
                        draw_bound)) {
                    ++overlapping_page_count;
                }
            }
        } else {
            const float4 center_ls =
                mul(directional_metadata.light_view, float4(draw_bound.xyz, 1.0f));
            for (uint clip_index = 0u;
                 clip_index < directional_metadata.clip_level_count;
                 ++clip_index) {
                int min_page_x = 0;
                int max_page_x = -1;
                int min_page_y = 0;
                int max_page_y = -1;
                if (!DrawBoundingSphereOverlapsClip(
                        directional_metadata,
                        draw_bound,
                        clip_index,
                        center_ls,
                        min_page_x,
                        max_page_x,
                        min_page_y,
                        max_page_y)) {
                    continue;
                }

                for (int page_y = min_page_y; page_y <= max_page_y; ++page_y) {
                    for (int page_x = min_page_x; page_x <= max_page_x; ++page_x) {
                        const uint global_page_index =
                            clip_index * pass_constants.pages_per_level
                            + uint(page_y) * pass_constants.pages_per_axis
                            + uint(page_x);
                        const uint scheduled_index = schedule_lookup[global_page_index];
                        if (scheduled_index == 0xFFFFFFFFu
                            || scheduled_index >= scheduled_page_count) {
                            continue;
                        }
                        if (ScheduledPageOverlapsBoundingSphere(
                                directional_metadata,
                                schedule[scheduled_index],
                                draw_bound)) {
                            ++overlapping_page_count;
                        }
                    }
                }
            }
        }

        if (overlapping_page_count == 0u) {
            draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
            draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
            return;
        }

        uint range_offset = 0u;
        InterlockedAdd(draw_page_counter[0], overlapping_page_count, range_offset);

        uint writable_count = 0u;
        if (range_offset < pass_constants.draw_page_list_capacity) {
            writable_count = min(
                overlapping_page_count,
                pass_constants.draw_page_list_capacity - range_offset);
        }

        uint local_write_index = 0u;
        if (!has_draw_bounds || draw_bound.w <= 0.0f) {
            for (uint scheduled_index = 0u;
                 scheduled_index < scheduled_page_count && local_write_index < writable_count;
                 ++scheduled_index) {
                if (ScheduledPageOverlapsBoundingSphere(
                        directional_metadata,
                        schedule[scheduled_index],
                        draw_bound)) {
                    draw_page_indices[range_offset + local_write_index] = scheduled_index;
                    ++local_write_index;
                }
            }
        } else {
            const float4 center_ls =
                mul(directional_metadata.light_view, float4(draw_bound.xyz, 1.0f));
            for (uint clip_index = 0u;
                 clip_index < directional_metadata.clip_level_count
                 && local_write_index < writable_count;
                 ++clip_index) {
                int min_page_x = 0;
                int max_page_x = -1;
                int min_page_y = 0;
                int max_page_y = -1;
                if (!DrawBoundingSphereOverlapsClip(
                        directional_metadata,
                        draw_bound,
                        clip_index,
                        center_ls,
                        min_page_x,
                        max_page_x,
                        min_page_y,
                        max_page_y)) {
                    continue;
                }

                for (int page_y = min_page_y;
                     page_y <= max_page_y && local_write_index < writable_count;
                     ++page_y) {
                    for (int page_x = min_page_x;
                         page_x <= max_page_x && local_write_index < writable_count;
                         ++page_x) {
                        const uint global_page_index =
                            clip_index * pass_constants.pages_per_level
                            + uint(page_y) * pass_constants.pages_per_axis
                            + uint(page_x);
                        const uint scheduled_index = schedule_lookup[global_page_index];
                        if (scheduled_index == 0xFFFFFFFFu
                            || scheduled_index >= scheduled_page_count) {
                            continue;
                        }
                        if (ScheduledPageOverlapsBoundingSphere(
                                directional_metadata,
                                schedule[scheduled_index],
                                draw_bound)) {
                            draw_page_indices[range_offset + local_write_index] = scheduled_index;
                            ++local_write_index;
                        }
                    }
                }
            }
        }

        const bool instance_overflow = DrawIndirectInstanceCountOverflows(
            meta.instance_count, writable_count);
        if (instance_overflow || writable_count == 0u) {
            draw_args[thread_index] = MakeZeroDrawIndirectCommand(thread_index);
            draw_page_ranges[thread_index] = MakeZeroDrawPageRange();
            return;
        }

        DrawPageRange range;
        range.offset = range_offset;
        range.count = writable_count;
        range._pad0 = 0u;
        range._pad1 = 0u;
        draw_page_ranges[thread_index] = range;
        draw_args[thread_index] = MakeDrawIndirectCommand(
            thread_index,
            vertex_count,
            meta.instance_count * writable_count);
        return;
    }
}

[shader("compute")]
[numthreads(64, 1, 1)]
void OXYGEN_VSM_RESOLVE_ENTRY_POINT(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    VirtualShadowResolveCSImpl(dispatch_thread_id);
}

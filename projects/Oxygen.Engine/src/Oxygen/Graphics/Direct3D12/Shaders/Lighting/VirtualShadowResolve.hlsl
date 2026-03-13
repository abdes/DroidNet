//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"
#include "Renderer/VirtualShadowPageAccess.hlsli"

#define BX_VERTEX_TYPE uint4
#include "Core/Bindless/BindlessHelpers.hlsl"

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VirtualShadowResolvePassConstants
{
    uint request_words_srv_index;
    uint schedule_uav_index;
    uint schedule_count_uav_index;
    uint page_table_srv_index;
    uint page_table_uav_index;
    uint page_flags_uav_index;
    uint physical_page_metadata_srv_index;
    uint physical_page_lists_srv_index;
    uint resolve_stats_srv_index;
    uint request_word_count;
    uint total_page_count;
    uint schedule_capacity;
    uint pages_per_axis;
    uint clip_level_count;
    uint pages_per_level;
    uint requested_page_list_count;
    uint dirty_page_list_count;
    uint clean_page_list_count;
    uint total_page_management_list_count;
    uint phase;
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
    uint resident_entry_count;
    uint resident_entry_capacity;
    uint clean_page_count;
    uint dirty_page_count;
    uint pending_page_count;
    uint mapped_page_count;
    uint pending_raster_page_count;
    uint selected_page_count;
    uint allocated_page_count;
    uint evicted_page_count;
    uint rerasterized_page_count;
    uint reused_requested_page_count;
    uint requested_page_list_count;
    uint dirty_page_list_count;
    uint clean_page_list_count;
    uint available_page_list_count;
};

static const uint kResolvePhaseClear = 0u;
static const uint kResolvePhasePopulateCurrent = 1u;
static const uint kResolvePhasePopulateFallback = 2u;
static const uint kResolvePhaseSchedule = 3u;

static const uint kPageManagementHierarchyVisibilityMask
    = OXYGEN_VSM_PAGE_FLAG_ALLOCATED
    | OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME
    | OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY
    | OXYGEN_VSM_PAGE_FLAG_HIERARCHY_ALLOCATED_DESCENDANT
    | OXYGEN_VSM_PAGE_FLAG_HIERARCHY_USED_THIS_FRAME_DESCENDANT
    | OXYGEN_VSM_PAGE_FLAG_HIERARCHY_DETAIL_DESCENDANT;

static const uint kVirtualResidentPageCoordBits = 28u;
static const uint64_t kVirtualResidentPageCoordMask = (1ull << kVirtualResidentPageCoordBits) - 1ull;
static const uint kVirtualResidentPageCoordSignBit = (1u << (kVirtualResidentPageCoordBits - 1u));

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

[shader("compute")]
[numthreads(64, 1, 1)]
void CS(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    if (g_PassConstantsIndex == K_INVALID_BINDLESS_INDEX
        || !BX_IN_GLOBAL_SRV(g_PassConstantsIndex)) {
        return;
    }

    ConstantBuffer<VirtualShadowResolvePassConstants> pass_constants =
        ResourceDescriptorHeap[g_PassConstantsIndex];

    if (!BX_IN_GLOBAL_SRV(pass_constants.page_table_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_metadata_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.physical_page_lists_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.resolve_stats_srv_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.page_table_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.page_flags_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_uav_index)
        || !BX_IN_GLOBAL_SRV(pass_constants.schedule_count_uav_index)) {
        return;
    }

    RWStructuredBuffer<uint> page_table =
        ResourceDescriptorHeap[pass_constants.page_table_uav_index];
    RWStructuredBuffer<uint> page_flags =
        ResourceDescriptorHeap[pass_constants.page_flags_uav_index];
    StructuredBuffer<VirtualShadowPhysicalPageMetadata> physical_page_metadata =
        ResourceDescriptorHeap[pass_constants.physical_page_metadata_srv_index];
    StructuredBuffer<VirtualShadowPhysicalPageListEntry> physical_page_lists =
        ResourceDescriptorHeap[pass_constants.physical_page_lists_srv_index];
    RWStructuredBuffer<uint4> schedule =
        ResourceDescriptorHeap[pass_constants.schedule_uav_index];
    RWStructuredBuffer<uint> schedule_count =
        ResourceDescriptorHeap[pass_constants.schedule_count_uav_index];

    const uint thread_index = dispatch_thread_id.x;

    if (pass_constants.phase == kResolvePhaseClear) {
        if (thread_index < pass_constants.total_page_count) {
            page_table[thread_index] = 0u;
            page_flags[thread_index] = 0u;
        }
        return;
    }

    if (pass_constants.phase == kResolvePhasePopulateCurrent) {
        const uint current_page_list_count =
            pass_constants.requested_page_list_count
            + pass_constants.dirty_page_list_count
            + pass_constants.clean_page_list_count;
        if (thread_index >= current_page_list_count) {
            return;
        }

        const VirtualShadowPhysicalPageListEntry list_entry = physical_page_lists[thread_index];
        if (list_entry.physical_page_index == 0xFFFFFFFFu) {
            return;
        }

        const VirtualShadowPhysicalPageMetadata metadata =
            physical_page_metadata[list_entry.physical_page_index];
        const uint clip_index = DecodeVirtualResidentPageKeyClipLevel(list_entry.resident_key);
        if (clip_index >= pass_constants.clip_level_count) {
            return;
        }

        const int local_page_x = DecodeVirtualResidentPageKeyGridX(list_entry.resident_key)
            - LoadPackedInt(pass_constants.clip_grid_origin_x_packed, clip_index);
        const int local_page_y = DecodeVirtualResidentPageKeyGridY(list_entry.resident_key)
            - LoadPackedInt(pass_constants.clip_grid_origin_y_packed, clip_index);
        if (local_page_x < 0 || local_page_y < 0
            || local_page_x >= int(pass_constants.pages_per_axis)
            || local_page_y >= int(pass_constants.pages_per_axis)) {
            return;
        }

        const uint local_page_index
            = uint(local_page_y) * pass_constants.pages_per_axis + uint(local_page_x);
        const uint global_page_index = clip_index * pass_constants.pages_per_level + local_page_index;
        if (global_page_index >= pass_constants.total_page_count) {
            return;
        }

        const bool requested_this_frame =
            thread_index < pass_constants.requested_page_list_count;
        page_table[global_page_index] = PackVirtualShadowPageTableEntry(
            DecodePackedTileX(metadata.packed_atlas_tile_coords),
            DecodePackedTileY(metadata.packed_atlas_tile_coords),
            0u,
            true,
            true,
            requested_this_frame);
        page_flags[global_page_index] = list_entry.page_flags;
        return;
    }

    if (pass_constants.phase == kResolvePhasePopulateFallback) {
        if (thread_index >= pass_constants.total_page_count) {
            return;
        }

        const uint current_packed_entry = page_table[thread_index];
        if (VirtualShadowPageTableEntryHasCurrentLod(DecodeVirtualShadowPageTableEntry(current_packed_entry))) {
            return;
        }

        if (pass_constants.pages_per_level == 0u || pass_constants.pages_per_axis == 0u) {
            return;
        }

        const uint clip_index = thread_index / pass_constants.pages_per_level;
        if (clip_index + 1u >= pass_constants.clip_level_count) {
            return;
        }

        const uint local_page_index = thread_index % pass_constants.pages_per_level;
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

            const uint candidate_flags = page_flags[candidate_global_page_index];
            if ((candidate_flags & kPageManagementHierarchyVisibilityMask) == 0u) {
                continue;
            }

            const uint resolved_fallback_clip = ResolveVirtualShadowFallbackClipIndex(
                candidate_clip, pass_constants.clip_level_count, candidate_entry);
            if (resolved_fallback_clip <= clip_index
                || resolved_fallback_clip >= pass_constants.clip_level_count) {
                continue;
            }

            page_table[thread_index] = PackVirtualShadowPageTableEntry(
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

    if (pass_constants.phase == kResolvePhaseSchedule) {
        if (!BX_IN_GLOBAL_SRV(pass_constants.request_words_srv_index)) {
            return;
        }

        if (thread_index >= pass_constants.request_word_count) {
            return;
        }

        StructuredBuffer<uint> request_words =
            ResourceDescriptorHeap[pass_constants.request_words_srv_index];
        uint request_word = request_words[thread_index];
        while (request_word != 0u) {
            const uint bit_index = firstbitlow(request_word);
            const uint page_index = thread_index * 32u + bit_index;
            if (page_index < pass_constants.total_page_count) {
                const uint packed_entry = page_table[page_index];
                if ((packed_entry & OXYGEN_VSM_PAGE_TABLE_CURRENT_LOD_VALID_BIT) != 0u) {
                    uint output_index = 0u;
                    InterlockedAdd(schedule_count[0], 1u, output_index);
                    if (output_index < pass_constants.schedule_capacity) {
                        const uint tile_x = packed_entry & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK;
                        const uint tile_y =
                            (packed_entry >> OXYGEN_VSM_PAGE_TABLE_TILE_Y_SHIFT)
                            & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK;
                        schedule[output_index] = uint4(page_index, packed_entry, tile_x, tile_y);
                    }
                }
            }

            request_word &= (request_word - 1u);
        }
    }
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VIRTUALSHADOWPAGEACCESS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VIRTUALSHADOWPAGEACCESS_HLSLI

static const uint OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK = 0x0FFFu;
static const uint OXYGEN_VSM_PAGE_TABLE_TILE_X_SHIFT = 0u;
static const uint OXYGEN_VSM_PAGE_TABLE_TILE_Y_SHIFT = 12u;
static const uint OXYGEN_VSM_PAGE_TABLE_FALLBACK_LOD_OFFSET_SHIFT = 24u;
static const uint OXYGEN_VSM_PAGE_TABLE_FALLBACK_LOD_OFFSET_MASK = 0x0Fu;
static const uint OXYGEN_VSM_PAGE_TABLE_CURRENT_LOD_VALID_BIT = (1u << 28u);
static const uint OXYGEN_VSM_PAGE_TABLE_ANY_LOD_VALID_BIT = (1u << 29u);
static const uint OXYGEN_VSM_PAGE_TABLE_REQUESTED_THIS_FRAME_BIT = (1u << 30u);

static const uint OXYGEN_VSM_PAGE_FLAG_ALLOCATED = (1u << 0u);
static const uint OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED = (1u << 1u);
static const uint OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED = (1u << 2u);
static const uint OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY = (1u << 3u);
static const uint OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME = (1u << 4u);

struct VirtualShadowPageTableEntry
{
    uint tile_x;
    uint tile_y;
    uint fallback_lod_offset;
    bool current_lod_valid;
    bool any_lod_valid;
    bool requested_this_frame;
};

static inline VirtualShadowPageTableEntry DecodeVirtualShadowPageTableEntry(uint packed_entry)
{
    VirtualShadowPageTableEntry entry;
    entry.tile_x =
        (packed_entry >> OXYGEN_VSM_PAGE_TABLE_TILE_X_SHIFT) & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK;
    entry.tile_y =
        (packed_entry >> OXYGEN_VSM_PAGE_TABLE_TILE_Y_SHIFT) & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK;
    entry.fallback_lod_offset =
        (packed_entry >> OXYGEN_VSM_PAGE_TABLE_FALLBACK_LOD_OFFSET_SHIFT)
        & OXYGEN_VSM_PAGE_TABLE_FALLBACK_LOD_OFFSET_MASK;
    entry.current_lod_valid =
        (packed_entry & OXYGEN_VSM_PAGE_TABLE_CURRENT_LOD_VALID_BIT) != 0u;
    entry.any_lod_valid =
        (packed_entry & OXYGEN_VSM_PAGE_TABLE_ANY_LOD_VALID_BIT) != 0u;
    entry.requested_this_frame =
        (packed_entry & OXYGEN_VSM_PAGE_TABLE_REQUESTED_THIS_FRAME_BIT) != 0u;
    return entry;
}

static inline bool VirtualShadowPageTableEntryHasCurrentLod(VirtualShadowPageTableEntry entry)
{
    return entry.current_lod_valid;
}

static inline bool VirtualShadowPageTableEntryHasAnyLod(VirtualShadowPageTableEntry entry)
{
    return entry.any_lod_valid;
}

static inline uint ResolveVirtualShadowFallbackClipIndex(
    uint clip_index,
    uint clip_level_count,
    VirtualShadowPageTableEntry entry)
{
    if (!entry.any_lod_valid || clip_level_count == 0u) {
        return clip_level_count;
    }

    return min(clip_level_count - 1u, clip_index + entry.fallback_lod_offset);
}

static inline bool VirtualShadowPageHasFlag(uint page_flags, uint flag)
{
    return (page_flags & flag) != 0u;
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VIRTUALSHADOWPAGEACCESS_HLSLI

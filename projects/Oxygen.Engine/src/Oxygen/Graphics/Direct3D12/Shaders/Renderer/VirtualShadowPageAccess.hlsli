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
static const uint OXYGEN_VSM_PAGE_FLAG_HIERARCHY_ALLOCATED_DESCENDANT = (1u << 5u);
static const uint OXYGEN_VSM_PAGE_FLAG_HIERARCHY_DYNAMIC_UNCACHED_DESCENDANT = (1u << 6u);
static const uint OXYGEN_VSM_PAGE_FLAG_HIERARCHY_STATIC_UNCACHED_DESCENDANT = (1u << 7u);
static const uint OXYGEN_VSM_PAGE_FLAG_HIERARCHY_DETAIL_DESCENDANT = (1u << 8u);
static const uint OXYGEN_VSM_PAGE_FLAG_HIERARCHY_USED_THIS_FRAME_DESCENDANT = (1u << 9u);
static const uint OXYGEN_VSM_PAGE_FLAG_BASE_MASK
    = OXYGEN_VSM_PAGE_FLAG_ALLOCATED
    | OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED
    | OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED
    | OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY
    | OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME;

static inline uint PackVirtualShadowPageTableEntry(
    uint tile_x,
    uint tile_y,
    uint fallback_lod_offset,
    bool current_lod_valid,
    bool any_lod_valid,
    bool requested_this_frame)
{
    const bool any_valid = any_lod_valid || current_lod_valid;
    return ((tile_x & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK) << OXYGEN_VSM_PAGE_TABLE_TILE_X_SHIFT)
        | ((tile_y & OXYGEN_VSM_PAGE_TABLE_TILE_COORD_MASK) << OXYGEN_VSM_PAGE_TABLE_TILE_Y_SHIFT)
        | ((fallback_lod_offset & OXYGEN_VSM_PAGE_TABLE_FALLBACK_LOD_OFFSET_MASK)
            << OXYGEN_VSM_PAGE_TABLE_FALLBACK_LOD_OFFSET_SHIFT)
        | (current_lod_valid ? OXYGEN_VSM_PAGE_TABLE_CURRENT_LOD_VALID_BIT : 0u)
        | (any_valid ? OXYGEN_VSM_PAGE_TABLE_ANY_LOD_VALID_BIT : 0u)
        | (requested_this_frame ? OXYGEN_VSM_PAGE_TABLE_REQUESTED_THIS_FRAME_BIT : 0u);
}

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

static inline uint MakeVirtualShadowHierarchyFlags(uint page_flags)
{
    const bool any_allocated
        = VirtualShadowPageHasFlag(page_flags, OXYGEN_VSM_PAGE_FLAG_ALLOCATED)
        || VirtualShadowPageHasFlag(
            page_flags, OXYGEN_VSM_PAGE_FLAG_HIERARCHY_ALLOCATED_DESCENDANT);
    const bool any_dynamic_uncached
        = VirtualShadowPageHasFlag(page_flags, OXYGEN_VSM_PAGE_FLAG_DYNAMIC_UNCACHED)
        || VirtualShadowPageHasFlag(
            page_flags, OXYGEN_VSM_PAGE_FLAG_HIERARCHY_DYNAMIC_UNCACHED_DESCENDANT);
    const bool any_static_uncached
        = VirtualShadowPageHasFlag(page_flags, OXYGEN_VSM_PAGE_FLAG_STATIC_UNCACHED)
        || VirtualShadowPageHasFlag(
            page_flags, OXYGEN_VSM_PAGE_FLAG_HIERARCHY_STATIC_UNCACHED_DESCENDANT);
    const bool any_detail
        = VirtualShadowPageHasFlag(page_flags, OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY)
        || VirtualShadowPageHasFlag(
            page_flags, OXYGEN_VSM_PAGE_FLAG_HIERARCHY_DETAIL_DESCENDANT);
    const bool any_used
        = VirtualShadowPageHasFlag(page_flags, OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME)
        || VirtualShadowPageHasFlag(
            page_flags, OXYGEN_VSM_PAGE_FLAG_HIERARCHY_USED_THIS_FRAME_DESCENDANT);

    return (any_allocated ? OXYGEN_VSM_PAGE_FLAG_HIERARCHY_ALLOCATED_DESCENDANT : 0u)
        | (any_dynamic_uncached ? OXYGEN_VSM_PAGE_FLAG_HIERARCHY_DYNAMIC_UNCACHED_DESCENDANT : 0u)
        | (any_static_uncached ? OXYGEN_VSM_PAGE_FLAG_HIERARCHY_STATIC_UNCACHED_DESCENDANT : 0u)
        | (any_detail ? OXYGEN_VSM_PAGE_FLAG_HIERARCHY_DETAIL_DESCENDANT : 0u)
        | (any_used ? OXYGEN_VSM_PAGE_FLAG_HIERARCHY_USED_THIS_FRAME_DESCENDANT : 0u);
}

static inline uint MergeVirtualShadowHierarchyFlags(uint base_flags, uint child_page_flags)
{
    return base_flags | MakeVirtualShadowHierarchyFlags(child_page_flags);
}

static inline bool HasVirtualShadowHierarchyVisibility(uint page_flags)
{
    return VirtualShadowPageHasFlag(page_flags, OXYGEN_VSM_PAGE_FLAG_ALLOCATED)
        || VirtualShadowPageHasFlag(page_flags, OXYGEN_VSM_PAGE_FLAG_USED_THIS_FRAME)
        || VirtualShadowPageHasFlag(page_flags, OXYGEN_VSM_PAGE_FLAG_DETAIL_GEOMETRY)
        || VirtualShadowPageHasFlag(
            page_flags, OXYGEN_VSM_PAGE_FLAG_HIERARCHY_ALLOCATED_DESCENDANT)
        || VirtualShadowPageHasFlag(
            page_flags, OXYGEN_VSM_PAGE_FLAG_HIERARCHY_USED_THIS_FRAME_DESCENDANT)
        || VirtualShadowPageHasFlag(
            page_flags, OXYGEN_VSM_PAGE_FLAG_HIERARCHY_DETAIL_DESCENDANT);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VIRTUALSHADOWPAGEACCESS_HLSLI

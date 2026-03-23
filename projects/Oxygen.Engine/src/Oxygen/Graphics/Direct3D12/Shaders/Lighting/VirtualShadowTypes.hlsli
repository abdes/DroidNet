//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_LIGHTING_VIRTUALSHADOWTYPES_HLSLI
#define OXYGEN_D3D12_SHADERS_LIGHTING_VIRTUALSHADOWTYPES_HLSLI

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
    uint scheduled_raster_page_count;
    uint allocated_page_count;
    uint requested_page_count;
    uint resident_dirty_page_count;
    uint resident_clean_page_count;
    uint pages_requiring_schedule_count;
    uint available_page_list_count;
    uint rasterized_page_count;
    uint cached_page_transition_count;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

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
    uint atlas_tiles_per_axis,
    uint physical_page_index)
{
    const uint safe_tiles_per_axis = max(atlas_tiles_per_axis, 1u);
    const uint tile_x = physical_page_index % safe_tiles_per_axis;
    const uint tile_y = physical_page_index / safe_tiles_per_axis;
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

#endif

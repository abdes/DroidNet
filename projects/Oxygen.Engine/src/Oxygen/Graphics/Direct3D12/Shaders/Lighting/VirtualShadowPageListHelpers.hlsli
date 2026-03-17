//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_LIGHTING_VIRTUALSHADOWPAGELISTHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_LIGHTING_VIRTUALSHADOWPAGELISTHELPERS_HLSLI

#include "Lighting/VirtualShadowTypes.hlsli"

static uint PhysicalPageListStart(
    uint physical_page_capacity,
    uint list_index)
{
    return list_index * physical_page_capacity;
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

#endif

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGETABLE_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGETABLE_HLSLI

#include "Vortex/Services/Shadows/Vsm/VsmCommon.hlsli"

// Virtual page-table ABI.
//
// One entry represents one virtual page owned by one virtual shadow map. The
// page-management path writes this buffer after reuse/allocation. Downstream
// raster, HZB, and projection passes read it to resolve a virtual page to a
// physical page in the shared pool.
//
// Encoding:
// - mapped bit set   : the entry owns a physical page
// - mapped bit clear : the entry is unmapped this frame
// - remaining bits   : physical page index inside the shared pool
struct VsmShaderPageTableEntry
{
    // Packed mapping payload consumed by VsmIsPageTableEntryMapped() and
    // VsmDecodePageTablePhysicalPage().
    uint encoded;
};

static VsmShaderPageTableEntry VsmMakeUnmappedPageTableEntry()
{
    VsmShaderPageTableEntry entry;
    entry.encoded = 0u;
    return entry;
}

static VsmShaderPageTableEntry VsmMakeMappedPageTableEntry(
    const uint physical_page)
{
    VsmShaderPageTableEntry entry;
    entry.encoded
        = VSM_PAGE_TABLE_MAPPED_BIT | (physical_page & VSM_PAGE_TABLE_PHYSICAL_PAGE_MASK);
    return entry;
}

static bool VsmIsPageTableEntryMapped(const VsmShaderPageTableEntry entry)
{
    return (entry.encoded & VSM_PAGE_TABLE_MAPPED_BIT) != 0u;
}

// Returns the physical page index payload. Callers should only interpret the
// value after checking VsmIsPageTableEntryMapped().
static uint VsmDecodePageTablePhysicalPage(const VsmShaderPageTableEntry entry)
{
    return entry.encoded & VSM_PAGE_TABLE_PHYSICAL_PAGE_MASK;
}

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGETABLE_HLSLI

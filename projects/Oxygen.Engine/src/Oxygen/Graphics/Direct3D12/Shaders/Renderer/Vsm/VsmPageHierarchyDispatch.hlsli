//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEHIERARCHYDISPATCH_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEHIERARCHYDISPATCH_HLSLI

// Compact per-level work item for stages 9 and 10.
//
// Each level still lives in a fixed-size per-level page-table slice, but the
// propagation logic collapses child coordinates into coarser parents through
// >> 1, matching hierarchical reduction semantics instead of 1:1 coordinate
// forwarding.
struct VsmShaderPageHierarchyDispatch
{
    uint first_page_table_entry;
    uint level0_pages_x;
    uint level0_pages_y;
    uint pages_per_level;
    uint source_level;
    uint target_level;
    uint source_pages_x;
    uint source_pages_y;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEHIERARCHYDISPATCH_HLSLI

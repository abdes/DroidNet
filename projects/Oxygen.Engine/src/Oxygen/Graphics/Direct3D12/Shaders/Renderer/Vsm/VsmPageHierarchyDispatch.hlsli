//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEHIERARCHYDISPATCH_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEHIERARCHYDISPATCH_HLSLI

// Compact per-level work item for stages 9 and 10.
//
// Oxygen's current virtual-map layouts use the same page-grid footprint for
// each level within one map, so propagation advances between matching (x, y)
// coordinates at adjacent levels instead of aggregating 2x2 child quads.
struct VsmShaderPageHierarchyDispatch
{
    uint first_page_table_entry;
    uint pages_x;
    uint pages_y;
    uint pages_per_level;
    uint source_level;
    uint target_level;
    uint _pad0;
    uint _pad1;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMPAGEHIERARCHYDISPATCH_HLSLI

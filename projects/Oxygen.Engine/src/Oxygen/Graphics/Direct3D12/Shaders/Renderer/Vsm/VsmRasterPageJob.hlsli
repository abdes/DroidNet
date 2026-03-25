//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMRASTERPAGEJOB_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMRASTERPAGEJOB_HLSLI

struct VsmRasterPageJob
{
    float4x4 view_projection_matrix;
    uint page_table_index;
    uint map_id;
    uint virtual_page_x;
    uint virtual_page_y;
    uint virtual_page_level;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

#endif // OXYGEN_D3D12_SHADERS_RENDERER_VSM_VSMRASTERPAGEJOB_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_PROCEDURALGRIDMATERIALCONSTANTS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_PROCEDURALGRIDMATERIALCONSTANTS_HLSLI

// ABI: must match sizeof(oxygen::engine::ProceduralGridMaterialConstants) == 112
struct ProceduralGridMaterialConstants
{
    float2 grid_spacing;
    uint grid_major_every;
    float grid_line_thickness;

    float grid_major_thickness;
    float grid_axis_thickness;
    float grid_fade_start;
    float grid_fade_end;

    float4 grid_minor_color;
    float4 grid_major_color;
    float4 grid_axis_color_x;
    float4 grid_axis_color_y;
    float4 grid_origin_color;
};

#endif // OXYGEN_D3D12_SHADERS_RENDERER_PROCEDURALGRIDMATERIALCONSTANTS_HLSLI

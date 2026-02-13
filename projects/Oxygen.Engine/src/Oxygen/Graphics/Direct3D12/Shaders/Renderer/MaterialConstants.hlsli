//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_MATERIALCONSTANTS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_MATERIALCONSTANTS_HLSLI

// ABI: must match sizeof(oxygen::engine::MaterialConstants) == 224
struct MaterialConstants
{
    // Register 0
    float4 base_color;

    // Register 1
    float3 emissive_factor;
    uint flags;

    // Register 2
    float metalness;
    float roughness;
    float normal_scale;
    float ambient_occlusion;

    // Register 3
    uint base_color_texture_index;
    uint normal_texture_index;
    uint metallic_texture_index;
    uint roughness_texture_index;

    // Register 4
    uint ambient_occlusion_texture_index;
    uint opacity_texture_index;
    uint emissive_texture_index;
    float alpha_cutoff;

    // Register 5
    float2 uv_scale;
    float2 uv_offset;

    // Register 6
    float uv_rotation_radians;
    uint uv_set;
    uint _pad0;
    uint _pad1;

    // Register 7
    float2 grid_spacing;
    uint grid_major_every;
    float grid_line_thickness;

    // Register 8
    float grid_major_thickness;
    float grid_axis_thickness;
    float grid_fade_start;
    float grid_fade_end;

    // Register 9
    float4 grid_minor_color;

    // Register 10
    float4 grid_major_color;

    // Register 11
    float4 grid_axis_color_x;

    // Register 12
    float4 grid_axis_color_y;

    // Register 13
    float4 grid_origin_color;
};

// UV convention:
// - Rotation is in radians, counter-clockwise, around the UV origin (0,0).
// - Transform order is: scale -> rotation -> offset.
// - uv_set selects the source UV set (0 = TEXCOORD0). Other sets require
//   vertex data support; currently only uv0 is available.
float2 ApplyMaterialUv(float2 uv0, MaterialConstants mat)
{
    float2 uv = uv0;
    // TODO: Support selecting alternate UV sets when available.
    if (mat.uv_set != 0u) {
        uv = uv0;
    }

    uv *= mat.uv_scale;
    if (mat.uv_rotation_radians != 0.0f) {
        const float c = cos(mat.uv_rotation_radians);
        const float s = sin(mat.uv_rotation_radians);
        uv = float2(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
    }
    uv += mat.uv_offset;
    return uv;
}

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_MATERIALCONSTANTS_HLSLI

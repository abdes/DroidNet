//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_MATERIALCONSTANTS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_MATERIALCONSTANTS_HLSLI

// ABI: must match sizeof(oxygen::engine::MaterialConstants) == 80
struct MaterialConstants
{
    float4 base_color;
    float metalness;
    float roughness;
    float normal_scale;
    float ambient_occlusion;
    uint base_color_texture_index;
    uint normal_texture_index;
    uint metallic_texture_index;
    uint roughness_texture_index;
    uint ambient_occlusion_texture_index;
    uint flags;
    float2 uv_scale;
    float2 uv_offset;
    uint _pad0;
    uint _pad1;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_MATERIALCONSTANTS_HLSLI

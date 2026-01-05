//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_SCENECONSTANTS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_SCENECONSTANTS_HLSLI

// Mirrors oxygen::engine::SceneConstants::GpuData (sizeof = 192)
cbuffer SceneConstants : register(b1, space0)
{
    float4x4 view_matrix;
    float4x4 projection_matrix;
    float3 camera_position;
    uint frame_slot;
    uint64_t frame_seq_num;
    float time_seconds;
    uint _pad0;

    uint bindless_draw_metadata_slot;
    uint bindless_transforms_slot;
    uint bindless_normal_matrices_slot;
    uint bindless_material_constants_slot;

    uint bindless_env_static_slot;
    uint bindless_directional_lights_slot;
    uint bindless_directional_shadows_slot;
    uint bindless_positional_lights_slot;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_SCENECONSTANTS_HLSLI

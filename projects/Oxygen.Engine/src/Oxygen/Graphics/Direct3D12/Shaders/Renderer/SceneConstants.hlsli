//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_SCENECONSTANTS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_SCENECONSTANTS_HLSLI

// Mirrors oxygen::engine::SceneConstants::GpuData (sizeof = 256)
cbuffer SceneConstants : register(b1, space0)
{
    uint64_t frame_seq_num;
    uint frame_slot;
    float time_seconds;
    float4x4 view_matrix;
    float4x4 projection_matrix;
    float3 camera_position;
    float exposure;

    uint bindless_draw_metadata_slot;
    uint bindless_transforms_slot;
    uint bindless_normal_matrices_slot;
    uint bindless_material_constants_slot;

    uint bindless_env_static_slot;
    uint bindless_directional_lights_slot;
    uint bindless_directional_shadows_slot;
    uint bindless_positional_lights_slot;

    uint bindless_instance_data_slot;
    uint bindless_gpu_debug_line_slot;
    uint bindless_gpu_debug_counter_slot;
    uint _pad_to_16_1;

    float4 _pad_to_256_1;
    float4 _pad_to_256_2;
    float4 _pad_to_256_3;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_SCENECONSTANTS_HLSLI

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
    float _pad0;

    uint bindless_draw_metadata_slot;
    uint bindless_transforms_slot;
    uint bindless_normal_matrices_slot;
    uint bindless_material_constants_slot;

    uint bindless_instance_data_slot;
    uint bindless_view_frame_bindings_slot;
    uint _pad1;
    uint _pad2;

    float4 _pad_to_256_1;
    float4 _pad_to_256_2;
    float4 _pad_to_256_3;
    float4 _pad_to_256_4;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_SCENECONSTANTS_HLSLI

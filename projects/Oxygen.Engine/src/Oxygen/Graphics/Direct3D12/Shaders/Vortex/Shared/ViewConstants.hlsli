//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SHARED_VIEWCONSTANTS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SHARED_VIEWCONSTANTS_HLSLI

cbuffer ViewConstants : register(b1, space0)
{
    uint64_t frame_seq_num;
    uint frame_slot;
    float time_seconds;
    float4x4 view_matrix;
    float4x4 projection_matrix;
    float4x4 inverse_view_projection_matrix;
    float3 camera_position;
    float _pad0;

    uint bindless_view_frame_bindings_slot;
    uint reverse_z;
    uint _pad2;
    uint _pad3;
    float4 _pad_to_256_1;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SHARED_VIEWCONSTANTS_HLSLI

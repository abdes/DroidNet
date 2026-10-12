//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SHARED_MESHCLIPPOSITION_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SHARED_MESHCLIPPOSITION_HLSLI

// Clip position of a mesh vertex in the view given by its matrices.
//
// Passes that depth-test against the depth prepass with GreaterOrEqual must
// rasterize a draw at bit-identical depth. They all compute the position
// here, and `precise` stops the compiler from reassociating or fusing the
// math differently per shader. Camera passes pass the bound view's
// view_matrix and projection_matrix.
float4 ComputeMeshClipPosition(float4x4 world_matrix, float3 local_position,
    float3 world_offset, float4x4 view_matrix, float4x4 projection_matrix)
{
    precise float4 world_position
        = mul(world_matrix, float4(local_position, 1.0f));
    world_position.xyz += world_offset;
    precise float4 clip_position
        = mul(projection_matrix, mul(view_matrix, world_position));
    return clip_position;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SHARED_MESHCLIPPOSITION_HLSLI

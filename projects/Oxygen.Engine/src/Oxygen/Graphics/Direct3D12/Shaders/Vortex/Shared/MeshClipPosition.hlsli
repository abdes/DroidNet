//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SHARED_MESHCLIPPOSITION_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SHARED_MESHCLIPPOSITION_HLSLI

#include "Vortex/Contracts/View/ViewConstants.hlsli"

// Clip position of a mesh vertex in the current view.
//
// Passes that depth-test against the depth prepass with GreaterOrEqual must
// rasterize a draw at bit-identical depth. They all compute the position
// here, and `precise` stops the compiler from reassociating or fusing the
// math differently per shader.
float4 ComputeMeshClipPosition(
    float4x4 world_matrix, float3 local_position, float3 world_offset)
{
    precise float4 world_position
        = mul(world_matrix, float4(local_position, 1.0f));
    world_position.xyz += world_offset;
    precise float4 clip_position
        = mul(projection_matrix, mul(view_matrix, world_position));
    return clip_position;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SHARED_MESHCLIPPOSITION_HLSLI

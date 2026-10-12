//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_DRAW_MESHPOSITION_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_DRAW_MESHPOSITION_HLSLI

#include "Vortex/Contracts/Draw/DrawFrameBindings.hlsli"
#include "Vortex/Contracts/Draw/VelocityPublications.hlsli"

// Mesh vertex positions. Every pass that rasterizes scene meshes (depth
// prepass, base pass, velocity, shadow and contact-shadow depth) computes them
// here, with the draw's material world-position offset, so every view
// rasterizes the same geometry. `precise` stops the compiler from
// reassociating or fusing the math differently per shader, so passes that
// depth-test against the depth prepass with GreaterOrEqual see bit-identical
// depth.

// The draw's current material world-position offset; zero when the draw
// publishes none.
float3 ResolveDrawWorldOffset(DrawFrameBindings draw_bindings, uint draw_index)
{
    VelocityDrawMetadata velocity_metadata = MakeInvalidVelocityDrawMetadata();
    LoadVelocityDrawMetadata(
        draw_bindings.velocity_draw_metadata_slot, draw_index, velocity_metadata);
    return ResolveCurrentMaterialWpoOffset(draw_bindings, velocity_metadata);
}

float4 ComputeMeshWorldPosition(
    float4x4 world_matrix, float3 local_position, float3 world_offset)
{
    precise float4 world_position
        = mul(world_matrix, float4(local_position, 1.0f));
    world_position.xyz += world_offset;
    return world_position;
}

// Clip position in the view given by its matrices. Camera passes pass the
// bound view's view_matrix and projection_matrix.
float4 ComputeMeshClipPosition(float4x4 world_matrix, float3 local_position,
    float3 world_offset, float4x4 view_matrix, float4x4 projection_matrix)
{
    precise float4 world_position
        = ComputeMeshWorldPosition(world_matrix, local_position, world_offset);
    precise float4 clip_position
        = mul(projection_matrix, mul(view_matrix, world_position));
    return clip_position;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_DRAW_MESHPOSITION_HLSLI

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Sky Sphere Vertex Shader
//!
//! Generates a fullscreen triangle for sky background rendering.
//! Computes world-space view direction for cubemap sampling in pixel shader.

#include "Renderer/FullscreenTriangle.hlsli"
#include "Renderer/SceneConstants.hlsli"

//! Vertex shader output for sky rendering.
struct SkyVSOutput
{
    float4 position : SV_POSITION; //!< Clip-space position (z=1 for far plane).
    float3 view_dir : TEXCOORD0;   //!< World-space view direction (unnormalized).
    float2 uv : TEXCOORD1;         //!< Screen UV coordinates.
};

//! Computes the inverse of view-projection matrix.
//! Note: This is done per-vertex (3 vertices) which is acceptable.
float4x4 ComputeInvViewProj()
{
    float4x4 view_proj = mul(projection_matrix, view_matrix);
    // HLSL doesn't have built-in matrix inverse, so we use transpose trick
    // for view-projection where possible. However, for a general inverse,
    // we need to compute it. For sky rendering, we can use a simpler approach:
    // reconstruct view direction from the clip-space corners.

    // For now, compute analytically by combining inverse view and inverse proj.
    // Inverse of orthonormal view matrix is its transpose (for rotation part).
    // This is a simplification - full inverse would be more robust.

    // Actually, let's use a different approach: compute view ray from clip position
    // using the view and projection matrices directly.
    return (float4x4)0; // Placeholder - we'll use a different method below.
}

[shader("vertex")]
SkyVSOutput VS(uint vertex_id : SV_VertexID)
{
    SkyVSOutput output;

    // Generate fullscreen triangle vertex position and UVs.
    float4 clip_pos;
    float2 uv;
    GenerateFullscreenTriangle(vertex_id, clip_pos, uv);

    output.position = clip_pos;
    output.uv = uv;

    // Compute world-space view direction by unprojecting clip position.
    // We need to go from clip space -> view space -> world space.

    // First, unproject from clip to view space using inverse projection.
    // For a perspective projection, we can derive the view-space direction
    // from the clip-space x, y coordinates and the projection matrix.

    // Extract projection parameters from the projection matrix.
    // Standard perspective projection has:
    //   P[0][0] = 1 / (aspect * tan(fov/2))
    //   P[1][1] = 1 / tan(fov/2)
    // So: view_x = clip_x / P[0][0], view_y = clip_y / P[1][1], view_z = -1 (forward)

    float3 view_dir_vs;
    view_dir_vs.x = clip_pos.x / projection_matrix[0][0];
    view_dir_vs.y = clip_pos.y / projection_matrix[1][1];
    view_dir_vs.z = -1.0f; // Looking down -Z in view space (right-handed).

    // Transform from view space to world space using inverse of view matrix.
    // For an orthonormal rotation matrix, inverse = transpose.
    // View matrix = Rotation * Translation, so we need to handle both.
    // Extract the 3x3 rotation part and transpose it.
    float3x3 inv_view_rot = transpose((float3x3)view_matrix);

    // Rotate view direction to world space.
    output.view_dir = mul(inv_view_rot, view_dir_vs);

    return output;
}

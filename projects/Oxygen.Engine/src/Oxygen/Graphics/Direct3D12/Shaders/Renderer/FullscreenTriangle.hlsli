//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_FULLSCREENTRIANGLE_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_FULLSCREENTRIANGLE_HLSLI

//! Output structure for fullscreen triangle vertex shader.
struct FullscreenVSOutput
{
    float4 position : SV_POSITION; //!< Clip-space position.
    float2 uv : TEXCOORD0;         //!< Texture coordinates [0, 1].
    float3 view_dir_ws : TEXCOORD1; //!< World-space view direction (unnormalized).
};

//! Generates a fullscreen triangle from SV_VertexID.
/*!
 Produces a single triangle that covers the entire screen using three vertices.
 The triangle extends beyond normalized device coordinates to ensure full
 coverage without needing index buffers or vertex data.

 Vertex layout (counter-clockwise):
   ID=0: (-1, -1) bottom-left
   ID=1: ( 3, -1) bottom-right (extends past screen)
   ID=2: (-1,  3) top-left (extends past screen)

 UV coordinates are computed to match standard DirectX convention:
   (0,0) at top-left, (1,1) at bottom-right.

 @param vertex_id The SV_VertexID semantic value (0, 1, or 2).
 @param[out] position Clip-space position with w=1 and z=1 (far plane for reverse-Z).
 @param[out] uv Texture coordinates in [0, 1] range.
*/
static inline void GenerateFullscreenTriangle(
    uint vertex_id,
    out float4 position,
    out float2 uv)
{
    // Generate clip-space coordinates for a triangle covering the full screen.
    // Uses bit manipulation for branchless coordinate generation.
    float2 corner;
    corner.x = (vertex_id == 1) ? 3.0f : -1.0f;
    corner.y = (vertex_id == 2) ? -3.0f : 1.0f;

    // Position at z=1 (far plane in reverse-Z) for sky rendering.
    // This allows depth test LESS_EQUAL to pass only where no geometry was rendered.
    position = float4(corner.x, corner.y, 1.0f, 1.0f);

    // UV coordinates: remap from clip [-1,1] to texture [0,1].
    // Note: DirectX texture convention has (0,0) at top-left.
    uv.x = (corner.x + 1.0f) * 0.5f;
    uv.y = (1.0f - corner.y) * 0.5f;
}

//! Computes world-space view direction from clip-space position.
/*!
 Reconstructs the view ray from clip-space position using the inverse
 view-projection matrix. The result is an unnormalized world-space direction
 from the camera position toward the far plane.

 @param clip_pos Clip-space position (x, y in [-1, 1], z=1, w=1).
 @param inv_view_proj Inverse of the combined view-projection matrix.
 @return Unnormalized world-space view direction.
*/
static inline float3 ComputeViewDirection(float4 clip_pos, float4x4 inv_view_proj)
{
    // Transform clip-space position to world-space.
    float4 world_pos = mul(inv_view_proj, clip_pos);
    world_pos.xyz /= world_pos.w;

    // The direction is from camera origin (implicitly at inverse transform origin)
    // to the world position. Since we're at z=1 (far plane), this gives the view ray.
    return world_pos.xyz;
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_FULLSCREENTRIANGLE_HLSLI

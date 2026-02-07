//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Sky Capture Vertex Shader
//!
//! Generates a fullscreen triangle for sky capture.
//! Uses a dedicated face-specific view/projection matrix.

#include "Renderer/FullscreenTriangle.hlsli"

// Face constants (b2, space0 - shared root param with engine RootConstants)
// In SkyCapturePass, we'll bind a small CBV here.
struct SkyCaptureFaceConstants
{
    float4x4 view_matrix;
    float4x4 projection_matrix;
};

cbuffer RootConstants : register(b2, space0)
{
    uint g_DrawIndex;
    uint g_FaceConstantsIndex; // Use this to point to the face constants
}

struct SkyVSOutput
{
    float4 position : SV_POSITION;
    float3 view_dir : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

SkyVSOutput VS(uint vertex_id : SV_VertexID)
{
    SkyVSOutput output;

    // Generate fullscreen triangle vertex position and UVs.
    float4 clip_pos;
    float2 uv;
    GenerateFullscreenTriangle(vertex_id, clip_pos, uv);

    output.position = clip_pos;
    output.uv = uv;

    // Load face constants via bindless index
    ConstantBuffer<SkyCaptureFaceConstants> face
        = ResourceDescriptorHeap[g_FaceConstantsIndex];

    // Compute world-space view direction
    // view_x = clip_x / P[0][0], view_y = clip_y / P[1][1], view_z = -1 (forward)
    float3 view_dir_vs;
    view_dir_vs.x = clip_pos.x / face.projection_matrix[0][0];
    view_dir_vs.y = clip_pos.y / face.projection_matrix[1][1];
    view_dir_vs.z = -1.0f;

    // Rotate to world space
    float3x3 inv_view_rot = transpose((float3x3)face.view_matrix);
    output.view_dir = mul(inv_view_rot, view_dir_vs);

    return output;
}

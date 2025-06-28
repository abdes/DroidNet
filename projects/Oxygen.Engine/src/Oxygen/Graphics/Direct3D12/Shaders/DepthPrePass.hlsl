//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// === Bindless Rendering Contract ===
// - The engine provides a single shader-visible descriptor heap for all CBV,
//   SRV, and UAV resources.
// - Samplers are managed in a separate heap.
// - All resource descriptors (vertex buffers, textures, etc.) are mixed in the
//   same heap and use the same register space (space0).
// - The root constant buffer (register b0) contains global indices to access
//   resources in the bindless arrays.
// - The root signature and engine must match this layout for correct operation.
//   See MainModule.cpp and PipelineStateCache.cpp for details.

struct VertexData
{
    float3 position;
    float3 normal;
    float2 texcoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

// Bindless resource arrays: all resources are in the same descriptor heap and use space0
StructuredBuffer<VertexData> g_BindlessVertexBuffers[] : register(t0, space0);
// Example for other resource types in the same heap:
// Texture2D g_BindlessTextures[] : register(t0, space0);
// ByteAddressBuffer g_BindlessConstants[] : register(t0, space0);
// (All indices are global indices into the unified heap)
// Samplers are still declared in their own heap/space:
// SamplerState g_BindlessSamplers[] : register(s0, space0);

// Constant buffer for bindless resource access configured by the engine.
// Contains global indices for accessing resources in the bindless arrays.
cbuffer ResourceIndices : register(b0) {
    uint g_VertexBufferIndex;  // Global index into g_BindlessVertexBuffers array
    // uint g_TextureIndex;    // Global index into g_BindlessTextures[]
    // uint g_SamplerIndex;    // Index into g_BindlessSamplers[]
    // uint g_MaterialIndex;   // Global index into g_BindlessConstants[]
};

// Constant buffer for scene-wide data. This is typically bound directly as a
// root CBV (not indexed from the descriptor heap), allowing fast and efficient
// updates for per-frame or per-draw constants.
cbuffer SceneConstants : register(b1) {
    float4x4 world_matrix;
    float4x4 view_matrix;
    float4x4 projection_matrix;
    float3 camera_position;
    float _pad0; // Padding to match C++ struct alignment
};

// Output structure for the Vertex Shader
struct VS_OUTPUT_DEPTH {
    float4 clipSpacePosition : SV_POSITION;
};

// Vertex Shader: DepthOnlyVS
// Transforms vertices to clip space for depth buffer population.
// Entry point should match the identifier used in PipelineStateDesc (e.g., "DepthOnlyVS").
[shader("vertex")]
VS_OUTPUT_DEPTH VS(uint vertexID : SV_VertexID) {
    VS_OUTPUT_DEPTH output;

    // Fetch the current vertex data from the bindless buffer array
    // using the global index from ResourceIndices and the system-generated vertexID.
    VertexData currentVertex = g_BindlessVertexBuffers[g_VertexBufferIndex][vertexID];

    // Transform position from object space to clip space using world, view, projection
    float4 world_pos = mul(world_matrix, float4(currentVertex.position, 1.0f));
    float4 view_pos = mul(view_matrix, world_pos);
    output.clipSpacePosition = mul(projection_matrix, view_pos);

    return output;
}

// Pixel Shader: MinimalPS
// This shader is minimal as no color output is needed for the depth pre-pass.
// The pipeline state should be configured with no color render targets.
// Entry point should match the identifier used in PipelineStateDesc (e.g., "MinimalPS").
[shader("pixel")]
void PS(VS_OUTPUT_DEPTH input) {
    // Intentionally empty.
    // Depth writes are handled by the GPU's rasterizer and depth test unit
    // based on the SV_POSITION output from the vertex shader and the
    // depth/stencil state configured in the pipeline state object.
    // If alpha testing were required for this depth pass (e.g., for foliage textures),
    // you would implement it using the bindless system as follows:
    // ...
}

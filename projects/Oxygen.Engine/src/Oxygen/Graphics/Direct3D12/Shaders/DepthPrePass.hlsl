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

// Matches the CPU-side Vertex structure
// (Ensure this matches your actual common Vertex struct definition)
struct VertexData {
    float3 position; // Object-space position
    float3 color;    // Color attribute (present in example, not used by depth pass)
    // Add other attributes like UVs, normals if they are part of the common Vertex struct
    // and might be needed by the interpolators for alpha testing in a more complex depth pass.
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
    matrix worldViewProjectionMatrix;
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

    // Transform position from object space to clip space.
    // Assumes currentVertex.position is in object space.
    output.clipSpacePosition = mul(worldViewProjectionMatrix, float4(currentVertex.position, 1.0f));

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
    //
    // 1. Update VertexData to include texture coordinates:
    //    struct VertexData {
    //        float3 position;
    //        float3 color;
    //        float2 texCoord;  // Added for texture sampling
    //    };
    //
    // 2. Pass texCoords through VS_OUTPUT_DEPTH:
    //    struct VS_OUTPUT_DEPTH {
    //        float4 clipSpacePosition : SV_POSITION;
    //        float2 texCoord : TEXCOORD0;
    //    };
    //
    // 3. Add texture and sampler indices to ResourceIndices:
    //    cbuffer ResourceIndices : register(b0) {
    //        uint g_VertexBufferIndex;
    //        uint g_AlbedoTextureIndex;  // Global index for alpha testing
    //        uint g_SamplerIndex;
    //    };
    //
    // 4. Declare bindless textures and samplers (all in space0 except samplers):
    //    Texture2D g_BindlessTextures[] : register(t0, space0);
    //    SamplerState g_BindlessSamplers[] : register(s0, space0);
    //
    // 5. Implement alpha testing in PS:
    //    float alpha = g_BindlessTextures[g_AlbedoTextureIndex].Sample(
    //        g_BindlessSamplers[g_SamplerIndex], input.texCoord).a;
    //    clip(alpha - 0.5); // Discard pixel if alpha < 0.5
}

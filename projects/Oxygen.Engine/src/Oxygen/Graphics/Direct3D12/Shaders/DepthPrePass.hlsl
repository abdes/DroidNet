//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// === Bindless Rendering Contract ===
// - The engine provides single shader-visible descriptor heaps for SRV/UAV/CBV, Samplers, etc.
// - Root constant buffer (register b0) contains indices to access resources in bindless arrays.
// - Resources are organized in register spaces:
//   - space0 register t0: Vertex buffers as structured buffers (g_BindlessVertexBuffers)
//   - (space1 register t0 could contain textures, space2 could have constants, etc.)
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

// Using DXC 1.8.2502.8, implementing bindless resources with structured buffers
// Array of StructuredBuffers for vertex data (traditional approach)
// Using register space0 for vertex buffer access
StructuredBuffer<VertexData> g_BindlessVertexBuffers[] : register(t0, space0);

// Examples of other resource types for a comprehensive bindless system:
// Texture2D g_BindlessTextures[] : register(t0, space1);
// SamplerState g_BindlessSamplers[] : register(s0, space0);
// ByteAddressBuffer g_BindlessConstants[] : register(t0, space2);

// Constant buffer for bindless resource access configured by the engine.
// Contains indices for accessing resources in the bindless arrays.
cbuffer ResourceIndices : register(b0) {
    uint g_VertexBufferIndex;  // Index into g_BindlessVertexBuffers array

    // Reserved for future expansion as bindless system grows:
    // uint g_TextureIndex;    // Would index into g_BindlessTextures[] in space1
    // uint g_SamplerIndex;    // Would index into g_BindlessSamplers[] in space0
    // uint g_MaterialIndex;   // Would index into g_BindlessConstants[] in space2
};

// Constant buffer for scene-wide data. The engine must update this buffer as
// appropriate (e.g., per-frame or per-object).
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
    // using the index from ResourceIndices and the system-generated vertexID.
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
    // Depth writes are handled by the GPU's rasterizer and depth test unit    // based on the SV_POSITION output from the vertex shader and the
    // depth/stencil state configured in the pipeline state object.    // If alpha testing were required for this depth pass (e.g., for foliage textures),
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
    // 3. Add texture indices to ResourceIndices:
    //    cbuffer ResourceIndices : register(b0) {
    //        uint g_VertexBufferIndex;
    //        uint g_AlbedoTextureIndex;  // For alpha testing
    //        uint g_SamplerIndex;
    //    };
    //
    // 4. Declare bindless textures and samplers:
    //    Texture2D g_BindlessTextures[] : register(t0, space1);
    //    SamplerState g_BindlessSamplers[] : register(s0, space0);
    //
    // 5. Implement alpha testing in PS:
    //    float alpha = g_BindlessTextures[g_AlbedoTextureIndex].Sample(
    //        g_BindlessSamplers[g_SamplerIndex], input.texCoord).a;
    //    clip(alpha - 0.5); // Discard pixel if alpha < 0.5
}

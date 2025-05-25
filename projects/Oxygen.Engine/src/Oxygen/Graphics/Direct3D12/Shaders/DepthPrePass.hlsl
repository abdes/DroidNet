// HLSL Shaders for Depth Pre-Pass (DepthOnlyVS & MinimalPS)
// File: DepthPrePass.hlsl
//
// Bindless Rendering System Contract:
// - Vertex buffer index is passed via a Constant Buffer (CBV) at register(b0).
// - Vertex data is accessed from an array of StructuredBuffers at register(t0, space0).
// - Scene constants (e.g., WorldViewProjection matrix) are assumed in a CBV at register(b1).

// Matches the CPU-side Vertex structure
// (Ensure this matches your actual common Vertex struct definition)
struct VertexData {
    float3 position; // Object-space position
    float3 color;    // Color attribute (present in example, not used by depth pass)
    // Add other attributes like UVs, normals if they are part of the common Vertex struct
    // and might be needed by the interpolators for alpha testing in a more complex depth pass.
};

// Constant buffer for scene-wide data
// The engine must update this buffer as appropriate (e.g., per-frame or per-object).
cbuffer SceneConstants : register(b1) {
    matrix worldViewProjectionMatrix;
};

// Constant buffer for bindless vertex buffer access
// Contains the index into the g_BindlessVertexBuffers array.
cbuffer VertexBufferConstants : register(b0) {
    uint g_VertexBufferIndex;
};

// Array of StructuredBuffers, representing bindless vertex buffers.
// Each element is an SRV to a vertex buffer.
StructuredBuffer<VertexData> g_BindlessVertexBuffers[] : register(t0, space0);

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
    // using the index from VertexBufferConstants and the system-generated vertexID.
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
    // you would:
    // 1. Ensure VertexData and VS_OUTPUT_DEPTH pass through texture coordinates.
    // 2. Sample the alpha texture (e.g., g_BindlessTextures[textureIndex].Sample(samplerState, input.texCoord).a).
    // 3. Use 'clip(sampledAlpha - alphaThreshold);' to discard pixels below the threshold.
    // For this basic opaque depth pass, alpha testing is not included.
}

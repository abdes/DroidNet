//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Define vertex structure to match the CPU-side Vertex struct
struct Vertex {
    float3 position;
    float3 color;
};

// Define constant buffer explicitly to match our root signature
cbuffer VertexBufferConstants : register(b0) {
    uint g_VertexBufferIndex;
};

// Access to the bindless descriptor heap
StructuredBuffer<Vertex> g_BindlessVertexBuffers[] : register(t0, space0);

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

[shader("vertex")]
VSOutput VS(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Access vertex data from the structured buffer using the index
    // Use the correct SRV index from the CBV
    Vertex vertex = g_BindlessVertexBuffers[g_VertexBufferIndex][vertexID];

    output.position = float4(vertex.position, 1.0);
    output.color = vertex.color;
    return output;
}

[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0 {
    // Output the interpolated color from the vertex shader
    return float4(input.color, 1.0);
}

// === Bindless Rendering Contract ===
// - The engine provides a single shader-visible CBV_SRV_UAV heap.
// - The constant buffer (CBV) is always at heap index 0 (register b0).
// - All other resources (SRVs, UAVs) are in the heap starting at index 1.
// - For this shader, the vertex buffer SRV is always at heap index 1 (register t0, space0).
// - The CBV contains a uint specifying the index of the vertex buffer SRV in the heap (should be 1 for this draw).
// - The root signature and engine must match this layout for correct operation.
// See MainModule.cpp and PipelineStateCache.cpp for details.

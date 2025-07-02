//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// === Bindless Rendering Contract ===
// - The engine provides a single shader-visible CBV_SRV_UAV heap.
// - The constant buffer (CBV) is always at heap index 0 (register b0).
// - All other resources (SRVs, UAVs) are in the heap starting at index 1.
// - For this shader, the vertex buffer SRV is always at heap index 1 (register
//   t0, space0).
// - The CBV contains a uint specifying the index of the vertex buffer SRV in
//   the heap (should be 1 for this draw).
// - The root signature and engine must match this layout for correct operation.
//   See MainModule.cpp and PipelineStateCache.cpp for details.

// Define vertex structure to match the CPU-side Vertex struct
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

// Define constant buffer explicitly to match our root signature
cbuffer VertexBufferConstants : register(b0) {
    uint g_VertexBufferIndex;
};

// Scene constants buffer (matches C++ struct layout)
cbuffer SceneConstants : register(b1) {
    float4x4 world_matrix;
    float4x4 view_matrix;
    float4x4 projection_matrix;
    float3 camera_position;
    float _pad0; // Padding to match C++ struct alignment
}

// Material constants buffer (matches C++ MaterialConstants struct layout)
cbuffer MaterialConstants : register(b2) {
    float4 base_color;                      // RGBA fallback color
    float metalness;                        // Metalness scalar
    float roughness;                        // Roughness scalar
    float normal_scale;                     // Normal map scale
    float ambient_occlusion;                // AO scalar
    uint base_color_texture_index;          // Texture indices (bindless)
    uint normal_texture_index;
    uint metallic_texture_index;
    uint roughness_texture_index;
    uint ambient_occlusion_texture_index;
    uint flags;                             // Material flags
    float _mat_pad0;                        // Padding for alignment
    float _mat_pad1;                        // Padding for alignment
}

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
    Vertex vertex = g_BindlessVertexBuffers[g_VertexBufferIndex][vertexID];

    // Apply world, view, and projection transforms
    float4 world_pos = mul(world_matrix, float4(vertex.position, 1.0));
    float4 view_pos = mul(view_matrix, world_pos);
    float4 proj_pos = mul(projection_matrix, view_pos);
    output.position = proj_pos;
    output.color = vertex.color.rgb;
    return output;
}

[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0 {
    // Combine vertex color with material base color
    float3 combined_color = input.color * base_color.rgb;

    // Apply some basic material properties for visual feedback
    // Modulate color based on metalness and roughness for demonstration
    combined_color = lerp(combined_color, combined_color * 0.5, metalness);  // Darken metallic surfaces
    combined_color = lerp(combined_color, combined_color * 1.2, 1.0 - roughness); // Brighten smooth surfaces

    return float4(combined_color, base_color.a);
}

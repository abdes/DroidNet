//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// === Bindless Rendering Contract ===
// - The engine provides a single shader-visible CBV_SRV_UAV heap.
// - The entire heap is mapped to a single descriptor table covering t0-unbounded, space0.
// - Resources are intermixed in the heap (structured indirection + geometry buffers).
// - DrawResourceIndices structured buffer occupies a dynamic heap slot; slot
//   provided in SceneConstants.bindless_indices_slot each frame.
// - Uses ResourceDescriptorHeap for direct heap access with proper type casting.
// - The root signature uses one table (t0-unbounded, space0) + direct CBVs.
//   See MainModule.cpp and CommandRecorder.cpp for details.

// Define vertex structure to match the CPU-side Vertex struct
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};


// Structured buffer for draw resource indices (matches C++ DrawResourceIndices)
struct DrawResourceIndices {
    uint vertex_buffer_index;
    uint index_buffer_index;
    uint is_indexed; // 1 for indexed meshes, 0 for non-indexed meshes
};

// Scene constants buffer (matches C++ struct layout)
cbuffer SceneConstants : register(b1) {
    float4x4 view_matrix;
    float4x4 projection_matrix;
    float3 camera_position;
    float time_seconds;
    uint frame_index;
    uint bindless_indices_slot; // dynamic slot (0xFFFFFFFF when unused)
    uint _reserved[2];
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
    uint _pad0;                             // Padding for alignment
    uint _pad1;                             // Padding for alignment
}

// Access to the bindless descriptor heap
// Modern SM 6.6+ approach using ResourceDescriptorHeap for direct heap indexing
// No resource declarations needed - ResourceDescriptorHeap provides direct access

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};


[shader("vertex")]
VSOutput VS(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Access indices buffer through dynamic slot; skip if unavailable.
    if (bindless_indices_slot == 0xFFFFFFFFu) {
        // Fallback: no geometry; output origin.
        output.position = float4(0,0,0,1);
        output.color = float3(1,0,1); // debug magenta
        return output;
    }
    StructuredBuffer<DrawResourceIndices> indices_buffer = ResourceDescriptorHeap[bindless_indices_slot];
    DrawResourceIndices indices = indices_buffer[0];

    uint vertex_buffer_index = indices.vertex_buffer_index;
    uint index_buffer_index = indices.index_buffer_index;

    uint actual_vertex_index;
    if (indices.is_indexed) {
        // For indexed rendering, get the index buffer and read the actual vertex index
        Buffer<uint> index_buffer = ResourceDescriptorHeap[index_buffer_index];
        actual_vertex_index = index_buffer[vertexID];
    } else {
        // For non-indexed rendering, use the vertex ID directly
        actual_vertex_index = vertexID;
    }

    // Access vertex data using ResourceDescriptorHeap
    StructuredBuffer<Vertex> vertex_buffer = ResourceDescriptorHeap[vertex_buffer_index];
    Vertex vertex = vertex_buffer[actual_vertex_index];

    // Apply world, view, and projection transforms
    // Object space == world space (temporary until per-item transform path).
    float4 view_pos = mul(view_matrix, float4(vertex.position, 1.0));
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

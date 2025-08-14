//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// === Bindless Rendering Contract ===
// - The engine provides a single shader-visible CBV_SRV_UAV heap.
// - The entire heap is mapped to a single descriptor table covering t0-unbounded, space0.
// - Resources are intermixed in the heap (structured indirection + geometry buffers).
// - DrawMetadata structured buffer occupies a dynamic heap slot; slot
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


// Structured buffer for per-draw metadata (matches C++ DrawMetadata)
struct DrawMetadata {
    uint vertex_buffer_index;            // Bindless index into vertex buffer table
    uint index_buffer_index;             // Bindless index into index buffer table
    uint is_indexed;                     // 0 = non-indexed, 1 = indexed
    uint instance_count;                 // Number of instances to draw

    uint transform_offset;               // Offset into transform buffer for this draw

    uint material_index;                 // Index into material constants buffer

    uint instance_metadata_buffer_index; // Bindless index into instance metadata buffer
    uint instance_metadata_offset;       // Offset into instance metadata buffer
    uint flags;                          // Bitfield: visibility, pass ID, etc.

    uint _pad0; // Padding for alignment
    uint _pad1; // Padding for alignment
    uint _pad2; // Padding for alignment
};

// Material constants structure (matches C++ MaterialConstants struct layout)
struct MaterialConstants {
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
};

// Scene constants buffer (matches C++ struct layout)
cbuffer SceneConstants : register(b1) {
    float4x4 view_matrix;
    float4x4 projection_matrix;
    float3 camera_position;
    float time_seconds;
    uint frame_index;
    uint bindless_indices_slot; // dynamic slot (0xFFFFFFFF when unused)
    uint bindless_draw_meta_data_slot; // dynamic slot for per-draw metadata
    uint bindless_transforms_slot; // dynamic slot for per-draw world matrices
    uint bindless_material_constants_slot; // dynamic slot for material constants
    uint _pad0; // Padding for alignment
    uint _pad1; // Padding for alignment
    uint _pad2; // Padding for alignment
}

// Draw index passed as a root constant (32-bit value at register b2)
cbuffer DrawIndexConstant : register(b2) {
    uint g_DrawIndex;
}

// Access to the bindless descriptor heap
// Modern SM 6.6+ approach using ResourceDescriptorHeap for direct heap indexing
// No resource declarations needed - ResourceDescriptorHeap provides direct access

// Helper function to generate a color based on draw index (temporary fix)
float3 GetDrawIndexColor() {
    // Generate a simple color based on draw index for testing
    float3 colors[8] = {
        float3(1.0, 0.0, 0.0), // Red
        float3(0.0, 1.0, 0.0), // Green
        float3(0.0, 0.0, 1.0), // Blue
        float3(1.0, 1.0, 0.0), // Yellow
        float3(1.0, 0.0, 1.0), // Magenta
        float3(0.0, 1.0, 1.0), // Cyan
        float3(1.0, 0.5, 0.0), // Orange
        float3(0.5, 0.0, 1.0)  // Purple
    };

    return colors[g_DrawIndex % 8];
}

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};


[shader("vertex")]
VSOutput VS(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Access per-draw metadata buffer through dynamic slot; skip if unavailable.
    if (bindless_indices_slot == 0xFFFFFFFFu) {
        // Fallback: no geometry; output origin.
        output.position = float4(0,0,0,1);
        output.color = float3(1,0,1); // debug magenta
        return output;
    }
    StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_indices_slot];
    // Select per-draw entry using the draw index provided via root constant
    DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

    uint vertex_buffer_index = meta.vertex_buffer_index;
    uint index_buffer_index = meta.index_buffer_index;

    uint actual_vertex_index;
    if (meta.is_indexed) {
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

    // Fetch per-draw world matrix and apply world, view, and projection transforms
    float4x4 world_matrix;
    if (bindless_transforms_slot != 0xFFFFFFFFu) {
        StructuredBuffer<float4x4> worlds = ResourceDescriptorHeap[bindless_transforms_slot];
        // Use per-draw transform offset from metadata
        world_matrix = worlds[meta.transform_offset];
    } else {
        world_matrix = float4x4(1,0,0,0,
                                0,1,0,0,
                                0,0,1,0,
                                0,0,0,1);
    }
    float4 world_pos = mul(world_matrix, float4(vertex.position, 1.0));
    float4 view_pos = mul(view_matrix, world_pos);
    float4 proj_pos = mul(projection_matrix, view_pos);
    output.position = proj_pos;
    output.color = vertex.color.rgb;
    return output;
}

[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0 {
    // Use draw index based color instead of materials (temporary fix)
    float3 draw_color = GetDrawIndexColor();

    // Combine vertex color with draw index color
    float3 combined_color = input.color * draw_color;

    return float4(combined_color, 1.0);
}

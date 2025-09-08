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
//   provided in SceneConstants.bindless_draw_meta_data_slot each frame.
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
    // --- Geometry buffers ---
    uint vertex_buffer_index;            // Bindless index into vertex buffer table
    uint index_buffer_index;             // Bindless index into index buffer table
    uint first_index;                    // Start index within index buffer
    int  base_vertex;                    // Base vertex offset

    // --- Draw configuration ---
    uint is_indexed;                     // 0 = non-indexed, 1 = indexed
    uint instance_count;                 // Number of instances
    uint index_count;                    // Number of indices (undefined if non-indexed)
    uint vertex_count;                   // Number of vertices (undefined if indexed)
    uint material_handle;                // Stable material handle (registry)

    // --- Transform & instance indirection ---
    uint transform_index;                // Index into transform arrays
    uint instance_metadata_buffer_index; // Bindless index into instance metadata buffer
    uint instance_metadata_offset;       // Offset into instance metadata buffer
    uint flags;                          // Bitfield: visibility, pass mask, etc.
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

// Constant buffer for scene-wide data. This is typically bound directly as a
// root CBV (not indexed from the descriptor heap), allowing fast and efficient
// updates for per-frame or per-draw constants.
cbuffer SceneConstants : register(b1) {
    float4x4 view_matrix;
    float4x4 projection_matrix;
    float3 camera_position;
    float time_seconds;
    uint frame_slot;
    uint64_t frame_seq_num;
    uint bindless_draw_metadata_slot; // dynamic slot for per-draw metadata
    uint bindless_transforms_slot; // dynamic slot for per-draw world matrices
    uint bindless_normal_matrices_slot; // dynamic slot for per-draw normal matrices
    uint bindless_material_constants_slot; // dynamic slot for material constants
    uint _pad0; // Padding for alignment
}

// Draw index passed as a root constant (32-bit value at register b2)
cbuffer DrawIndexConstant : register(b2) {
    uint g_DrawIndex;
}

// Access to the bindless descriptor heap
// Modern SM 6.6+ approach using ResourceDescriptorHeap for direct heap indexing
// No resource declarations needed - ResourceDescriptorHeap provides direct access

// Note: Materials are provided via a bindless StructuredBuffer<MaterialConstants>.
// We'll fetch the material for this draw using DrawMetadata.material_handle in PS.

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
};

// -----------------------------------------------------------------------------
// Normal Matrix Integration Notes
// -----------------------------------------------------------------------------
// The CPU side now publishes both world and normal matrices as float4x4 arrays
// (normal matrices stored natively as 4x4 with the unused row/column forming an
// identity extension of the 3x3 inverse-transpose). Currently this fullscreen
// triangle shader does not consume normals. If/when lighting or surface
// re-orientation is added here (e.g. screen-space effects needing reconstructed
// normals), prefer the following pattern:
//   1. Add a SceneConstants slot for bindless_normal_matrices_slot (if not
//      already present globally). Alternatively re-use bindless_transforms_slot
//      when the normal matrix is derivable in-shader (costly if many draws).
//   2. Fetch the normal matrix similarly to world_matrix:
//         StructuredBuffer<float4x4> normals =
//             ResourceDescriptorHeap[bindless_normal_matrices_slot];
//         float3x3 normal_mat = (float3x3)normals[meta.transform_index];
//   3. Apply to per-vertex/object-space normals: normalized(mul(normal_mat, n)).
//   4. Avoid recomputing inverse-transpose in shader; CPU cache guarantees it
//      is already correct even under non-uniform scale.
//   5. When adding the extra slot, keep existing order of SceneConstants fields
//      stable for binary compatibility; append new uint after existing dynamic
//      slots plus padding, adjusting padding usage accordingly.
// No code changes are required here until normals are needed.


[shader("vertex")]
VSOutput VS(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Access per-draw metadata buffer through dynamic slot; skip if unavailable.
    if (bindless_draw_metadata_slot == 0xFFFFFFFFu) {
        // Fallback: no geometry; output origin.
        output.position = float4(0,0,0,1);
        output.color = float3(1,0,1); // debug magenta
        return output;
    }
    StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_draw_metadata_slot];
    // Select per-draw entry using the draw index provided via root constant
    DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

    uint vertex_buffer_index = meta.vertex_buffer_index;
    uint index_buffer_index = meta.index_buffer_index;

    uint actual_vertex_index;
    if (meta.is_indexed) {
        // For indexed rendering, get the index buffer and read the actual vertex index
        Buffer<uint> index_buffer = ResourceDescriptorHeap[index_buffer_index];
        actual_vertex_index = index_buffer[meta.first_index + vertexID] + (uint)meta.base_vertex;
    } else {
        // For non-indexed rendering, use the vertex ID directly
        actual_vertex_index = vertexID + (uint)meta.base_vertex;
    }

    // Access vertex data using ResourceDescriptorHeap
    StructuredBuffer<Vertex> vertex_buffer = ResourceDescriptorHeap[vertex_buffer_index];
    Vertex vertex = vertex_buffer[actual_vertex_index];

    // Fetch per-draw world matrix and apply world, view, and projection transforms
    float4x4 world_matrix;
    if (bindless_transforms_slot != 0xFFFFFFFFu) {
        StructuredBuffer<float4x4> worlds = ResourceDescriptorHeap[bindless_transforms_slot];
        // Use per-draw transform offset from metadata
    world_matrix = worlds[meta.transform_index];
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
    // Default: just vertex color if materials buffer is not available
    float4 result = float4(input.color, 1.0);

    // Access per-draw metadata to find the material index for this draw
    if (bindless_draw_metadata_slot != 0xFFFFFFFFu &&
        bindless_material_constants_slot != 0xFFFFFFFFu) {
    StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_draw_metadata_slot];
        DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

        // Read material constants for this draw
        StructuredBuffer<MaterialConstants> materials = ResourceDescriptorHeap[bindless_material_constants_slot];
    MaterialConstants mat = materials[meta.material_handle];

        // Simple unlit shading: vertex color modulated by material base color
        float3 base_rgb = mat.base_color.rgb;
        float  base_a   = mat.base_color.a;
        result = float4(input.color * base_rgb, base_a);
    }

    return result;
}

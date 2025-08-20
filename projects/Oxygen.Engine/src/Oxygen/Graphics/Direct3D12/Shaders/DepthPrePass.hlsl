//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// === Bindless Rendering Contract ===
// - The engine provides a single shader-visible CBV_SRV_UAV heap.
// - The entire heap is mapped to a single descriptor table covering t0-unbounded, space0.
// - Resources are intermixed in the heap (structured indirection + geometry buffers).
// - DrawMetadata structured buffer occupies a dynamic heap slot; its slot
//   is published each frame via SceneConstants.bindless_indices_slot.
// - Uses ResourceDescriptorHeap for direct heap access with proper type casting.
// - The root signature uses one table (t0-unbounded, space0) + direct CBVs.
//   See MainModule.cpp and CommandRecorder.cpp for details.

struct VertexData
{
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

    // Per-view geometry slice
    uint first_index;
    int  base_vertex;
    uint _pad0; // Padding for alignment
};


// Access to the bindless descriptor heap (SM 6.6+)
// No resource declarations needed - ResourceDescriptorHeap provides direct access

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
    uint bindless_indices_slot; // dynamic slot (0xFFFFFFFF when absent)
    uint bindless_draw_meta_data_slot; // dynamic slot for per-draw metadata
    uint bindless_transforms_slot; // dynamic slot for per-draw world matrices
    uint bindless_material_constants_slot; // dynamic slot for material constants
    uint _pad0; // Padding for alignment
    uint _pad1; // Padding for alignment
    uint _pad2; // Padding for alignment
    uint _pad3; // Padding for alignment
}

// Draw index passed as a root constant (32-bit value at register b2)
cbuffer DrawIndexConstant : register(b2) {
    uint g_DrawIndex;
}

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

    // Access the DrawMetadata buffer using the dynamic slot from scene constants.
    if (bindless_indices_slot == 0xFFFFFFFFu) {
        // No geometry bound; output position safely (could early return). Use vertexID as trivial position.
        output.clipSpacePosition = float4(0,0,0,1);
        return output;
    }
    StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_indices_slot];
    // Use the draw index from the root constant to index into the DrawMetadata array
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
    StructuredBuffer<VertexData> vertex_buffer = ResourceDescriptorHeap[vertex_buffer_index];
    VertexData currentVertex = vertex_buffer[actual_vertex_index];

    // Fetch world matrix and transform position from object to world, then to view and projection
    float4x4 world_matrix;
    if (bindless_transforms_slot != 0xFFFFFFFFu) {
        StructuredBuffer<float4x4> worlds = ResourceDescriptorHeap[bindless_transforms_slot];
        // Use explicit offset provided in per-draw metadata
        world_matrix = worlds[meta.transform_offset];
    } else {
        world_matrix = float4x4(1,0,0,0,
                                0,1,0,0,
                                0,0,1,0,
                                0,0,0,1);
    }
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

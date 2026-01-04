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
//   is published each frame via SceneConstants.bindless_draw_meta_data_slot.
// - Uses ResourceDescriptorHeap for direct heap access with proper type casting.
// - The root signature uses one table (t0-unbounded, space0) + direct CBVs.
//   See MainModule.cpp and CommandRecorder.cpp for details.

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialConstants.hlsli"

struct VertexData
{
    float3 position;
    float3 normal;
    float2 texcoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

#define BX_VERTEX_TYPE VertexData
#include "Core/Bindless/BindlessHelpers.hlsl"

// Access to the bindless descriptor heap (SM 6.6+)
// No resource declarations needed - ResourceDescriptorHeap provides direct access

// Root constants b2 (shared root param index with engine)
// ABI layout:
//   g_DrawIndex          : per-draw index into DrawMetadata
//   g_PassConstantsIndex : per-pass payload (typically a bindless index for
//                          pass-level constants)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Output structure for the Vertex Shader
struct VS_OUTPUT_DEPTH {
    float4 clipSpacePosition : SV_POSITION;
};

// -----------------------------------------------------------------------------
// Normal Matrix Integration Notes
// -----------------------------------------------------------------------------
// Depth pre-pass currently ignores vertex normals. With future features such as
// alpha-tested foliage or per-vertex displacement needing geometric normal
// adjustment, consider binding a normal matrix buffer parallel to world
// matrices. CPU now provides normal matrices as float4x4 (inverse-transpose of
// world upper 3x3) allowing direct StructuredBuffer<float4x4> consumption.
// Recommended steps for future integration:
//   1. Introduce `uint bindless_normal_matrices_slot;` in SceneConstants after
//      existing dynamic slots (update C++ side packing & padding comments).
//   2. Fetch: `StructuredBuffer<float4x4> normals =
//         ResourceDescriptorHeap[bindless_normal_matrices_slot];`
//   3. Derive normal3x3: `(float3x3)normals[meta.transform_index];`
//   4. Use for any operations needing correct normal orientation (e.g., alpha
//      test using normal mapped geometry or conservative depth adjustments).
// Avoid recomputing inverse/transpose in shader to save ALU; rely on CPU cache.
// Until such features are introduced, no shader changes are required.


// Vertex Shader: DepthOnlyVS
// Transforms vertices to clip space for depth buffer population.
// Entry point should match the identifier used in PipelineStateDesc (e.g., "DepthOnlyVS").
[shader("vertex")]
VS_OUTPUT_DEPTH VS(uint vertexID : SV_VertexID) {
    VS_OUTPUT_DEPTH output;

    // Access the DrawMetadata buffer using the dynamic slot from scene constants.
    if (bindless_draw_metadata_slot == K_INVALID_BINDLESS_INDEX) {
        // No geometry bound; output position safely (could early return). Use vertexID as trivial position.
        output.clipSpacePosition = float4(0,0,0,1);
        return output;
    }
    StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_draw_metadata_slot];
    // Use the draw index from the root constant to index into the DrawMetadata array
    DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

    uint vertex_buffer_index = meta.vertex_buffer_index;
    uint index_buffer_index = meta.index_buffer_index;

    uint actual_vertex_index;
    if (meta.is_indexed) {
        // For indexed rendering, get the index buffer and read the actual vertex index
        StructuredBuffer<uint> index_buffer = ResourceDescriptorHeap[index_buffer_index];
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
    world_matrix = worlds[meta.transform_index];
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

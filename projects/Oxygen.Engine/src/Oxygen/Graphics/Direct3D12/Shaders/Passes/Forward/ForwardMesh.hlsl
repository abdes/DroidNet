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

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/DirectionalLightBasic.hlsli"
#include "Renderer/PositionalLightData.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialConstants.hlsli"

#include "MaterialFlags.hlsli"

// Define vertex structure to match the CPU-side Vertex struct
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

#include "Core/Bindless/BindlessHelpers.hlsl"

// Extracted systems (depend on SceneConstants + BindlessHelpers definitions)
#include "Passes/Forward/ForwardPbr.hlsli"
#include "Passes/Forward/ForwardMaterialEval.hlsli"
#include "Passes/Forward/ForwardDirectLighting.hlsli"


// Root constants b2 (shared root param index with engine)
// ABI layout:
//   g_DrawIndex          : per-draw index into DrawMetadata
//   g_PassConstantsIndex : per-pass payload (typically a bindless index for
//                          pass-level constants)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

// Root CBV b3: per-frame, frequently accessed environment data.
ConstantBuffer<EnvironmentDynamicData> g_EnvDyn : register(b3, space0);

// Access to the bindless descriptor heap
// Modern SM 6.6+ approach using ResourceDescriptorHeap for direct heap indexing
// No resource declarations needed - ResourceDescriptorHeap provides direct access

// Note: Materials are provided via a bindless StructuredBuffer<MaterialConstants>.
// We'll fetch the material for this draw using DrawMetadata.material_handle in PS.

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float3 world_bitangent : BINORMAL;
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
    if (bindless_draw_metadata_slot == K_INVALID_BINDLESS_INDEX) {
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
        StructuredBuffer<uint> index_buffer = ResourceDescriptorHeap[index_buffer_index];
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
    // Compute world-space normal: prefer uploaded normal matrices if available
    float3x3 normal_mat;
    if (bindless_normal_matrices_slot != 0xFFFFFFFFu) {
        StructuredBuffer<float4x4> normals = ResourceDescriptorHeap[bindless_normal_matrices_slot];
        normal_mat = (float3x3)normals[meta.transform_index];
    } else {
        normal_mat = (float3x3)world_matrix;
    }
    const float3x3 world_3x3 = (float3x3)world_matrix;
    float3 n_ws = SafeNormalize(mul(normal_mat, vertex.normal));
    // Tangent and bitangent are direction vectors; transform with the world
    // matrix (then orthonormalize in PS) instead of the normal matrix.
    float3 t_ws = SafeNormalize(mul(world_3x3, vertex.tangent));
    float3 b_ws = SafeNormalize(mul(world_3x3, vertex.bitangent));
    float4 view_pos = mul(view_matrix, world_pos);
    float4 proj_pos = mul(projection_matrix, view_pos);
    output.position = proj_pos;
    output.color = vertex.color.rgb;
    output.uv = vertex.texcoord;
    output.world_pos = world_pos.xyz;
    output.world_normal = n_ws;
    output.world_tangent = t_ws;
    output.world_bitangent = b_ws;
    return output;
}

[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0 {
    MaterialSurface surf = EvaluateMaterialSurface(
        input.world_pos,
        input.world_normal,
        input.world_tangent,
        input.world_bitangent,
        input.uv,
        g_DrawIndex);

    const float3 base_rgb = surf.base_rgb;
    const float  base_a   = surf.base_a;
    const float  metalness = surf.metalness;
    const float  roughness = surf.roughness;
    const float3 N = surf.N;
    const float3 V = surf.V;
    const float NdotV = saturate(dot(N, V));

    // -------------------------------------------------------------------------
    // Direct lighting (GGX specular + Lambert diffuse)
    // -------------------------------------------------------------------------

    // Base reflectance
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, base_rgb, metalness);

    float3 direct = float3(0.0, 0.0, 0.0);
    direct += AccumulateDirectionalLights(N, V, NdotV, F0, base_rgb, metalness, roughness);
    direct += AccumulatePositionalLights(input.world_pos, N, V, NdotV, F0, base_rgb, metalness, roughness);

    // Strict lighting: do not add ambient or fallback terms. If the scene has
    // no contributing lights, it should render unlit.
    const float3 shaded = direct * input.color;

    // Output to the swapchain backbuffer (RGBA8_UNORM). Encode to sRGB so
    // linear lighting reads correctly on display.
    return float4(LinearToSrgb(shaded), base_a);
}

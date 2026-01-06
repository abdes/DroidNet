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

#include "Passes/Depth/DepthPrePassConstants.hlsli"

#include "MaterialFlags.hlsli"

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
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
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
VS_OUTPUT_DEPTH VS(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    (void)instanceID;

    VS_OUTPUT_DEPTH output;
    output.position = float4(0, 0, 0, 1);
    output.uv = float2(0, 0);

    DrawMetadata meta;
    if (!BX_LoadDrawMetadata(bindless_draw_metadata_slot, g_DrawIndex, meta)) {
        return output;
    }

    const uint actual_vertex_index = BX_ResolveVertexIndex(meta, vertexID);
    const VertexData v = BX_LoadVertex(meta.vertex_buffer_index, actual_vertex_index);
    output.uv = v.texcoord;

    const float4x4 world_matrix = BX_LoadWorldMatrix(bindless_transforms_slot, meta.transform_index);
    const float4 world_pos = mul(world_matrix, float4(v.position, 1.0f));
    const float4 view_pos = mul(view_matrix, world_pos);
    output.position = mul(projection_matrix, view_pos);
    return output;
}

// Pixel Shader: MinimalPS
// This shader is minimal as no color output is needed for the depth pre-pass.
// The pipeline state should be configured with no color render targets.
// Entry point should match the identifier used in PipelineStateDesc (e.g., "MinimalPS").
[shader("pixel")]
void PS(VS_OUTPUT_DEPTH input) {
    // Depth writes are handled by fixed function; PS exists only to optionally
    // discard masked fragments so they don't contribute to the depth buffer.

    if (!BX_IsValidSlot(bindless_draw_metadata_slot) ||
        !BX_IsValidSlot(bindless_material_constants_slot)) {
        return;
    }

    StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_draw_metadata_slot];
    const DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

    StructuredBuffer<MaterialConstants> materials = ResourceDescriptorHeap[bindless_material_constants_slot];
    const MaterialConstants mat = materials[meta.material_handle];

    const bool alpha_test_enabled = (mat.flags & MATERIAL_FLAG_ALPHA_TEST) != 0u;
    if (!alpha_test_enabled) {
        return;
    }

    float alpha = mat.base_color.a;

    const bool no_texture_sampling =
        (mat.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u;

    if (!no_texture_sampling && mat.opacity_texture_index != K_INVALID_BINDLESS_INDEX) {
        const float2 uv = input.uv * mat.uv_scale + mat.uv_offset;
        Texture2D<float4> opacity_tex = ResourceDescriptorHeap[mat.opacity_texture_index];
        SamplerState samp = SamplerDescriptorHeap[0];
        const float4 texel = opacity_tex.Sample(samp, uv);
        alpha *= texel.a;
    }

    float cutoff = mat.alpha_cutoff;
    if (cutoff <= 0.0f && g_PassConstantsIndex != K_INVALID_BINDLESS_INDEX) {
        ConstantBuffer<DepthPrePassConstants> pass_constants =
            ResourceDescriptorHeap[g_PassConstantsIndex];
        cutoff = pass_constants.alpha_cutoff_default;
    }

    clip(alpha - cutoff);
}

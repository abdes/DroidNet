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
//   is published each frame via DrawFrameBindings.
// - Uses ResourceDescriptorHeap for direct heap access with proper type casting.
// - The root signature uses one table (t0-unbounded, space0) + direct CBVs.
//   See MainModule.cpp and CommandRecorder.cpp for details.

#include "Renderer/ViewConstants.hlsli"
#include "Renderer/DrawHelpers.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialShadingConstants.hlsli"

#include "Depth/DepthPrePassConstants.hlsli"

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

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VS_OUTPUT_DEPTH {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VS_OUTPUT_DEPTH VS(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    VS_OUTPUT_DEPTH output;
    output.position = float4(0, 0, 0, 1);
    output.uv = float2(0, 0);

    const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();

    DrawMetadata meta;
    if (!BX_LoadDrawMetadata(draw_bindings.draw_metadata_slot, g_DrawIndex, meta)) {
        return output;
    }

    const uint actual_vertex_index = BX_ResolveVertexIndex(meta, vertexID);
    const VertexData v = BX_LoadVertex(meta.vertex_buffer_index, actual_vertex_index);
    output.uv = v.texcoord;

    const float4x4 world_matrix = BX_LoadInstanceWorldMatrix(
        draw_bindings.transforms_slot, draw_bindings.instance_data_slot, meta, instanceID);
    const float4 world_pos = mul(world_matrix, float4(v.position, 1.0f));
    const float4 view_pos = mul(view_matrix, world_pos);
    output.position = mul(projection_matrix, view_pos);
    return output;
}

[shader("pixel")]
void PS(
#ifdef ALPHA_TEST
    VS_OUTPUT_DEPTH input
#else
    VS_OUTPUT_DEPTH
#endif
)
{
#ifdef ALPHA_TEST
    const DrawFrameBindings draw_bindings = LoadResolvedDrawFrameBindings();
    if (!BX_IsValidSlot(draw_bindings.draw_metadata_slot) ||
        !BX_IsValidSlot(draw_bindings.material_shading_constants_slot)) {
        return;
    }

    StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[draw_bindings.draw_metadata_slot];
    const DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

    StructuredBuffer<MaterialShadingConstants> materials = ResourceDescriptorHeap[draw_bindings.material_shading_constants_slot];
    const MaterialShadingConstants mat = materials[meta.material_handle];

    const bool alpha_test_enabled = (mat.flags & MATERIAL_FLAG_ALPHA_TEST) != 0u;
    if (!alpha_test_enabled) {
        return;
    }

    float alpha = mat.base_color.a;

    const bool no_texture_sampling =
        (mat.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u;

    if (!no_texture_sampling && mat.base_color_texture_index != K_INVALID_BINDLESS_INDEX) {
        const float2 uv = ApplyMaterialUv(input.uv, mat);
        Texture2D<float4> base_tex = ResourceDescriptorHeap[mat.base_color_texture_index];
        SamplerState samp = SamplerDescriptorHeap[0];
        const float4 texel = base_tex.Sample(samp, uv);
        alpha *= texel.a;
    }

    float cutoff = mat.alpha_cutoff;
    if (cutoff <= 0.0f && g_PassConstantsIndex != K_INVALID_BINDLESS_INDEX) {
        ConstantBuffer<DepthPrePassConstants> pass_constants =
            ResourceDescriptorHeap[g_PassConstantsIndex];
        cutoff = pass_constants.alpha_cutoff_default;
    }

    clip(alpha - cutoff);
#endif
}

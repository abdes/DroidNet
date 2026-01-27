//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ForwardWireframe_PS.hlsl
//! @brief Unlit wireframe pixel shader (constant color).

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialConstants.hlsli"
#include "MaterialFlags.hlsli"

// Define vertex structure to satisfy BindlessHelpers.hlsl defaults.
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float3 tangent;
    float3 bitangent;
    float4 color;
};

#include "Core/Bindless/BindlessHelpers.hlsl"

// Root constants b2 (shared root param index with engine)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
};

// Vertex shader output / Pixel shader input (must match ForwardMesh_VS.hlsl)
struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float3 world_bitangent : BINORMAL;
};

struct WireframePassConstants {
    float4 wire_color;
};

[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0 {
#ifdef ALPHA_TEST
    if (bindless_draw_metadata_slot != K_INVALID_BINDLESS_INDEX
        && bindless_material_constants_slot != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<DrawMetadata> draw_meta_buffer =
            ResourceDescriptorHeap[bindless_draw_metadata_slot];
        DrawMetadata meta = draw_meta_buffer[g_DrawIndex];

        StructuredBuffer<MaterialConstants> materials =
            ResourceDescriptorHeap[bindless_material_constants_slot];
        MaterialConstants mat = materials[meta.material_handle];

        const bool alpha_test_enabled =
            (mat.flags & MATERIAL_FLAG_ALPHA_TEST) != 0u;
        if (alpha_test_enabled) {
            const bool no_texture_sampling =
                (mat.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u;

            const float2 uv = ApplyMaterialUv(input.uv, mat);

            float alpha = 1.0f;
            if (!no_texture_sampling
                && mat.opacity_texture_index != K_INVALID_BINDLESS_INDEX) {
                Texture2D<float4> opacity_tex =
                    ResourceDescriptorHeap[mat.opacity_texture_index];
                SamplerState samp = SamplerDescriptorHeap[0];
                alpha = opacity_tex.Sample(samp, uv).a;
            }

            float cutoff = mat.alpha_cutoff;
            if (cutoff <= 0.0f) {
                cutoff = 0.5f;
            }
            clip(alpha - cutoff);
        }
    }
#endif // ALPHA_TEST

    float4 color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    if (BX_IsValidSlot(g_PassConstantsIndex)) {
        ConstantBuffer<WireframePassConstants> pc =
            ResourceDescriptorHeap[g_PassConstantsIndex];
        color = pc.wire_color;
    }

    return color;
}

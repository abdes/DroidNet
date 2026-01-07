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
    // DEBUG MODE - set to -1 for normal rendering, or 0-7 for debug:
    // 0 = world normal, 1 = tangent, 2 = bitangent, 3 = final normal,
    // 4 = NdotV, 5 = tangent length, 6 = bitangent length, 7 = NaN check
    const int DEBUG_MODE = -1; // <-- NORMAL RENDERING

    // -------------------------------------------------------------------------
    // ALPHA_TEST permutation: Alpha-test for cutout materials.
    // This must match the depth pre-pass cutout behavior so the depth buffer
    // and color pass agree on visibility. Compiled in via ALPHA_TEST define.
    // -------------------------------------------------------------------------
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

            const float2 uv = input.uv * mat.uv_scale + mat.uv_offset;

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

    float3 N = SafeNormalize(input.world_normal);
    float3 T = input.world_tangent;
    float3 B = input.world_bitangent;

    // Fix degenerate tangents at runtime
    if (dot(T, T) < 1e-6) {
        float3 axis = (abs(N.z) < 0.9) ? float3(0, 0, 1) : float3(1, 0, 0);
        T = normalize(cross(N, axis));
        B = normalize(cross(N, T));
    }
    T = SafeNormalize(T);
    if (dot(B, B) < 1e-6) {
        B = normalize(cross(N, T));
    }
    B = SafeNormalize(B);

    float3 V = SafeNormalize(camera_position - input.world_pos);

    // Debug visualization modes
    if (DEBUG_MODE == 0) {
        return float4(N * 0.5 + 0.5, 1.0);
    } else if (DEBUG_MODE == 1) {
        return float4(T * 0.5 + 0.5, 1.0);
    } else if (DEBUG_MODE == 2) {
        return float4(B * 0.5 + 0.5, 1.0);
    } else if (DEBUG_MODE == 3) {
        MaterialSurface surf = EvaluateMaterialSurface(
            input.world_pos, input.world_normal, input.world_tangent,
            input.world_bitangent, input.uv, g_DrawIndex);
        return float4(surf.N * 0.5 + 0.5, 1.0);
    } else if (DEBUG_MODE == 4) {
        float NdotV = saturate(dot(N, V));
        return float4(NdotV, NdotV, NdotV, 1.0);
    } else if (DEBUG_MODE == 5) {
        float len = length(input.world_tangent);
        return float4(len, len, len, 1.0);
    } else if (DEBUG_MODE == 6) {
        float len = length(input.world_bitangent);
        return float4(len, len, len, 1.0);
    } else if (DEBUG_MODE == 7) {
        float3 t = input.world_tangent;
        if (isnan(t.x) || isnan(t.y) || isnan(t.z)) return float4(1, 1, 0, 1);
        float len = length(t);
        if (len < 0.001) return float4(1, 0, 0, 1);
        if (len > 1.5) return float4(0, 0, 1, 1);
        return float4(0, len, 0, 1);
    }

    // Normal rendering path
    MaterialSurface surf = EvaluateMaterialSurface(
        input.world_pos,
        input.world_normal,
        input.world_tangent,
        input.world_bitangent,
        input.uv,
        g_DrawIndex);

    const float3 base_rgb = surf.base_rgb;
    const float  metalness = surf.metalness;
    const float  roughness = surf.roughness;
    N = surf.N;
    V = surf.V;
    const float NdotV = saturate(dot(N, V));

    // Direct lighting (GGX specular + Lambert diffuse)
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, base_rgb, metalness);

    float3 direct = float3(0.0, 0.0, 0.0);
    direct += AccumulateDirectionalLights(N, V, NdotV, F0, base_rgb, metalness, roughness);
    direct += AccumulatePositionalLights(input.world_pos, N, V, NdotV, F0, base_rgb, metalness, roughness);

    // Ambient term
    const float ambient_strength = 0.02f;
    const float3 ambient = base_rgb * (1.0f - metalness) * ambient_strength;
    const float3 shaded = (direct + ambient) * input.color;

    // Output alpha: for ALPHA_TEST path use 1.0 (already clipped), else use
    // base_a for potential transparent blending in TransparentPass.
#ifdef ALPHA_TEST
    return float4(LinearToSrgb(shaded), 1.0f);
#else
    return float4(LinearToSrgb(shaded), surf.base_a);
#endif
}

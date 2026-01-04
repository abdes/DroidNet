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


// Root constants b2 (shared root param index with engine)
// ABI layout:
//   g_DrawIndex          : per-draw index into DrawMetadata
//   g_PassConstantsIndex : per-pass payload (typically a bindless index for
//                          pass-level constants)
cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

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
// PBR helpers (metallic-roughness, GGX)
// -----------------------------------------------------------------------------

static const float kPi = 3.14159265359;

float3 SrgbToLinear(float3 c)
{
    // IEC 61966-2-1:1999
    c = saturate(c);
    const float3 lo = c / 12.92;
    const float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return lerp(hi, lo, step(c, 0.04045));
}

float3 LinearToSrgb(float3 c)
{
    c = max(c, 0.0);
    const float3 lo = c * 12.92;
    const float3 hi = 1.055 * pow(c, 1.0 / 2.4) - 0.055;
    return saturate(lerp(hi, lo, step(c, 0.0031308)));
}

float DistributionGGX(float NdotH, float roughness)
{
    const float a = max(roughness * roughness, 1e-4);
    const float a2 = a * a;
    const float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * denom * denom, 1e-6);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    // UE4-style k for direct lighting.
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return NdotV / max(NdotV * (1.0 - k) + k, 1e-6);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    const float ggxV = GeometrySchlickGGX(NdotV, roughness);
    const float ggxL = GeometrySchlickGGX(NdotL, roughness);
    return ggxV * ggxL;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float3 DecodeNormalTS(float3 n)
{
    // Normal maps are typically stored as [0..1]; remap to [-1..1].
    n = n * 2.0 - 1.0;
    // Re-normalize after remap.
    return normalize(n);
}

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
    float3 n_ws = normalize(mul(normal_mat, vertex.normal));
    float3 t_ws = normalize(mul(normal_mat, vertex.tangent));
    float3 b_ws = normalize(mul(normal_mat, vertex.bitangent));
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
    // Hardcoded directional light (POC)
    // Light traveling towards +X (Right), +Y (Back), -Z (Down)
    // This illuminates faces facing -X (Left), -Y (Forward), and +Z (Up).
    // Given the engine is Z-UP and Forward is -Y, a camera at Y = -10 looking
    // towards the origin is looking "Back" (+Y). This light travels with that
    // view to illuminate the "Front" and "Top" of objects.
    const float3 light_dir_ws = normalize(float3(0.4, 1.0, -0.7));
    const float3 light_color  = float3(1.0, 1.0, 1.0);
    const float  light_intensity = 1.0;
    const float3 ambient_color = float3(0.2, 0.2, 0.2);

    // Material defaults
    float3 base_rgb = float3(1.0, 1.0, 1.0);
    float  base_a   = 1.0;
    float  metalness = 0.0;
    float  roughness = 1.0;
    float  ao = 1.0;

    float3 N = normalize(input.world_normal);
    float3 V = normalize(camera_position - input.world_pos);

    // Use a directional light coming *from* light_dir_ws.
    float3 L = normalize(-light_dir_ws);
    float3 H = normalize(V + L);

    // If material constants available, modulate by base_color
    if (bindless_draw_metadata_slot != 0xFFFFFFFFu &&
        bindless_material_constants_slot != 0xFFFFFFFFu) {
        StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_draw_metadata_slot];
        DrawMetadata meta = draw_meta_buffer[g_DrawIndex];
        StructuredBuffer<MaterialConstants> materials = ResourceDescriptorHeap[bindless_material_constants_slot];
        MaterialConstants mat = materials[meta.material_handle];
        base_rgb = mat.base_color.rgb;
        base_a   = mat.base_color.a;
        metalness = saturate(mat.metalness);
        roughness = saturate(mat.roughness);
        ao = saturate(mat.ambient_occlusion);

        const float2 uv = input.uv * mat.uv_scale + mat.uv_offset;

        const bool no_texture_sampling =
            (mat.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u;

        if (!no_texture_sampling && mat.base_color_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> base_tex = ResourceDescriptorHeap[mat.base_color_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            float4 texel = base_tex.Sample(samp, uv);
            // Base-color (albedo) textures are authored in sRGB. Since the engine
            // currently binds RGBA8 as UNORM (non-sRGB) and renders to a non-sRGB
            // swapchain/backbuffer, we must manually convert.
            base_rgb *= SrgbToLinear(texel.rgb);
            base_a   *= texel.a;
        }

        // Normal map (tangent-space)
        if (!no_texture_sampling && mat.normal_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> nrm_tex = ResourceDescriptorHeap[mat.normal_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            float3 n_ts = DecodeNormalTS(nrm_tex.Sample(samp, uv).xyz);
            n_ts.xy *= mat.normal_scale;
            n_ts = normalize(float3(n_ts.xy, max(n_ts.z, 1e-4)));

            float3 T = normalize(input.world_tangent);
            float3 B = normalize(input.world_bitangent);
            float3 NN = normalize(input.world_normal);

            // Orthonormalize TBN to reduce artifacts.
            T = normalize(T - NN * dot(NN, T));
            B = normalize(cross(NN, T));

            float3x3 TBN = float3x3(T, B, NN);
            N = normalize(mul(TBN, n_ts));
        }

        // Scalar maps (if present)
        if (!no_texture_sampling && mat.metallic_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> m_tex = ResourceDescriptorHeap[mat.metallic_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            metalness *= saturate(m_tex.Sample(samp, uv).r);
        }
        if (!no_texture_sampling && mat.roughness_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> r_tex = ResourceDescriptorHeap[mat.roughness_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            roughness *= saturate(r_tex.Sample(samp, uv).r);
        }
        if (!no_texture_sampling && mat.ambient_occlusion_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> ao_tex = ResourceDescriptorHeap[mat.ambient_occlusion_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            ao *= saturate(ao_tex.Sample(samp, uv).r);
        }
    }

    // -------------------------------------------------------------------------
    // Direct lighting (GGX specular + Lambert diffuse)
    // -------------------------------------------------------------------------

    const float NdotL = saturate(dot(N, L));
    const float NdotV = saturate(dot(N, V));
    const float NdotH = saturate(dot(N, H));
    const float VdotH = saturate(dot(V, H));

    // Base reflectance
    float3 F0 = float3(0.04, 0.04, 0.04);
    F0 = lerp(F0, base_rgb, metalness);

    const float3 F = FresnelSchlick(VdotH, F0);
    const float  D = DistributionGGX(NdotH, roughness);
    const float  G = GeometrySmith(NdotV, NdotL, roughness);

    const float3 numerator = D * G * F;
    const float  denom = max(4.0 * NdotV * NdotL, 1e-6);
    const float3 specular = numerator / denom;

    const float3 kS = F;
    const float3 kD = (1.0 - kS) * (1.0 - metalness);
    const float3 diffuse = kD * base_rgb / kPi;

    const float3 direct = (diffuse + specular) * light_color * light_intensity * NdotL;

    // Simple ambient terms.
    // Keep ambient conservative so the directional light still reads clearly.
    // Diffuse ambient should respect metalness (metals have ~no diffuse term).
    const float3 ambient_diffuse = ambient_color * (kD * base_rgb) * ao;

    // Specular ambient approximation (NOT real IBL): keep it conservative but
    // strong enough that fully metallic materials don't go to near-black.
    const float3 F_amb = FresnelSchlick(NdotV, F0);
    const float amb_spec_scale = lerp(0.02, 0.25, metalness);
    const float amb_view = 0.25 + 0.75 * NdotV;
    const float3 ambient_specular
        = ambient_color * F_amb * (1.0 - roughness) * amb_spec_scale * amb_view;

    const float3 ambient = ambient_diffuse + (ambient_specular * ao);

    const float3 shaded = (ambient + direct) * input.color;

    // Output to the swapchain backbuffer (RGBA8_UNORM). Encode to sRGB so
    // linear lighting reads correctly on display.
    return float4(LinearToSrgb(shaded), base_a);
}

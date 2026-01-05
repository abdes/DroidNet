#ifndef OXYGEN_PASSES_FORWARD_FORWARDMATERIALEVAL_HLSLI
#define OXYGEN_PASSES_FORWARD_FORWARDMATERIALEVAL_HLSLI

#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialConstants.hlsli"
#include "MaterialFlags.hlsli"
#include "Passes/Forward/ForwardPbr.hlsli"

struct MaterialSurface
{
    float3 base_rgb;
    float  base_a;
    float  metalness;
    float  roughness;
    float  ao;

    float3 N;
    float3 V;
};

MaterialSurface EvaluateMaterialSurface(
    float3 world_pos,
    float3 world_normal,
    float3 world_tangent,
    float3 world_bitangent,
    float2 uv0,
    uint   draw_index)
{
    MaterialSurface s;

    // Defaults
    s.base_rgb  = float3(1.0, 1.0, 1.0);
    s.base_a    = 1.0;
    s.metalness = 0.0;
    s.roughness = 1.0;
    s.ao        = 1.0;

    s.N = SafeNormalize(world_normal);
    s.V = SafeNormalize(camera_position - world_pos);

    if (bindless_draw_metadata_slot != 0xFFFFFFFFu &&
        bindless_material_constants_slot != 0xFFFFFFFFu) {
        StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_draw_metadata_slot];
        DrawMetadata meta = draw_meta_buffer[draw_index];

        StructuredBuffer<MaterialConstants> materials = ResourceDescriptorHeap[bindless_material_constants_slot];
        MaterialConstants mat = materials[meta.material_handle];

        s.base_rgb  = mat.base_color.rgb;
        s.base_a    = mat.base_color.a;
        s.metalness = saturate(mat.metalness);
        s.roughness = saturate(mat.roughness);
        s.ao        = saturate(mat.ambient_occlusion);

        const float2 uv = uv0 * mat.uv_scale + mat.uv_offset;

        const bool no_texture_sampling =
            (mat.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u;

        if (!no_texture_sampling && mat.base_color_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> base_tex = ResourceDescriptorHeap[mat.base_color_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            float4 texel = base_tex.Sample(samp, uv);
            // Base-color textures are authored in sRGB. Since the engine binds
            // RGBA8 as UNORM (non-sRGB) and renders to a non-sRGB backbuffer,
            // we must manually convert.
            s.base_rgb *= SrgbToLinear(texel.rgb);
            s.base_a   *= texel.a;
        }

        // Normal map (tangent-space)
        if (!no_texture_sampling && mat.normal_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> nrm_tex = ResourceDescriptorHeap[mat.normal_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            float3 n_ts = DecodeNormalTS(nrm_tex.Sample(samp, uv).xyz);
            n_ts.xy *= mat.normal_scale;
            n_ts = SafeNormalize(float3(n_ts.xy, max(n_ts.z, 1e-4)));

            float3 T = SafeNormalize(world_tangent);
            float3 B_in = world_bitangent;
            float3 NN = SafeNormalize(world_normal);

            // Orthonormalize TBN to reduce artifacts.
            T = T - NN * dot(NN, T);
            if (dot(T, T) <= 1e-16) {
                // Degenerate tangent (e.g., tangent parallel to normal). Choose a
                // stable orthogonal axis so we don't introduce NaNs.
                const float3 axis = (abs(NN.z) < 0.999) ? float3(0.0, 0.0, 1.0)
                                                        : float3(0.0, 1.0, 0.0);
                T = cross(axis, NN);
            }
            T = SafeNormalize(T);

            // Preserve TBN handedness (mirrored UVs) when possible.
            const float3 B_from_cross = cross(NN, T);
            const float handedness = (dot(B_from_cross, B_in) < 0.0) ? -1.0 : 1.0;
            const float3 B = SafeNormalize(B_from_cross * handedness);

            float3x3 TBN = float3x3(T, B, NN);
            s.N = SafeNormalize(mul(TBN, n_ts));
        }

        // Scalar maps
        if (!no_texture_sampling && mat.metallic_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> m_tex = ResourceDescriptorHeap[mat.metallic_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            s.metalness *= saturate(m_tex.Sample(samp, uv).r);
        }
        if (!no_texture_sampling && mat.roughness_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> r_tex = ResourceDescriptorHeap[mat.roughness_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            s.roughness *= saturate(r_tex.Sample(samp, uv).r);
        }
        if (!no_texture_sampling && mat.ambient_occlusion_texture_index != 0xFFFFFFFFu) {
            Texture2D<float4> ao_tex = ResourceDescriptorHeap[mat.ambient_occlusion_texture_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            s.ao *= saturate(ao_tex.Sample(samp, uv).r);
        }
    }

    return s;
}

#endif // OXYGEN_PASSES_FORWARD_FORWARDMATERIALEVAL_HLSLI

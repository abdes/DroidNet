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
    // Fallback for degenerate normals from vertex data
    if (dot(s.N, s.N) < 0.5) {
        s.N = float3(0.0, 1.0, 0.0);
    }
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

            float3 NN = SafeNormalize(world_normal);
            // Fallback if geometric normal is degenerate
            if (dot(NN, NN) < 0.5) {
                NN = float3(0.0, 1.0, 0.0);
            }

            float3 T = SafeNormalize(world_tangent);
            float3 B_in = world_bitangent;

            // Orthonormalize TBN to reduce artifacts.
            T = T - NN * dot(NN, T);
            if (dot(T, T) <= 1e-6) {
                // Degenerate tangent (e.g., tangent parallel to normal). Choose a
                // stable orthogonal axis so we don't introduce NaNs.
                const float3 axis = (abs(NN.y) > 0.9) ? float3(1.0, 0.0, 0.0)
                                                      : float3(0.0, 1.0, 0.0);
                T = cross(NN, axis);
            }
            T = SafeNormalize(T);
            // Double-check T is valid
            if (dot(T, T) < 0.5) {
                T = float3(1.0, 0.0, 0.0);
            }

            // Preserve TBN handedness (mirrored UVs) when possible.
            const float3 B_from_cross = cross(NN, T);
            const float handedness = (dot(B_from_cross, B_in) < 0.0) ? -1.0 : 1.0;
            float3 B = SafeNormalize(B_from_cross * handedness);
            if (dot(B, B) < 0.5) {
                B = cross(NN, T);
            }

            // Transform tangent-space normal to world space.
            // TBN basis vectors (T, B, NN) are columns of the transform matrix.
            // n_ws = T * n_ts.x + B * n_ts.y + NN * n_ts.z
            float3 perturbed_N = T * n_ts.x + B * n_ts.y + NN * n_ts.z;
            perturbed_N = SafeNormalize(perturbed_N);

            // Validate the perturbed normal - if it's zero or points sharply away
            // from the geometric normal (> 90 degrees), fall back to geometric normal
            if (dot(perturbed_N, perturbed_N) < 0.5 || dot(perturbed_N, NN) < 0.0) {
                perturbed_N = NN;
            }
            s.N = perturbed_N;
        }

        // Scalar maps
        const bool is_orm_packed = (mat.flags & MATERIAL_FLAG_GLTF_ORM_PACKED) != 0u;
        bool use_orm_packed = is_orm_packed;

        // If the packed flag isn't set, still support the common case where
        // ORM is authored as a single texture wired into all three slots.
        uint orm_tex_index = 0xFFFFFFFFu;
        if (!use_orm_packed) {
            const uint idx = mat.metallic_texture_index;
            if (idx != 0xFFFFFFFFu && idx == mat.roughness_texture_index
                && idx == mat.ambient_occlusion_texture_index) {
                use_orm_packed = true;
                orm_tex_index = idx;
            }
        } else {
            orm_tex_index = (mat.roughness_texture_index != 0xFFFFFFFFu)
                ? mat.roughness_texture_index
                : ((mat.metallic_texture_index != 0xFFFFFFFFu)
                    ? mat.metallic_texture_index
                    : mat.ambient_occlusion_texture_index);
        }

        if (!no_texture_sampling && use_orm_packed && orm_tex_index != 0xFFFFFFFFu) {
            Texture2D<float4> orm_tex = ResourceDescriptorHeap[orm_tex_index];
            SamplerState samp = SamplerDescriptorHeap[0];
            const float3 orm = saturate(orm_tex.Sample(samp, uv).rgb);

            // glTF ORM packing:
            // - AO:       R
            // - Roughness:G
            // - Metalness:B
            s.roughness *= orm.g;
            s.metalness *= orm.b;

            // Prefer dedicated AO map if provided separately.
            if (mat.ambient_occlusion_texture_index != 0xFFFFFFFFu
                && mat.ambient_occlusion_texture_index != orm_tex_index) {
                Texture2D<float4> ao_tex = ResourceDescriptorHeap[mat.ambient_occlusion_texture_index];
                s.ao *= saturate(ao_tex.Sample(samp, uv).r);
            } else {
                s.ao *= orm.r;
            }
        } else {
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
    }

    return s;
}

#endif // OXYGEN_PASSES_FORWARD_FORWARDMATERIALEVAL_HLSLI

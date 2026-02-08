//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! @file ForwardMesh_PS.hlsl
//! @brief Pixel shader for Forward+ physically based rendering.

#include "Renderer/SceneConstants.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Atmosphere/AerialPerspective.hlsli"
#include "Renderer/DirectionalLightBasic.hlsli"
#include "Renderer/PositionalLightData.hlsli"
#include "Renderer/DrawMetadata.hlsli"
#include "Renderer/MaterialConstants.hlsli"
#include "Renderer/Vertex.hlsli"

#include "MaterialFlags.hlsli"

#include "Core/Bindless/BindlessHelpers.hlsl"
#include "Forward/ForwardPbr.hlsli"
#include "Forward/ForwardMaterialEval.hlsli"
#include "Forward/ForwardDirectLighting.hlsli"
#include "Lighting/ClusterLookup.hlsli"

float3 EnvBrdfApprox(float3 F0, float roughness, float NoV)
{
    const float4 c0 = float4(-1.0, -0.0275, -0.572, 0.022);
    const float4 c1 = float4(1.0, 0.0425, 1.04, -0.04);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    float2 AB = float2(-1.04, 1.04) * a004 + r.zw;
    return F0 * AB.x + AB.y;
}

cbuffer RootConstants : register(b2, space0) {
    uint g_DrawIndex;
    uint g_PassConstantsIndex;
}

struct VSOutput {
    float4 position : SV_POSITION;
    float3 color : COLOR;
    float2 uv : TEXCOORD0;
    float3 world_pos : TEXCOORD1;
    float3 world_normal : NORMAL;
    float3 world_tangent : TANGENT;
    float3 world_bitangent : BINORMAL;
    bool is_front_face : SV_IsFrontFace;
};

[shader("pixel")]
float4 PS(VSOutput input) : SV_Target0 {
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

#ifdef ALPHA_TEST
    if (bindless_draw_metadata_slot != K_INVALID_BINDLESS_INDEX && bindless_material_constants_slot != K_INVALID_BINDLESS_INDEX) {
        StructuredBuffer<DrawMetadata> draw_meta_buffer = ResourceDescriptorHeap[bindless_draw_metadata_slot];
        DrawMetadata meta = draw_meta_buffer[g_DrawIndex];
        StructuredBuffer<MaterialConstants> materials = ResourceDescriptorHeap[bindless_material_constants_slot];
        MaterialConstants mat = materials[meta.material_handle];
        if ((mat.flags & MATERIAL_FLAG_ALPHA_TEST) != 0u) {
            const float2 uv = ApplyMaterialUv(input.uv, mat);
            float alpha = 1.0f;
            if (!((mat.flags & MATERIAL_FLAG_NO_TEXTURE_SAMPLING) != 0u) && mat.opacity_texture_index != K_INVALID_BINDLESS_INDEX) {
                Texture2D<float4> opacity_tex = ResourceDescriptorHeap[mat.opacity_texture_index];
                alpha = opacity_tex.Sample(linear_sampler, uv).a;
            }
            clip(alpha - (mat.alpha_cutoff <= 0.0f ? 0.5f : mat.alpha_cutoff));
        }
    }
#endif

    //=== Normal PBR Path ===
    MaterialSurface surf = EvaluateMaterialSurface(input.world_pos, input.world_normal, input.world_tangent, input.world_bitangent, input.uv, g_DrawIndex, input.is_front_face);
    const float3 base_rgb = surf.base_rgb * input.color;
    const float3 N = surf.N;
    const float3 V = surf.V;
    const float NdotV = saturate(dot(N, V));
    float3 F0 = lerp(float3(0.04, 0.04, 0.04), base_rgb, surf.metalness);

    float3 direct = AccumulateDirectionalLights(N, V, NdotV, F0, base_rgb, surf.metalness, surf.roughness);
    direct += AccumulatePositionalLightsClustered(input.world_pos, input.position.xy, max(-mul(view_matrix, float4(input.world_pos, 1.0)).z, 0.0), N, V, NdotV, F0, base_rgb, surf.metalness, surf.roughness);

    float3 ibl_diffuse = 0.0f;
    float3 ibl_specular = 0.0f;
    EnvironmentStaticData env_data;
    if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data) && env_data.sky_light.enabled) {
        uint slot = env_data.sky_light.cubemap_slot;
        if (slot == K_INVALID_BINDLESS_INDEX) slot = env_data.sky_sphere.cubemap_slot;
        if (slot != K_INVALID_BINDLESS_INDEX) {
            TextureCube<float4> sky_cube = ResourceDescriptorHeap[slot];
            const float3 cube_R = CubemapSamplingDirFromOxygenWS(reflect(-V, N));
            const float3 cube_N = CubemapSamplingDirFromOxygenWS(N);
            if (env_data.sky_light.irradiance_map_slot != K_INVALID_BINDLESS_INDEX && env_data.sky_light.prefilter_map_slot != K_INVALID_BINDLESS_INDEX) {
                TextureCube<float4> irr_map = ResourceDescriptorHeap[env_data.sky_light.irradiance_map_slot];
                TextureCube<float4> pref_map = ResourceDescriptorHeap[env_data.sky_light.prefilter_map_slot];
                ibl_diffuse = irr_map.SampleLevel(linear_sampler, cube_N, 0.0).rgb * env_data.sky_light.tint_rgb * env_data.sky_light.diffuse_intensity;
                ibl_specular = pref_map.SampleLevel(linear_sampler, cube_R, (float)env_data.sky_light.prefilter_max_mip * surf.roughness).rgb * env_data.sky_light.tint_rgb * env_data.sky_light.specular_intensity;
            } else {
                float max_m = (slot == env_data.sky_sphere.cubemap_slot) ? (float)env_data.sky_sphere.cubemap_max_mip : (float)env_data.sky_light.cubemap_max_mip;
                ibl_specular = sky_cube.SampleLevel(linear_sampler, cube_R, max_m * surf.roughness).rgb * env_data.sky_light.tint_rgb * env_data.sky_light.intensity * env_data.sky_light.specular_intensity;
                ibl_diffuse = sky_cube.SampleLevel(linear_sampler, cube_N, max_m).rgb * env_data.sky_light.tint_rgb * env_data.sky_light.intensity * env_data.sky_light.diffuse_intensity;
            }
        }
    }

    float3 ibl_spec_term = ibl_specular * EnvBrdfApprox(F0, surf.roughness, NdotV);
    #if !defined(SKIP_BRDF_LUT)
    if (env_data.sky_light.brdf_lut_slot != K_INVALID_BINDLESS_INDEX) {
        Texture2D<float2> brdf_lut = ResourceDescriptorHeap[env_data.sky_light.brdf_lut_slot];
        uint lut_w = 1u;
        uint lut_h = 1u;
        brdf_lut.GetDimensions(lut_w, lut_h);
        const float2 lut_size = float2(max(lut_w, 1u), max(lut_h, 1u));
        const float2 uv_raw = saturate(float2(NdotV, surf.roughness));
        const float2 uv = (uv_raw * (lut_size - 1.0) + 0.5) / lut_size;
        float2 brdf = brdf_lut.SampleLevel(linear_sampler, uv, 0.0).rg;
        if (!any(isnan(brdf)) && max(brdf.x, brdf.y) > 1e-5) ibl_spec_term = ibl_specular * (F0 * brdf.x + brdf.y);
    }
    #endif

    float3 final_color = direct
        + (ibl_spec_term + ibl_diffuse * base_rgb * (1.0f - surf.metalness))
        + surf.emissive;

    if (LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data)
        && (env_data.fog.enabled || ShouldUseLutAerialPerspective(env_data.atmosphere))) {
        float3 s_dir = normalize(GetSunDirectionWS());
        if (EnvironmentDynamicData.sun_enabled == 0 && !IsOverrideSunEnabled()) {
            s_dir = float3(0.5, 0.707, 0.5);
        }

        const AerialPerspectiveResult ap = ComputeAerialPerspective(
            env_data,
            input.world_pos,
            camera_position,
            s_dir);
        final_color = ApplyAerialPerspective(final_color, ap);
    }

#ifdef OXYGEN_HDR_OUTPUT
    return float4(final_color, surf.base_a);
#else
    final_color *= GetExposure();
    return float4(LinearToSrgb(final_color), surf.base_a);
#endif
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_GBUFFERHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_GBUFFERHELPERS_HLSLI

#include "../Shared/PackUnpack.hlsli"

#include "GBufferLayout.hlsli"
#include "SceneTextures.hlsli"

struct GBufferData
{
    float3 world_normal;
    float3 base_color;
    float metallic;
    float specular;
    float roughness;
    float ambient_occlusion;
    uint shading_model;
    float4 custom_data;
};

static inline float4 EncodeGBufferNormal(float3 world_normal)
{
    const float2 encoded = OctahedronEncode(normalize(world_normal));
    return float4(encoded * 0.5f + 0.5f, 0.0f, 1.0f);
}

static inline float3 DecodeGBufferNormal(float4 gbuffer_normal)
{
    const float2 encoded = gbuffer_normal.xy * 2.0f - 1.0f;
    return OctahedronDecode(encoded);
}

static inline float4 EncodeGBufferMaterial(
    float metallic, float specular, float roughness, uint shading_model)
{
    return float4(
        metallic, specular, roughness, (float(shading_model) + 0.5f) / 255.0f);
}

static inline void DecodeGBufferMaterial(float4 gbuffer_material,
    out float metallic, out float specular, out float roughness,
    out uint shading_model)
{
    metallic = gbuffer_material.x;
    specular = gbuffer_material.y;
    roughness = gbuffer_material.z;
    shading_model = uint(gbuffer_material.w * 255.0f);
}

static inline float4 EncodeGBufferBaseColor(float3 base_color, float ambient_occlusion)
{
    return float4(base_color, ambient_occlusion);
}

static inline void DecodeGBufferBaseColor(
    float4 gbuffer_base_color, out float3 base_color, out float ambient_occlusion)
{
    base_color = gbuffer_base_color.xyz;
    ambient_occlusion = gbuffer_base_color.w;
}

static inline GBufferData ReadGBuffer(float2 uv, SceneTextureBindingData bindings)
{
    GBufferData data;

    const float4 gbuffer_normal = SampleGBuffer(GBUFFER_NORMAL, uv, bindings);
    const float4 gbuffer_material = SampleGBuffer(GBUFFER_MATERIAL, uv, bindings);
    const float4 gbuffer_base_color = SampleGBuffer(GBUFFER_BASE_COLOR, uv, bindings);
    data.custom_data = SampleGBuffer(GBUFFER_CUSTOM_DATA, uv, bindings);

    data.world_normal = DecodeGBufferNormal(gbuffer_normal);
    DecodeGBufferMaterial(gbuffer_material, data.metallic, data.specular,
        data.roughness, data.shading_model);
    DecodeGBufferBaseColor(
        gbuffer_base_color, data.base_color, data.ambient_occlusion);
    return data;
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_CONTRACTS_GBUFFERHELPERS_HLSLI

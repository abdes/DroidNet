//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_IBL_EVALUATION_HLSLI
#define OXYGEN_VORTEX_IBL_EVALUATION_HLSLI

#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/IblProductMetadata.hlsli"

#include "Vortex/Shared/IblSampling.hlsli"

static float3 EvaluateIblSpecularSplit(float3 filtered_radiance,
    float3 f0, float2 integrated_brdf)
{
    const float f90 = saturate(50.0 * f0.g);
    return filtered_radiance * (f0 * integrated_brdf.x + f90 * integrated_brdf.y);
}

static float2 SampleIblBrdf(uint descriptor, float normal_dot_view, float roughness)
{
    Texture2D<float2> lookup = ResourceDescriptorHeap[descriptor];
    SamplerState linear_clamp = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
    return lookup.SampleLevel(linear_clamp,
        float2(saturate(normal_dot_view), saturate(roughness)), 0.0);
}

struct IblSurfaceLighting
{
    float3 diffuse;
    float3 specular;
};

// Scene-linear result. The surface pass applies its view exposure once.
// Normal mapping and sidedness are resolved by the caller's material path.
static IblSurfaceLighting EvaluateSkyIbl(GpuSkyLightParams light, uint brdf_srv,
    float3 normal_ws, float3 view_ws, float3 base_color, float metallic,
    float roughness, float material_occlusion, float3 f0)
{
    IblSurfaceLighting result = (IblSurfaceLighting)0;
    if (light.enabled == 0u || !BX_IN_GLOBAL_SRV(light.product_metadata_srv)
        || !BX_IN_GLOBAL_SRV(light.diffuse_sh_slot)
        || !BX_IN_TEXTURES(light.prefilter_map_slot) || !BX_IN_TEXTURES(brdf_srv))
        return result;
    StructuredBuffer<IblProductMetadata> metadata = ResourceDescriptorHeap[light.product_metadata_srv];
    const IblProductMetadata generation = metadata[0];
    if (!IsIblProductReady(generation, light.ibl_generation)) return result;
    const float normal_squared = dot(normal_ws, normal_ws);
    const float view_squared = dot(view_ws, view_ws);
    if (!(normal_squared > 1.0e-8) || !(view_squared > 1.0e-8)) return result;
    const float3 n = normal_ws * rsqrt(normal_squared);
    const float3 v = view_ws * rsqrt(view_squared);
    const float3 gain = light.tint_rgb * light.radiance_scale * generation.source_radiance_scale;
    if (light.diffuse_intensity > 0.0) {
        StructuredBuffer<float4> sh = ResourceDescriptorHeap[light.diffuse_sh_slot];
        result.diffuse = EvaluatePackedSkyDiffuseSh(sh, n) * base_color
            * (1.0 - saturate(metallic)) * saturate(material_occlusion)
            * gain * light.diffuse_intensity;
    }
    if (light.specular_intensity > 0.0) {
        TextureCube<float4> prefiltered = ResourceDescriptorHeap[light.prefilter_map_slot];
        SamplerState linear_clamp = SamplerDescriptorHeap[VORTEX_SAMPLER_LINEAR_CLAMP];
        const float mip = IblRoughnessToMip(saturate(roughness), light.prefilter_max_mip);
        const float3 reflection = CubemapSamplingDirFromOxygenWS(reflect(-v, n));
        const float3 filtered = prefiltered.SampleLevel(linear_clamp, reflection, mip).rgb;
        result.specular = EvaluateIblSpecularSplit(filtered, f0,
            SampleIblBrdf(brdf_srv, dot(n, v), roughness)) * gain * light.specular_intensity;
    }
    return result;
}

#endif

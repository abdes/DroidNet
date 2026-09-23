//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SHARED_BRDFCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SHARED_BRDFCOMMON_HLSLI

#include "Vortex/Shared/Math.hlsli"
#include "Vortex/Contracts/Lighting/LightingFrameBindings.hlsli"

static const float kVortexDefaultSpecular = 0.5f;
static const float kVortexMinimumRoughness = 0.045f;

// Model 2: one compact, hardware-filtered directional-energy lookup. The square-
// root view coordinate spends the same 32 texels more effectively at grazing.
static float2 SampleGgxEnergyTexture(uint descriptor, float mu, float roughness)
{
    Texture2D<float2> texture = ResourceDescriptorHeap[descriptor];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    const float2 coordinate = float2(sqrt(saturate(mu)),
        saturate((roughness - kVortexMinimumRoughness) / (1.0 - kVortexMinimumRoughness)));
    return texture.SampleLevel(linear_sampler, (coordinate * 31.0 + 0.5) / 32.0, 0);
}

static float GgxDistribution(float3 N, float3 H, float roughness)
{
    const float r = max(roughness, kVortexMinimumRoughness);
    const float a2 = r * r * r * r;
    const float3 tangent = cross(N, H);
    const float nh = saturate(dot(N, H));
    const float denominator = dot(tangent, tangent) + a2 * nh * nh;
    return a2 / (PI * denominator * denominator);
}

struct GgxDirectLobes
{
    // Each lobe includes the receiver cosine exactly once.
    float3 single_scattering;
    float3 multiple_scattering;
    float3 diffuse;
};

struct GgxDirectContext
{
    float nv;
    float roughness;
    float alpha_squared;
    float view_root;
    float3 F0;
    float3 specular_scale;
    float3 diffuse_scale;
    uint state;
};

// View-dependent multiple-scattering compensation (Turquin / Fdez-Aguera).
// The scalar diffuse transmission preserves base hue under colored specular.
static float3 GgxEnergyScale(float3 F0, float2 energy)
{
    return 1.0 + F0 * ((1.0 - energy.x) / energy.x);
}

static float GgxDiffuseTransmission(float3 reflected)
{
    return saturate(1.0 - dot(reflected, float3(0.2126, 0.7152, 0.0722)));
}

static GgxDirectContext PrepareGgxDirect(float3 N, float3 V, float3 F0,
    float3 rho, float roughness, LightingFrameBindings lighting)
{
    GgxDirectContext context = (GgxDirectContext)0;
    context.nv = saturate(dot(N, V));
    if (context.nv <= 0.0) return context;
    context.state = 2u;
    if (lighting.brdf_model_revision != 2u || !BX_IN_TEXTURES(lighting.brdf_energy_srv))
        return context;
    context.state = 1u;
    context.roughness = max(roughness, kVortexMinimumRoughness);
    const float alpha = context.roughness * context.roughness;
    context.alpha_squared = alpha * alpha;
    context.view_root = sqrt(context.nv * context.nv
        + context.alpha_squared * (1.0 - context.nv * context.nv));
    context.F0 = F0;
    const float2 energy = SampleGgxEnergyTexture(lighting.brdf_energy_srv,
        context.nv, context.roughness);
    context.specular_scale = GgxEnergyScale(F0, energy);
    const float3 reflected = context.specular_scale * (F0 * energy.x + (1.0 - F0) * energy.y);
    context.diffuse_scale = rho * (GgxDiffuseTransmission(reflected) / PI);
    return context;
}

static float3 EvaluatePreparedGgxSpecular(float3 N, float3 V,
    float3 representative, float receiver_cosine, GgxDirectContext context)
{
    const float nl = saturate(receiver_cosine);
    const float nv = context.nv;
    if (nl <= 0.0 || context.state == 0u) return 0.0.xxx;
    if (context.state != 1u) return asfloat(0x7fc00000u).xxx;
    const float3 half_vector = representative + V;
    const float half_scale = max(max(abs(half_vector.x), abs(half_vector.y)), abs(half_vector.z));
    if (half_scale == 0.0) return 0.0.xxx;
    const float3 H = normalize(half_vector / half_scale);
    const float vh_complement = 1.0 - saturate(dot(V, H));
    const float vh_squared = vh_complement * vh_complement;
    const float3 fresnel = context.F0 + (1.0 - context.F0)
        * (vh_squared * vh_squared * vh_complement);
    // Scaling the correlated visibility keeps its cosine-weighted response
    // finite at grazing, without another lookup or a clipped GGX peak.
    const float larger_cosine = max(nv, nl);
    const float smaller_cosine = min(nv, nl);
    precise float cosine_ratio = smaller_cosine / larger_cosine;
    const float light_root = sqrt(nl * nl + context.alpha_squared * (1.0 - nl * nl));
    const float larger_root = nv >= nl ? context.view_root : light_root;
    const float smaller_root = nv >= nl ? light_root : context.view_root;
    precise float denominator = smaller_root + cosine_ratio * larger_root;
    precise float receiver_ratio = nl / larger_cosine;
    return GgxDistribution(N, H, context.roughness)
        * ((0.5 * receiver_ratio) / denominator) * fresnel;
}

static float3 EvaluatePreparedGgxSingleScattering(float3 N, float3 V,
    float3 L, GgxDirectContext context)
{
    return EvaluatePreparedGgxSpecular(N, V, L, dot(N, L), context);
}

static GgxDirectLobes EvaluatePreparedGgxDirectLobes(float3 N, float3 V,
    float3 L, GgxDirectContext context)
{
    GgxDirectLobes result = (GgxDirectLobes)0;
    result.single_scattering = EvaluatePreparedGgxSingleScattering(N, V, L, context);
    result.multiple_scattering = result.single_scattering * (context.specular_scale - 1.0);
    result.diffuse = context.diffuse_scale * saturate(dot(N, L));
    return result;
}

static GgxDirectLobes EvaluateGgxDirectLobes(float3 N, float3 V, float3 L,
    float3 F0, float3 rho, float roughness, LightingFrameBindings lighting)
{
    return EvaluatePreparedGgxDirectLobes(N, V, L,
        PrepareGgxDirect(N, V, F0, rho, roughness, lighting));
}

static float3 EvaluateGgxDirectResponse(float3 N, float3 V, float3 L,
    float3 F0, float3 rho, float roughness, LightingFrameBindings lighting)
{
    const GgxDirectLobes lobes = EvaluateGgxDirectLobes(N, V, L, F0, rho, roughness, lighting);
    return lobes.single_scattering + lobes.multiple_scattering + lobes.diffuse;
}

struct GgxIntegratedLobes
{
    float3 specular;
    float3 diffuse;
};

static GgxIntegratedLobes EvaluateGgxIntegratedLobes(float nv, float3 F0,
    float3 rho, float roughness, LightingFrameBindings lighting)
{
    GgxIntegratedLobes result = (GgxIntegratedLobes)0;
    if (nv <= 0.0) return result;
    if (lighting.brdf_model_revision != 2u || !BX_IN_TEXTURES(lighting.brdf_energy_srv)) {
        result.specular = result.diffuse = asfloat(0x7fc00000u).xxx;
        return result;
    }
    const float2 energy = SampleGgxEnergyTexture(lighting.brdf_energy_srv, nv, roughness);
    result.specular = GgxEnergyScale(F0, energy) * (F0 * energy.x + (1.0 - F0) * energy.y);
    result.diffuse = rho * GgxDiffuseTransmission(result.specular);
    return result;
}

static inline float3 VortexSafeNormalize(float3 value)
{
    const float length_squared = dot(value, value);
    if (length_squared <= EPSILON_SMALL) {
        return float3(0.0f, 0.0f, 1.0f);
    }

    return value * rsqrt(length_squared);
}

static inline float3 ComputeDielectricF0(float specular)
{
    const float reflectance = saturate(specular) * 0.08f;
    return float3(reflectance, reflectance, reflectance);
}

static inline float3 ComputeMetallicF0(
    float3 base_color, float metallic, float specular)
{
    return lerp(
        ComputeDielectricF0(specular), max(base_color, 0.0f.xxx), saturate(metallic));
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SHARED_BRDFCOMMON_HLSLI

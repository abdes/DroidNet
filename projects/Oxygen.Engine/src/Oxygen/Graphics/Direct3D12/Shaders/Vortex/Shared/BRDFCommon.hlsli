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

// Manual bilinear filtering retains full FP32 interpolation weights. Texture
// sampler weight quantization is not part of the certified moment-table model.
static float2 SampleGgxMomentTexture(uint descriptor, float mu, float roughness)
{
    Texture2D<float2> texture = ResourceDescriptorHeap[descriptor];
    uint width, height;
    texture.GetDimensions(width, height);
    float2 location = float2(sqrt(saturate(mu)),
        (max(roughness, kVortexMinimumRoughness) - kVortexMinimumRoughness)
            / (1.0 - kVortexMinimumRoughness)) * float2(width - 1u, height - 1u);
    location = clamp(location, 0.0.xx, float2(width - 1u, height - 1u));
    uint2 lower = uint2(location);
    uint2 upper = min(lower + 1u, uint2(width - 1u, height - 1u));
    float2 fraction = location - float2(lower);
    return lerp(lerp(texture.Load(int3(lower, 0)),
                     texture.Load(int3(upper.x, lower.y, 0)), fraction.x),
                lerp(texture.Load(int3(lower.x, upper.y, 0)),
                     texture.Load(int3(upper, 0)), fraction.x), fraction.y);
}

static float GgxDistribution(float3 N, float3 H, float roughness)
{
    const float r = max(roughness, kVortexMinimumRoughness);
    const float alpha = r * r;
    const float alpha_squared = alpha * alpha;
    const float3 tangent = cross(N, H);
    const float nh = saturate(dot(N, H));
    const float denominator = dot(tangent, tangent) + alpha_squared * nh * nh;
    return alpha_squared / (PI * denominator * denominator);
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
    float3 one_minus_compensation;
    float3 multiple_scale;
    float3 diffuse_scale;
    uint moments_srv;
    uint state;
};

// Material/view terms are invariant across every sample of a finite emitter.
static GgxDirectContext PrepareGgxDirect(float3 N, float3 V, float3 F0,
    float3 rho, float roughness, LightingFrameBindings lighting)
{
    GgxDirectContext context = (GgxDirectContext)0;
    context.nv = saturate(dot(N, V));
    if (context.nv <= 0.0) return context;
    context.state = 2u;
    if (lighting.brdf_model_revision != 1u
        || !BX_IN_TEXTURES(lighting.brdf_moments_srv)
        || !BX_IN_TEXTURES(lighting.brdf_mean_moments_srv)) return context;
    context.state = 1u;
    context.roughness = max(roughness, kVortexMinimumRoughness);
    const float alpha = context.roughness * context.roughness;
    context.alpha_squared = alpha * alpha;
    precise float view_root = sqrt(context.nv * context.nv
        + context.alpha_squared * (1.0 - context.nv * context.nv));
    context.view_root = view_root;
    context.F0 = F0;
    context.moments_srv = lighting.brdf_moments_srv;
    const float2 view = SampleGgxMomentTexture(lighting.brdf_moments_srv, context.nv, context.roughness);
    const float2 mean = SampleGgxMomentTexture(lighting.brdf_mean_moments_srv, 0.0, context.roughness);
    const float mean_energy = 1.0 - mean.x;
    const float3 average_fresnel = F0 + (1.0 - F0) / 21.0;
    const float3 compensation = average_fresnel * average_fresnel * mean_energy
        / ((1.0 - average_fresnel) + average_fresnel * mean_energy);
    context.one_minus_compensation = 1.0 - compensation;
    context.multiple_scale = mean.x > 0.0 ? compensation * view.x / (PI * mean.x) : 0.0.xxx;
    const float3 tv = (1.0 - compensation) * view.x
        + (1.0 - F0) * (1.0 - view.x - view.y);
    const float3 ta = (1.0 - compensation) * mean.x
        + (1.0 - F0) * (mean_energy - mean.y);
    const float3 denominator = (1.0 - rho) + rho * ta;
    context.diffuse_scale = float3(
        denominator.r > 0.0 ? rho.r * tv.r / (PI * denominator.r) : 0.0,
        denominator.g > 0.0 ? rho.g * tv.g / (PI * denominator.g) : 0.0,
        denominator.b > 0.0 ? rho.b * tv.b / (PI * denominator.b) : 0.0);
    return context;
}

static float3 EvaluatePreparedGgxSingleScattering(float3 N, float3 V,
    float3 L, GgxDirectContext context)
{
    const float nl = saturate(dot(N, L));
    const float nv = context.nv;
    if (nl <= 0.0 || context.state == 0u) return 0.0.xxx;
    if (context.state != 1u) return asfloat(0x7fc00000u).xxx;
    const float3 half_vector = L + V;
    const float half_scale = max(max(abs(half_vector.x), abs(half_vector.y)), abs(half_vector.z));
    const float3 H = normalize(half_vector / half_scale);
    const float vh_complement = 1.0 - saturate(0.5 * dot(L + V, H));
    const float vh_squared = vh_complement * vh_complement;
    const float fresnel_bias = vh_squared * vh_squared * vh_complement;
    const float3 fresnel = context.F0 + (1.0 - context.F0) * fresnel_bias;
    const float larger_cosine = max(nv, nl);
    const float smaller_cosine = min(nv, nl);
    precise float cosine_ratio = smaller_cosine / larger_cosine;
    precise float light_root = sqrt(nl * nl + context.alpha_squared * (1.0 - nl * nl));
    precise float larger_root = nv >= nl ? context.view_root : light_root;
    precise float smaller_root = nv >= nl ? light_root : context.view_root;
    precise float scaled_denominator = smaller_root + cosine_ratio * larger_root;
    precise float receiver_ratio = nl / larger_cosine;
    precise float visibility = (0.5 * receiver_ratio) / scaled_denominator;
    return GgxDistribution(N, H, context.roughness) * visibility * fresnel;
}

static GgxDirectLobes EvaluatePreparedGgxBroadLobes(float3 N, float3 L,
    GgxDirectContext context)
{
    GgxDirectLobes lobes = (GgxDirectLobes)0;
    const float nl = saturate(dot(N, L));
    if (nl <= 0.0 || context.state == 0u) return lobes;
    if (context.state != 1u) {
        lobes.diffuse = asfloat(0x7fc00000u).xxx;
        return lobes;
    }
    const float2 light = SampleGgxMomentTexture(context.moments_srv, nl, context.roughness);
    lobes.multiple_scattering = context.multiple_scale * light.x * nl;
    const float3 transmission = context.one_minus_compensation * light.x
        + (1.0 - context.F0) * (1.0 - light.x - light.y);
    lobes.diffuse = context.diffuse_scale * transmission * nl;
    return lobes;
}

static GgxDirectLobes EvaluatePreparedGgxDirectLobes(float3 N, float3 V,
    float3 L, GgxDirectContext context)
{
    GgxDirectLobes lobes = EvaluatePreparedGgxBroadLobes(N, L, context);
    lobes.single_scattering = EvaluatePreparedGgxSingleScattering(N, V, L, context);
    return lobes;
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
    if (lighting.brdf_model_revision != 1u
        || !BX_IN_TEXTURES(lighting.brdf_moments_srv)
        || !BX_IN_TEXTURES(lighting.brdf_mean_moments_srv)) {
        result.specular = result.diffuse = asfloat(0x7fc00000u).xxx;
        return result;
    }
    const float2 view = SampleGgxMomentTexture(lighting.brdf_moments_srv, nv, roughness);
    const float2 mean = SampleGgxMomentTexture(lighting.brdf_mean_moments_srv, 0.0, roughness);
    const float mean_energy = 1.0 - mean.x;
    const float3 average_fresnel = F0 + (1.0 - F0) / 21.0;
    const float3 compensation = average_fresnel * average_fresnel * mean_energy
        / ((1.0 - average_fresnel) + average_fresnel * mean_energy);
    const float3 tv = (1.0 - compensation) * view.x
        + (1.0 - F0) * (1.0 - view.x - view.y);
    const float3 ta = (1.0 - compensation) * mean.x
        + (1.0 - F0) * (mean_energy - mean.y);
    const float3 denominator = (1.0 - rho) + rho * ta;
    result.specular = F0 * (1.0 - view.x) + (1.0 - F0) * view.y
        + compensation * view.x;
    result.diffuse = float3(
        denominator.r > 0.0 ? rho.r * tv.r * ta.r / denominator.r : 0.0,
        denominator.g > 0.0 ? rho.g * tv.g * ta.g / denominator.g : 0.0,
        denominator.b > 0.0 ? rho.b * tv.b * ta.b / denominator.b : 0.0);
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

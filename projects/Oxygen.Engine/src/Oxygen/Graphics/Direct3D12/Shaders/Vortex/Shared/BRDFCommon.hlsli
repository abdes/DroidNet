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

static GgxDirectLobes EvaluateGgxDirectLobes(float3 N, float3 V, float3 L,
    float3 F0, float3 rho, float roughness, LightingFrameBindings lighting)
{
    GgxDirectLobes lobes = (GgxDirectLobes)0;
    const float nl = saturate(dot(N, L));
    const float nv = saturate(dot(N, V));
    if (nl <= 0.0 || nv <= 0.0) return lobes;
    if (lighting.brdf_model_revision != 1u
        || !BX_IN_TEXTURES(lighting.brdf_moments_srv)
        || !BX_IN_TEXTURES(lighting.brdf_mean_moments_srv)) {
        // Required-data failure must reach the existing nonfinite HDR gate,
        // never masquerade as successful black illumination.
        lobes.single_scattering = asfloat(0x7fc00000u).xxx;
        return lobes;
    }
    const float3 half_vector = L + V;
    const float half_scale = max(max(abs(half_vector.x), abs(half_vector.y)), abs(half_vector.z));
    const float3 H = normalize(half_vector / half_scale);
    const float vh_complement = 1.0 - saturate(dot(V, H));
    const float vh_squared = vh_complement * vh_complement;
    const float fresnel_bias = vh_squared * vh_squared * vh_complement;
    const float3 fresnel = F0 + (1.0 - F0) * fresnel_bias;
    const float r = max(roughness, kVortexMinimumRoughness);
    const float alpha = r * r;
    const float a2 = alpha * alpha;
    // Evaluate V*NoL as a bounded ratio. Computing V first can overflow at
    // grazing incidence even though the resulting radiance is representable.
    // precise prevents reassociation into (nv*light_root)/nl, which can flush
    // a small but significant numerator to zero before the division.
    precise float view_light_ratio = nv / nl;
    precise float view_root = sqrt(nv * nv + a2 * (1.0 - nv * nv));
    precise float light_root = sqrt(nl * nl + a2 * (1.0 - nl * nl));
    precise float visibility = 0.5 / (view_root + view_light_ratio * light_root);
    lobes.single_scattering = GgxDistribution(N, H, r) * visibility * fresnel;

    const float2 light = SampleGgxMomentTexture(lighting.brdf_moments_srv, nl, r);
    const float2 view = SampleGgxMomentTexture(lighting.brdf_moments_srv, nv, r);
    const float2 mean = SampleGgxMomentTexture(lighting.brdf_mean_moments_srv, 0.0, r);
    const float mean_energy = 1.0 - mean.x;
    const float3 average_fresnel = F0 + (1.0 - F0) / 21.0;
    const float3 compensation = average_fresnel * average_fresnel * mean_energy
        / ((1.0 - average_fresnel) + average_fresnel * mean_energy);
    lobes.multiple_scattering = mean.x > 0.0
        ? compensation * light.x * view.x * nl / (PI * mean.x) : 0.0.xxx;
    // Positive-term transmission preserves unit-F0 and near-unit limits.
    const float3 tl = (1.0 - compensation) * light.x
        + (1.0 - F0) * (1.0 - light.x - light.y);
    const float3 tv = (1.0 - compensation) * view.x
        + (1.0 - F0) * (1.0 - view.x - view.y);
    const float3 ta = (1.0 - compensation) * mean.x
        + (1.0 - F0) * (mean_energy - mean.y);
    const float3 denominator = (1.0 - rho) + rho * ta;
    lobes.diffuse = float3(
        denominator.r > 0.0 ? rho.r * tl.r * tv.r * nl / (PI * denominator.r) : 0.0,
        denominator.g > 0.0 ? rho.g * tl.g * tv.g * nl / (PI * denominator.g) : 0.0,
        denominator.b > 0.0 ? rho.b * tl.b * tv.b * nl / (PI * denominator.b) : 0.0);
    return lobes;
}

static float3 EvaluateGgxDirectResponse(float3 N, float3 V, float3 L,
    float3 F0, float3 rho, float roughness, LightingFrameBindings lighting)
{
    const GgxDirectLobes lobes = EvaluateGgxDirectLobes(N, V, L, F0, rho, roughness, lighting);
    return lobes.single_scattering + lobes.multiple_scattering + lobes.diffuse;
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

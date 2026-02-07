//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_PASSES_FORWARD_FORWARDPBR_HLSLI
#define OXYGEN_PASSES_FORWARD_FORWARDPBR_HLSLI

#include "Common/Math.hlsli"

// -----------------------------------------------------------------------------
// PBR helpers (metallic-roughness, GGX)
// -----------------------------------------------------------------------------

// Legacy compatibility
#ifndef kPi
#define kPi PI
#endif


float3 SafeNormalize(float3 v)
{
    const float len_sq = dot(v, v);
    if (len_sq <= 1e-20) {
        return float3(0.0, 0.0, 0.0);
    }
    return v * rsqrt(len_sq);
}

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
    return SafeNormalize(n);
}

#endif // OXYGEN_PASSES_FORWARD_FORWARDPBR_HLSLI

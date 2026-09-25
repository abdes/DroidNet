//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_IBL_SAMPLING_HLSLI
#define OXYGEN_VORTEX_IBL_SAMPLING_HLSLI

#include "Vortex/Shared/Math.hlsli"

static float IblRoughnessToMip(float roughness, uint maximum_mip)
{
    return clamp(float(maximum_mip) - 2.0
        + 1.2 * log2(max(roughness, 0.001)), 0.0, float(maximum_mip));
}

static float IblMipToRoughness(uint mip, uint maximum_mip)
{
    return exp2((float(mip) + 2.0 - float(maximum_mip)) / 1.2);
}

// Standard hardware cube faces and top-down texels. World conversion remains
// CubemapSamplingDirFromOxygenWS / OxygenDirFromCubemapSamplingDir.
static float3 IblCubeDirection(uint face, float2 uv)
{
    float2 p = uv * 2.0 - 1.0;
    float3 d;
    switch (face) {
    case 0u: d = float3(1.0, -p.y, -p.x); break;
    case 1u: d = float3(-1.0, -p.y, p.x); break;
    case 2u: d = float3(p.x, 1.0, p.y); break;
    case 3u: d = float3(p.x, -1.0, -p.y); break;
    case 4u: d = float3(p.x, -p.y, 1.0); break;
    default: d = float3(-p.x, -p.y, -1.0); break;
    }
    return normalize(d);
}

static float2 IblHammersley(uint index, uint count)
{
    return float2(float(index) / float(count), float(reversebits(index)) * 2.3283064365386963e-10);
}

static float IblTexelSolidAngle(uint2 pixel, uint size)
{
    float2 lo = 2.0 * float2(pixel) / float(size) - 1.0;
    float2 hi = 2.0 * float2(pixel + 1u) / float(size) - 1.0;
    return atan2(hi.x * hi.y, sqrt(dot(hi, hi) + 1.0))
        - atan2(lo.x * hi.y, sqrt(lo.x * lo.x + hi.y * hi.y + 1.0))
        - atan2(hi.x * lo.y, sqrt(hi.x * hi.x + lo.y * lo.y + 1.0))
        + atan2(lo.x * lo.y, sqrt(dot(lo, lo) + 1.0));
}

static float3 IblPrefilter(TextureCube<float4> source, SamplerState cube_sampler,
    float3 normal, float roughness, uint size, uint maximum_mip)
{
    if (roughness < 0.01) return source.SampleLevel(cube_sampler, normal, 0.0).rgb;
    const uint count = roughness < 0.1 ? 32u : 64u;
    const float texel_angle = 8.0 * PI / (6.0 * float(size) * float(size));
    const float3 axis = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    const float3 tangent = normalize(cross(axis, normal));
    const float3 bitangent = cross(normal, tangent);
    float3 sum = 0.0.xxx;
    float weight = 0.0;
    for (uint i = 0u; i < count; ++i) {
        float2 e = IblHammersley(i, count);
        const float phi = 2.0 * PI * e.x;
        float3 l;
        float pdf;
        float contribution;
        if (roughness > 0.99) {
            float cosine = sqrt(e.y);
            float sine = sqrt(1.0 - e.y);
            l = float3(sine * cos(phi), sine * sin(phi), cosine);
            pdf = cosine / PI;
            contribution = 1.0;
        } else {
            e.y *= 0.995;
            float a2 = roughness * roughness * roughness * roughness;
            float nh = sqrt((1.0 - e.y) / (1.0 + (a2 - 1.0) * e.y));
            float sine = sqrt(saturate(1.0 - nh * nh));
            float3 h = float3(sine * cos(phi), sine * sin(phi), nh);
            l = 2.0 * nh * h - float3(0, 0, 1);
            if (l.z <= 0.0) continue;
            float denominator = (nh * a2 - nh) * nh + 1.0;
            pdf = a2 / (4.0 * PI * denominator * denominator);
            contribution = l.z;
        }
        // The cosine sequence includes one horizon sample; its zero PDF means
        // the coarsest source mip, not a discarded sample or NaN.
        const float lod = pdf > 0.0
            ? clamp(0.5 * log2(1.0 / (float(count) * pdf * texel_angle)), 0.0, float(maximum_mip))
            : float(maximum_mip);
        float3 direction = tangent * l.x + bitangent * l.y + normal * l.z;
        sum += source.SampleLevel(cube_sampler, direction, lod).rgb * contribution;
        weight += contribution;
    }
    return weight > 0.0 ? sum / weight : asfloat(0x7fc00000u).xxx;
}

#endif

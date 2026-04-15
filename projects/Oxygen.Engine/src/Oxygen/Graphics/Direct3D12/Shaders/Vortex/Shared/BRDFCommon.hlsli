//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SHARED_BRDFCOMMON_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SHARED_BRDFCOMMON_HLSLI

#include "Common/Math.hlsli"

static const float kVortexDefaultSpecular = 0.5f;

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

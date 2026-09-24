//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_VORTEX_POINT_SHADOW_FILTERING_HLSLI
#define OXYGEN_VORTEX_POINT_SHADOW_FILTERING_HLSLI

#include "Vortex/Contracts/Definitions/SceneDefinitions.hlsli"

// Conventional cube-PCF disk coordinates, matching the UE5.7 5/29-tap
// numeric patterns. Offsets have radius 2.5 in inverse-resolution units.
static const float2 kPointPcfDisk5[5] = {
    float2(0.000000, 2.500000), float2(2.377641, 0.772542),
    float2(1.469463, -2.022543), float2(-1.469463, -2.022542),
    float2(-2.377641, 0.772543)
};
static const float2 kPointPcfDisk29[29] = {
    float2(0.000000, 2.500000), float2(1.016842, 2.283864),
    float2(1.857862, 1.672826), float2(2.377641, 0.772542),
    float2(2.486305, -0.261321), float2(2.165063, -1.250000),
    float2(1.469463, -2.022543), float2(0.519779, -2.445369),
    float2(-0.519779, -2.445369), float2(-1.469463, -2.022542),
    float2(-2.165064, -1.250000), float2(-2.486305, -0.261321),
    float2(-2.377641, 0.772543), float2(-1.857862, 1.672827),
    float2(-1.016841, 2.283864), float2(0.091021, -0.642186),
    float2(0.698035, 0.100940), float2(0.959731, -1.169393),
    float2(-1.053880, 1.180380), float2(-1.479156, -0.606937),
    float2(-0.839488, -1.320002), float2(1.438566, 0.705359),
    float2(0.067064, -1.605197), float2(0.728706, 1.344722),
    float2(1.521424, -0.380184), float2(-0.199515, 1.590091),
    float2(-1.524323, 0.364010), float2(-0.692694, -0.086749),
    float2(-0.082476, 0.654088)
};

static float SamplePointShadowHardwarePcf(TextureCubeArray<float> surface,
    uint cube_index, float3 receiver_to_light, float inverse_resolution,
    float receiver_depth, float comparison_bias, uint sample_count)
{
    SamplerComparisonState comparison_sampler =
        SamplerDescriptorHeap[VORTEX_SAMPLER_SHADOW_COMPARISON];
    if (sample_count == 1u) {
        return surface.SampleCmpLevelZero(comparison_sampler,
            float4(receiver_to_light, cube_index), receiver_depth + comparison_bias);
    }

    const float3 direction = normalize(receiver_to_light);
    // The ordinary Z-up basis degenerates exactly at either pole.
    const float3 axis = abs(direction.z) < 0.999f
        ? float3(0, 0, 1) : float3(0, 1, 0);
    const float3 side = normalize(cross(direction, axis));
    const float3 up = cross(side, direction);
    const float3 scaled_side = side * inverse_resolution;
    const float3 scaled_up = up * inverse_resolution;
    float visibility = 0.0f;
    if (sample_count == 5u) {
        [unroll] for (uint tap = 0u; tap < 5u; ++tap) {
            const float2 offset = kPointPcfDisk5[tap];
            const float3 sample_direction = direction
                + scaled_side * offset.x + scaled_up * offset.y;
            visibility += surface.SampleCmpLevelZero(comparison_sampler,
                float4(sample_direction, cube_index),
                receiver_depth + comparison_bias * length(offset));
        }
        return visibility * (1.0f / 5.0f);
    }
    [unroll] for (uint tap = 0u; tap < 29u; ++tap) {
        const float2 offset = kPointPcfDisk29[tap];
        const float3 sample_direction = direction
            + scaled_side * offset.x + scaled_up * offset.y;
        visibility += surface.SampleCmpLevelZero(comparison_sampler,
            float4(sample_direction, cube_index),
            receiver_depth + comparison_bias * length(offset));
    }
    return visibility * (1.0f / 29.0f);
}

#endif

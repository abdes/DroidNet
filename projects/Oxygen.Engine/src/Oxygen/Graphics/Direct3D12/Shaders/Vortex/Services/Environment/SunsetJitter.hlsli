//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_SUNSETJITTER_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_SUNSETJITTER_HLSLI

#include "Vortex/Services/Environment/AtmosphereConstants.hlsli"

// Mirrors UE5.7 RandomInterleavedGradientNoise.ush::InterleavedGradientNoise.
static float InterleavedGradientNoise(float2 uv, float frame_id)
{
    uv += frame_id * (float2(47.0f, 17.0f) * 0.695f);

    const float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(uv, magic.xy)));
}

static float ComputeSunsetSegmentOffset(
    float2 pixel_position,
    uint state_frame_index_mod8,
    bool deterministic_view)
{
    if (deterministic_view)
    {
        return kSegmentSampleOffset;
    }
    return InterleavedGradientNoise(
        pixel_position,
        float(state_frame_index_mod8 & 7u));
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_ENVIRONMENT_SUNSETJITTER_HLSLI

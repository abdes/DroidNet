//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Atmosphere/SkySphereCommon.hlsli"

struct SkyCapturePSInput
{
    float4 position : SV_POSITION;
    float3 view_dir : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 PS(SkyCapturePSInput input) : SV_TARGET
{
    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    // Input view_dir is already in Oxygen world space from SkyCapture_VS.
    float3 view_dir_oxy = normalize(input.view_dir);

    float3 sky_color = ComputeSkyColor(env_data, view_dir_oxy);

    // FIX: Fill the black void below the horizon (physically the planet/ground).
    // In Oxygen World Space, the ground is Z < 0.
    if (view_dir_oxy.z < 0.0 && length(sky_color) < 0.0001)
    {
        float3 ground_color = env_data.atmosphere.ground_albedo_rgb;
        // Default to dark grey if not configured
        if (all(ground_color == 0.0)) {
            ground_color = float3(0.2, 0.2, 0.2);
        }
        sky_color = ground_color;
    }

    // No Exposure applied. We want raw linear values for the IBL capture.

    return float4(sky_color, 1.0);
}

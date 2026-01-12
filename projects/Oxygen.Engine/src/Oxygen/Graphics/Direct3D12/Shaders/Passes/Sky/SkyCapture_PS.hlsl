//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Passes/Sky/SkySphereCommon.hlsli"

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

    float3 view_dir = normalize(input.view_dir);
    float3 sky_color = ComputeSkyColor(env_data, view_dir);

    // No Exposure applied. We want raw linear values for the IBL capture.

    return float4(sky_color, 1.0);
}

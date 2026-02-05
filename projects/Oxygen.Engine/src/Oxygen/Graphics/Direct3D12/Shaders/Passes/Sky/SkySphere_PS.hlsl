//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Sky Sphere Pixel Shader
//!
//! Renders the sky background with priority:
//! 1. SkyAtmosphere (procedural) - if enabled and LUTs available
//! 2. SkySphere cubemap - if enabled and source is kCubemap
//! 3. SkySphere solid color - if enabled and source is kSolidColor
//! 4. Black fallback

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Passes/Sky/SkySphereCommon.hlsli"

//! Input from vertex shader.
struct SkyPSInput
{
    float4 position : SV_POSITION;
    float3 view_dir : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float4 PS(SkyPSInput input) : SV_TARGET
{
    // Load environment static data.
    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    float3 view_dir = normalize(input.view_dir);

    // Compute linear sky color
    float3 sky_color = ComputeSkyColor(env_data, view_dir);

#ifdef OXYGEN_HDR_OUTPUT
    return float4(sky_color, 1.0f);
#else
    // Apply exposure (Tonemapping happens in PostProcess, but exposure is applied here often for Sky?)
    // Actually SkySphere_PS applied exposure.
    sky_color *= GetExposure();

    return float4(sky_color, 1.0f);
#endif
}

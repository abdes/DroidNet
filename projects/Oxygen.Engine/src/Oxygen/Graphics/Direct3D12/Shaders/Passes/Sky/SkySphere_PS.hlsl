//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Sky Sphere Pixel Shader
//!
//! Renders the sky background with priority:
//! 1. SkyAtmosphere (procedural) - if enabled
//! 2. SkySphere cubemap - if enabled and source is kCubemap
//! 3. SkySphere solid color - if enabled and source is kSolidColor
//! 4. Black fallback

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/SceneConstants.hlsli"

//! Input from vertex shader.
struct SkyPSInput
{
    float4 position : SV_POSITION;
    float3 view_dir : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

//! Samples a cubemap using the view direction with optional rotation.
//!
//! @param cubemap_slot Bindless SRV index for the cubemap.
//! @param view_dir World-space view direction (normalized).
//! @param rotation_radians Azimuth rotation around world up (Y axis).
//! @return Sampled color (linear RGB).
float3 SampleSkyboxCubemap(uint cubemap_slot, float3 view_dir, float rotation_radians)
{
    // Apply rotation around Y axis.
    float cos_rot = cos(rotation_radians);
    float sin_rot = sin(rotation_radians);
    float3 rotated_dir;
    rotated_dir.x = view_dir.x * cos_rot + view_dir.z * sin_rot;
    rotated_dir.y = view_dir.y;
    rotated_dir.z = -view_dir.x * sin_rot + view_dir.z * cos_rot;

    // Sample the cubemap.
    TextureCube<float4> cubemap = ResourceDescriptorHeap[cubemap_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0]; // Assume sampler 0 is linear.

    return cubemap.Sample(linear_sampler, rotated_dir).rgb;
}

float4 PS(SkyPSInput input) : SV_TARGET
{
    // Load environment static data.
    EnvironmentStaticData env_data;
    if (!LoadEnvironmentStaticData(bindless_env_static_slot, frame_slot, env_data))
    {
        // Fallback: black sky if environment data unavailable.
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // Normalize view direction for cubemap sampling.
    float3 view_dir = normalize(input.view_dir);
    float3 sky_color = float3(0.0f, 0.0f, 0.0f);

    // Priority 1: SkyAtmosphere (procedural)
    if (env_data.atmosphere.enabled)
    {
        // TODO: Implement procedural atmosphere rendering.
        // For now, use a simple sky gradient as placeholder.
        float up_factor = saturate(view_dir.y * 0.5f + 0.5f);
        float3 horizon_color = float3(0.8f, 0.85f, 0.95f);
        float3 zenith_color = float3(0.3f, 0.5f, 0.9f);
        sky_color = lerp(horizon_color, zenith_color, up_factor);
    }
    // Priority 2: SkySphere cubemap
    else if (env_data.sky_sphere.enabled
        && env_data.sky_sphere.source == SKY_SPHERE_SOURCE_CUBEMAP
        && env_data.sky_sphere.cubemap_slot != K_INVALID_BINDLESS_INDEX)
    {
        sky_color = SampleSkyboxCubemap(
            env_data.sky_sphere.cubemap_slot,
            view_dir,
            env_data.sky_sphere.rotation_radians);

        // Apply intensity and tint.
        sky_color *= env_data.sky_sphere.intensity;
        sky_color *= env_data.sky_sphere.tint_rgb;
    }
    // Priority 3: SkySphere solid color
    else if (env_data.sky_sphere.enabled
        && env_data.sky_sphere.source == SKY_SPHERE_SOURCE_SOLID_COLOR)
    {
        sky_color = env_data.sky_sphere.solid_color_rgb;
        sky_color *= env_data.sky_sphere.intensity;
        sky_color *= env_data.sky_sphere.tint_rgb;
    }
    // Priority 4: Black fallback (sky_color already initialized to black)

    return float4(sky_color, 1.0f);
}

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_SHADERS_SKY_SPHERE_COMMON_HLSLI
#define OXYGEN_SHADERS_SKY_SPHERE_COMMON_HLSLI

#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/SceneConstants.hlsli"
#include "Renderer/SkyAtmosphereSampling.hlsli"

//! Samples a cubemap using the view direction with optional rotation.
float3 SampleSkyboxCubemap(uint cubemap_slot, float3 view_dir, float rotation_radians)
{
    // Apply rotation around Z axis.
    float cos_rot = cos(rotation_radians);
    float sin_rot = sin(rotation_radians);
    float3 rotated_dir;
    rotated_dir.x = view_dir.x * cos_rot - view_dir.y * sin_rot;
    rotated_dir.y = view_dir.x * sin_rot + view_dir.y * cos_rot;
    rotated_dir.z = view_dir.z;

    // Convert Oxygen Z-up to our cubemap sampling convention .
    float3 cube_dir = CubemapSamplingDirFromOxygenWS(rotated_dir);

    TextureCube<float4> cubemap = ResourceDescriptorHeap[cubemap_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    return cubemap.Sample(linear_sampler, cube_dir).rgb;
}

//! Computes the linear sky color for a given view direction using EnvironmentStaticData.
float3 ComputeSkyColor(EnvironmentStaticData env_data, float3 view_dir)
{
    float3 sky_color = float3(0.0f, 0.0f, 0.0f);

    // Priority 1: SkyAtmosphere (procedural)
    if (env_data.atmosphere.enabled)
    {
        if (env_data.atmosphere.sky_view_lut_slot != K_INVALID_BINDLESS_INDEX)
        {
            float3 sun_dir = GetSunDirectionWS();
            float3 sun_luminance = GetSunLuminanceRGB();
            // FIXME: Temporary LDR scale: keeps the sky readable until HDR + tone mapping land.
            const float kLdrSkyLuminanceScale = 0.1f;
            sun_luminance *= kLdrSkyLuminanceScale;

            if (!HasSunLight() && !IsOverrideSunEnabled())
            {
                sun_dir = normalize(float3(0.5, 0.5, 0.5));
                sun_luminance = float3(0.0, 0.0, 0.0);
            }

            float planet_radius = env_data.atmosphere.planet_radius_m;
            float camera_altitude = GetCameraAltitudeM();

            sky_color = ComputeAtmosphereSkyColor(
                env_data.atmosphere,
                view_dir,
                sun_dir,
                sun_luminance,
                planet_radius,
                camera_altitude);
        }
        else
        {
            // Fallback gradient
            float up_factor = saturate(view_dir.z * 0.5f + 0.5f);
            float3 horizon_color = float3(0.8f, 0.85f, 0.95f);
            float3 zenith_color = float3(0.3f, 0.5f, 0.9f);
            sky_color = lerp(horizon_color, zenith_color, up_factor);
        }
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

    return sky_color;
}

#endif // OXYGEN_SHADERS_SKY_SPHERE_COMMON_HLSLI

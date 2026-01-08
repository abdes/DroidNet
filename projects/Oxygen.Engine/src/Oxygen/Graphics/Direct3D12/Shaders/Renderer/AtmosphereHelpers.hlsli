//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ATMOSPHEREHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ATMOSPHEREHELPERS_HLSLI

#include "Renderer/EnvironmentStaticData.hlsli"

//! Phase function for Henyey-Greenstein scattering.
/*!
 Computes the scattering phase function for atmospheric scattering effects.
 @param cos_theta Cosine of the angle between view and light directions.
 @param g Anisotropy parameter (-1 = backward, 0 = isotropic, 1 = forward).
 @return The phase function value.
*/
static inline float HenyeyGreenstein(float cos_theta, float g)
{
    static const float kOneOverFourPi = 0.07957747154f;
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cos_theta;
    return kOneOverFourPi * (1.0f - g2) / (denom * sqrt(denom));
}

//! Computes exponential height fog factor.
/*!
 Implements exponential height fog with distance-based density falloff.
 @param params The fog parameters from EnvironmentStaticData.
 @param world_pos World-space position of the fragment.
 @param camera_pos World-space camera position.
 @param view_distance Distance from camera to fragment.
 @return Fog opacity in [0, 1] range (0 = no fog, 1 = fully fogged).
*/
static inline float ComputeExponentialHeightFog(
    GpuFogParams params,
    float3 world_pos,
    float3 camera_pos,
    float view_distance)
{
    if (!params.enabled || view_distance < params.start_distance_m)
    {
        return 0.0f;
    }

    // Effective distance after start offset
    float effective_distance = view_distance - params.start_distance_m;

    // Height fog with exponential falloff
    float cam_height = camera_pos.y - params.height_offset_m;
    float frag_height = world_pos.y - params.height_offset_m;

    // Integrate density along the ray
    float fog_amount;
    if (abs(params.height_falloff) < 0.0001f)
    {
        // Uniform density (no height falloff)
        fog_amount = params.density * effective_distance;
    }
    else
    {
        // Exponential height fog integration
        float height_diff = frag_height - cam_height;
        if (abs(height_diff) < 0.001f)
        {
            // Nearly horizontal ray
            fog_amount = params.density * exp(-params.height_falloff * cam_height) * effective_distance;
        }
        else
        {
            // Full height-integrated fog
            float a = params.height_falloff * cam_height;
            float b = params.height_falloff * frag_height;
            float k = params.density / params.height_falloff;
            fog_amount = k * (exp(-a) - exp(-b)) / (height_diff / effective_distance);
        }
    }

    // Apply exponential falloff and clamp
    float fog_factor = 1.0f - exp(-fog_amount);
    return min(fog_factor, params.max_opacity);
}

//! Computes inscattering color based on sun direction.
/*!
 Adds atmospheric inscattering color contribution from the sun.
 @param params The fog parameters from EnvironmentStaticData.
 @param view_dir Normalized view direction (camera to fragment).
 @param sun_dir Normalized sun direction (toward sun).
 @return Inscattering color multiplier (RGB).
*/
static inline float3 ComputeFogInscattering(
    GpuFogParams params,
    float3 view_dir,
    float3 sun_dir)
{
    if (!params.enabled || params.scattering_intensity <= 0.0f)
    {
        return params.albedo_rgb;
    }

    // Angle between view and sun
    float cos_theta = dot(view_dir, sun_dir);

    // Phase function for forward scattering
    float phase = HenyeyGreenstein(cos_theta, params.anisotropy_g);

    // Blend base albedo with inscattering
    float3 inscatter_color = params.albedo_rgb * (1.0f + params.scattering_intensity * phase);
    return inscatter_color;
}

//! Result of atmospheric fog computation.
struct AtmosphericFogResult
{
    float opacity;        //!< Fog opacity [0, 1].
    float3 inscatter_rgb; //!< Inscattering color (linear RGB).
};

//! Computes complete atmospheric fog for a fragment.
/*!
 Full atmospheric fog calculation including height-based density and
 sun inscattering. This is the main entry point for fog integration.

 @param env_data The complete EnvironmentStaticData.
 @param world_pos World-space position of the fragment.
 @param camera_pos World-space camera position.
 @param sun_dir Normalized sun direction (toward sun, e.g., -light_dir).
 @return Fog result with opacity and inscattering color.
*/
static inline AtmosphericFogResult GetAtmosphericFog(
    EnvironmentStaticData env_data,
    float3 world_pos,
    float3 camera_pos,
    float3 sun_dir)
{
    AtmosphericFogResult result;
    result.opacity = 0.0f;
    result.inscatter_rgb = float3(0.0f, 0.0f, 0.0f);

    if (!env_data.fog.enabled)
    {
        return result;
    }

    float3 view_vec = world_pos - camera_pos;
    float view_distance = length(view_vec);

    if (view_distance < 0.001f)
    {
        return result;
    }

    float3 view_dir = view_vec / view_distance;

    // Compute fog factor based on model
    if (env_data.fog.model == FOG_MODEL_EXPONENTIAL_HEIGHT)
    {
        result.opacity = ComputeExponentialHeightFog(
            env_data.fog,
            world_pos,
            camera_pos,
            view_distance);
    }
    // FOG_MODEL_VOLUMETRIC would be handled here in future

    // Compute inscattering color
    result.inscatter_rgb = ComputeFogInscattering(env_data.fog, view_dir, sun_dir);

    return result;
}

//! Applies atmospheric fog to a lit fragment color.
/*!
 Blends the fog inscattering color with the fragment color based on opacity.

 @param fragment_color Original lit fragment color (linear RGB).
 @param fog Fog computation result from GetAtmosphericFog.
 @return Final color with fog applied (linear RGB).
*/
static inline float3 ApplyAtmosphericFog(float3 fragment_color, AtmosphericFogResult fog)
{
    return lerp(fragment_color, fog.inscatter_rgb, fog.opacity);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ATMOSPHEREHELPERS_HLSLI

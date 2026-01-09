//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Aerial Perspective Sampling for Forward Rendering
//!
//! Computes atmospheric scattering between the camera and scene geometry.
//! Uses precomputed LUTs when available, falling back to analytic fog.
//!
//! === Algorithm Overview ===
//! Aerial perspective adds color and fades distant objects by simulating
//! light scattering in the atmosphere between camera and surface:
//!   final_color = surface_color * transmittance + inscatter
//!
//! The transmittance LUT encodes optical depth, and the sky-view LUT provides
//! inscattered radiance. For scene geometry, we raymarch or approximate using
//! the average inscatter along the view ray.

#ifndef AERIAL_PERSPECTIVE_HLSLI
#define AERIAL_PERSPECTIVE_HLSLI

#include "Renderer/EnvironmentStaticData.hlsli"
#include "Renderer/EnvironmentDynamicData.hlsli"
#include "Renderer/SkyAtmosphereSampling.hlsli"

// Atmosphere feature flag bits (matches C++ AtmosphereFlags enum)
static const uint ATMOSPHERE_USE_LUT = 0x1;         // Use LUT sampling when available
static const uint ATMOSPHERE_VISUALIZE_LUT = 0x2;   // Debug: show LUT as overlay
static const uint ATMOSPHERE_FORCE_ANALYTIC = 0x4;  // Force analytic fallback
static const uint ATMOSPHERE_OVERRIDE_SUN = 0x8;    // Use debug override sun

//! Result of aerial perspective computation.
struct AerialPerspectiveResult
{
    float3 inscatter;     //!< Inscattered radiance to add (linear RGB).
    float3 transmittance; //!< RGB transmittance through atmosphere [0, 1].
};

float Luminance(float3 rgb)
{
    return dot(rgb, float3(0.2126, 0.7152, 0.0722));
}

float RaySphereIntersectNearest(float3 origin, float3 dir, float radius)
{
    float a = dot(dir, dir);
    float b = 2.0 * dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0)
    {
        return -1.0;
    }
    float s = sqrt(disc);
    float t0 = (-b - s) / (2.0 * a);
    float t1 = (-b + s) / (2.0 * a);
    if (t0 > 0.0)
    {
        return t0;
    }
    if (t1 > 0.0)
    {
        return t1;
    }
    return -1.0;
}

float RaySphereIntersectFarthest(float3 origin, float3 dir, float radius)
{
    float a = dot(dir, dir);
    float b = 2.0 * dot(origin, dir);
    float c = dot(origin, origin) - radius * radius;
    float disc = b * b - 4.0 * a * c;
    if (disc < 0.0)
    {
        return -1.0;
    }
    float s = sqrt(disc);
    float t0 = (-b - s) / (2.0 * a);
    float t1 = (-b + s) / (2.0 * a);
    float t = max(t0, t1);
    return (t > 0.0) ? t : -1.0;
}

//! Returns whether LUT-based aerial perspective should be used.
//!
//! Checks atmosphere flags and LUT availability.
//!
//! @param atmo Atmosphere parameters from EnvironmentStaticData.
//! @return True if LUT sampling should be used for aerial perspective.
bool ShouldUseLutAerialPerspective(GpuSkyAtmosphereParams atmo)
{
    // Check if atmosphere is enabled
    if (!atmo.enabled)
    {
        return false;
    }

    // Check if LUTs are valid
    if (atmo.transmittance_lut_slot == K_INVALID_BINDLESS_INDEX ||
        atmo.sky_view_lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        return false;
    }

    // Check atmosphere flags
    uint flags = EnvironmentDynamicData.atmosphere_flags;

    // Force analytic fallback overrides everything
    if (flags & ATMOSPHERE_FORCE_ANALYTIC)
    {
        return false;
    }

    // Use LUT if the flag is set
    return (flags & ATMOSPHERE_USE_LUT) != 0;
}

//! Computes aerial perspective using LUT sampling.
//!
//! Approximates inscattering along the view ray by sampling the sky-view LUT
//! at multiple points and integrating. Uses a simplified 2-sample approximation
//! for performance.
//!
//! @param atmo Atmosphere parameters from EnvironmentStaticData.
//! @param world_pos World-space position of the fragment.
//! @param camera_pos World-space camera position.
//! @param sun_dir Normalized direction toward the sun.
//! @param view_distance Distance from camera to fragment in meters.
//! @return Aerial perspective result with inscatter and transmittance.
AerialPerspectiveResult ComputeAerialPerspectiveLut(
    GpuSkyAtmosphereParams atmo,
    float3 world_pos,
    float3 camera_pos,
    float3 sun_dir,
    float view_distance)
{
    AerialPerspectiveResult result;
    result.inscatter = float3(0.0, 0.0, 0.0);
    result.transmittance = float3(1.0, 1.0, 1.0);

    if (view_distance < 0.1)
    {
        return result;
    }

    // User controls
    float distance_scale = max(atmo.aerial_perspective_distance_scale, 0.0);
    float scattering_strength = max(EnvironmentDynamicData.aerial_scattering_strength, 0.0);

    // Early out if disabled
    if (distance_scale < 0.0001 || scattering_strength < 0.0001)
    {
        return result;
    }

    // View direction from camera to fragment
    float3 view_dir = normalize(world_pos - camera_pos);

    // Camera altitude in meters
    float camera_altitude_m = max(1.0, GetCameraAltitudeM());

    // Scattering coefficients are in 1/meter units.
    // Rayleigh: ~5.8e-6 (R), ~13.5e-6 (G), ~33.1e-6 (B) per meter at sea level
    // Mie: ~21e-6 per meter (gray) at sea level
    //
    // For a 1km horizontal path at sea level:
    //   tau_blue = 33.1e-6 * 1000 = 0.033 → ~3% opacity
    //
    // Apply exponential density falloff with altitude.
    float rayleigh_scale_h = max(atmo.rayleigh_scale_height_m, 100.0);
    float mie_scale_h = max(atmo.mie_scale_height_m, 100.0);

    float rayleigh_density = exp(-camera_altitude_m / rayleigh_scale_h);
    float mie_density = exp(-camera_altitude_m / mie_scale_h);

    // Effective path length with user scaling
    float effective_distance = view_distance * distance_scale;

    // Optical depth: τ = β * d * ρ * user_strength
    // β is already in 1/m, d is in meters, ρ is dimensionless density factor
    float3 beta_rayleigh = atmo.rayleigh_scattering_rgb;
    float3 beta_mie = atmo.mie_scattering_rgb;

    float3 tau_rayleigh = beta_rayleigh * effective_distance * rayleigh_density * scattering_strength;
    float3 tau_mie = beta_mie * effective_distance * mie_density * scattering_strength;

    // Clamp to prevent extreme values at very long distances
    float3 tau_total = min(tau_rayleigh + tau_mie, float3(10.0, 10.0, 10.0));

    // Transmittance via Beer-Lambert
    result.transmittance = saturate(exp(-tau_total));

    // Inscatter: (1 - T) gives the opacity. Modulate by sky color for directionality.
    float3 opacity = 1.0 - result.transmittance;

    // Sample sky-view LUT for color/directionality (sun angle dependency)
    float planet_radius = atmo.planet_radius_m;
    float4 sky_sample = SampleSkyViewLut(
        atmo.sky_view_lut_slot,
        atmo.sky_view_lut_width,
        atmo.sky_view_lut_height,
        view_dir,
        sun_dir,
        planet_radius,
        camera_altitude_m);

    // The sky LUT gives radiance for infinite rays. For short geometry segments,
    // we use opacity as the blend factor. The sky_sample provides color/direction.
    // Normalize by max component to get a "sky color" without extreme brightness,
    // then scale by opacity and a subtle intensity factor.
    float3 sun_luminance = GetSunLuminanceRGB();
    float3 sky_color = sky_sample.rgb;
    float sky_lum = max(max(sky_color.r, sky_color.g), max(sky_color.b, 0.001));

    // Use sky color direction but cap brightness, then apply sun luminance
    float3 sky_dir = sky_color / sky_lum;
    float3 inscatter_color = sky_dir * sun_luminance;

    // Final inscatter = color * opacity (already distance-dependent via transmittance)
    result.inscatter = inscatter_color * opacity;

    return result;
}

//! Computes aerial perspective (main entry point).
//!
//! Selects between LUT-based and analytic fog based on availability and flags.
//! This is the recommended function to call from forward shaders.
//!
//! @param env_data Static environment data.
//! @param world_pos World-space position of the fragment.
//! @param camera_pos World-space camera position.
//! @param sun_dir Normalized direction toward the sun.
//! @return Aerial perspective result.
AerialPerspectiveResult ComputeAerialPerspective(
    EnvironmentStaticData env_data,
    float3 world_pos,
    float3 camera_pos,
    float3 sun_dir)
{
    AerialPerspectiveResult result;
    result.inscatter = float3(0.0, 0.0, 0.0);
    result.transmittance = float3(1.0, 1.0, 1.0);

    float3 view_vec = world_pos - camera_pos;
    float view_distance = length(view_vec);

    if (view_distance < 0.001)
    {
        return result;
    }

    // Try LUT-based aerial perspective first
    if (ShouldUseLutAerialPerspective(env_data.atmosphere))
    {
        return ComputeAerialPerspectiveLut(
            env_data.atmosphere,
            world_pos,
            camera_pos,
            sun_dir,
            view_distance);
    }

    // Fallback: No aerial perspective (let caller use analytic fog if available)
    return result;
}

//! Applies aerial perspective to a lit fragment color.
//!
//! Blends the fragment color with atmospheric inscatter based on transmittance.
//!
//! @param fragment_color Original lit fragment color (linear RGB).
//! @param ap Aerial perspective result from ComputeAerialPerspective.
//! @return Final color with aerial perspective applied (linear RGB).
float3 ApplyAerialPerspective(float3 fragment_color, AerialPerspectiveResult ap)
{
    return fragment_color * ap.transmittance + ap.inscatter;
}

#endif // AERIAL_PERSPECTIVE_HLSLI

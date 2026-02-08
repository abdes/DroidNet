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
#include "Renderer/SceneConstants.hlsli"
#include "Atmosphere/AtmosphereSampling.hlsli"
#include "Atmosphere/AtmospherePhase.hlsli"
#include "Common/Math.hlsli"
#include "Atmosphere/AtmosphereConstants.hlsli"

#include "Common/Geometry.hlsli"
#include "Common/Lighting.hlsli"

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



//! Samples the camera-volume LUT at the fragment's screen UV and depth slice.
//!
//! Returns float4 where rgb = inscatter, a = opacity (1 - transmittance).
float4 SampleCameraVolumeLut(
    GpuSkyAtmosphereParams atmo,
    float3 world_pos,
    float3 camera_pos,
    float view_distance)
{
    if (view_distance < 0.1 || atmo.camera_volume_lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        return float4(0.0, 0.0, 0.0, 0.0);
    }

    // User controls from EnvironmentDynamicData.
    // Note: the camera-volume LUT itself is generated at a fixed max distance;
    // this scale remaps view distance to the LUT slice distribution.
    float distance_scale = max(EnvironmentDynamicData.aerial_perspective_distance_scale, 0.0);

    // Effective path length with user scaling.
    float effective_distance = view_distance * distance_scale;
    float view_distance_km = effective_distance / 1000.0;

    // Camera volume froxel parameterization (UE tuned parameters).
    const float AP_SLICE_COUNT = (float)kAerialPerspectiveSliceCount;
    const float AP_KM_PER_SLICE = kAerialPerspectiveKmPerSlice;

    float slice = view_distance_km / AP_KM_PER_SLICE;
    slice = clamp(slice, 0.0, AP_SLICE_COUNT);

    // Fade near camera to avoid quantization artifacts in the first few froxels.
    if (slice < 0.5)
    {
        slice = 0.5;
    }

    float w = sqrt(saturate(slice / AP_SLICE_COUNT));

    // Screen UV from world position
    float4 view_pos = mul(view_matrix, float4(world_pos, 1.0));
    float4 clip_pos = mul(projection_matrix, view_pos);
    clip_pos /= clip_pos.w;
    float2 screen_uv = clip_pos.xy * 0.5 + 0.5;
    // Handle D3D12/Vulkan Y flip
    screen_uv.y = 1.0 - screen_uv.y;

    Texture3D<float4> camera_volume = ResourceDescriptorHeap[atmo.camera_volume_lut_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];
    return camera_volume.SampleLevel(linear_sampler, float3(screen_uv, w), 0);
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
//! Computes aerial perspective using froxel-based camera volume LUT.
//!
//! Replaces the simplified 2-sample approximation with a high-performance
//! 3D texture lookup into the camera-aligned volume.
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

    // User controls from EnvironmentDynamicData
    float scattering_strength = max(EnvironmentDynamicData.aerial_scattering_strength, 0.0);

    // Early out if disabled via strength
    if (scattering_strength < 0.0001)
    {
        return result;
    }

    // Fade near camera to avoid quantization artifacts in the first few froxels.
    // This matches the UE reference behavior (weight is linear in slice).
    // Note: the LUT sampler clamps slice >= 0.5, so we mirror the weight logic here.
    const float AP_SLICE_COUNT = (float)kAerialPerspectiveSliceCount;
    const float AP_KM_PER_SLICE = kAerialPerspectiveKmPerSlice;
    float distance_scale = max(EnvironmentDynamicData.aerial_perspective_distance_scale, 0.0);
    float view_distance_km = (view_distance * distance_scale) / 1000.0;
    float slice = clamp(view_distance_km / AP_KM_PER_SLICE, 0.0, AP_SLICE_COUNT);
    float weight = (slice < 0.5) ? saturate(slice * 2.0) : 1.0;

    float4 ap_sample = SampleCameraVolumeLut(atmo, world_pos, camera_pos, view_distance);

    // Apply user scattering strength
    result.inscatter = ap_sample.rgb * scattering_strength * weight;

    // Opacity = ap_sample.a; Transmittance = 1 - Opacity.
    // Keep transmittance independent of the artistic scattering strength control;
    // strength scales the added inscatter term only.
    float opacity = saturate(ap_sample.a) * weight;
    result.transmittance = 1.0 - opacity;

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

    // Compute atmosphere aerial perspective (LUT-based) when enabled.
    if (ShouldUseLutAerialPerspective(env_data.atmosphere))
    {
        result = ComputeAerialPerspectiveLut(
            env_data.atmosphere,
            world_pos,
            camera_pos,
            sun_dir,
            view_distance);
    }

    // Apply Fog environment system in addition to atmosphere.
    // Fog must remain responsive even when LUT haze is active.
    if (env_data.fog.enabled)
    {
        GpuFogParams fog = env_data.fog;

        const float start_d = max(fog.start_distance_m, 0.0);
        const float d = max(view_distance - start_d, 0.0);
        if (d > 1e-4)
        {
            const float base_density = max(fog.density, 0.0);
            const float falloff = max(fog.height_falloff, 0.0);

            // Oxygen convention is Z-up; fog height parameters are authored in meters.
            const float mid_height_m = 0.5 * (camera_pos.z + world_pos.z);
            const float height_rel_m = mid_height_m - fog.height_offset_m;

            // Density decreases with height above the offset.
            const float height_scale = (falloff > 1e-5) ? exp(-falloff * height_rel_m) : 1.0;
            const float effective_density = base_density * height_scale;

            // Beer-Lambert extinction approximation.
            float fog_opacity = 1.0 - exp(-effective_density * d);
            fog_opacity = saturate(fog_opacity);
            fog_opacity = min(fog_opacity, saturate(fog.max_opacity));

            const float fog_transmittance = 1.0 - fog_opacity;

            // TODO: Rethink Fog
            // Composite: multiply transmittance, add fog inscatter.
            // Single-scattering approximation: fog both attenuates and adds
            // inscattered radiance. This avoids the "pure darkening" look.
            result.transmittance *= fog_transmittance;

            // Directional single scattering from the sun.
            const float3 view_dir = view_vec / view_distance; // camera -> point
            const float cos_theta = dot(sun_dir, -view_dir);  // point->camera
            const float phase = HenyeyGreensteinPhase(cos_theta, fog.anisotropy_g);

            const float scattering_intensity
                = (fog.scattering_intensity > 0.0) ? fog.scattering_intensity : 1.0;

            const float sun_irradiance = LuxToIrradiance(GetSunIlluminance());
            const float3 sun_radiance = GetSunColorRGB() * (sun_irradiance * INV_PI);

            result.inscatter += sun_radiance * fog.albedo_rgb
                * (fog_opacity * scattering_intensity * phase);
        }
    }

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

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
#include "Renderer/SkyAtmosphereSampling.hlsli"
#include "Common/Math.hlsli"
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

    if (view_distance < 0.1 || atmo.camera_volume_lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        return result;
    }

    // User controls from EnvironmentDynamicData
    float distance_scale = max(atmo.aerial_perspective_distance_scale, 0.0);
    float scattering_strength = max(EnvironmentDynamicData.aerial_scattering_strength, 0.0);

    // Early out if disabled via strength
    if (scattering_strength < 0.0001)
    {
        return result;
    }

    // Effective path length with user scaling.
    float effective_distance = view_distance * distance_scale;
    float view_distance_km = effective_distance / 1000.0;

    // Camera volume froxel parameterization (UE tuned parameters).
    // SliceCount = 32, SliceSizeKm = 4 → MaxDistanceKm = 128.
    // Generation uses squared distribution and sampling uses:
    //   w = sqrt(slice / SliceCount)
    // where slice = depth_km / SliceSizeKm.
    const float AP_SLICE_COUNT = 32.0;
    const float AP_KM_PER_SLICE = 4.0;

    float slice = view_distance_km / AP_KM_PER_SLICE;
    slice = clamp(slice, 0.0, AP_SLICE_COUNT);

    // Fade near camera to avoid quantization artifacts in the first few froxels.
    // This matches the UE reference behavior (weight is linear in slice).
    float weight = 1.0;
    if (slice < 0.5)
    {
        weight = saturate(slice * 2.0);
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

    // Sample camera volume (32 slices, squared distribution)
    // RGB = Inscattered Radiance
    // A   = 1 - Transmittance (Opacity)
    Texture3D<float4> camera_volume = ResourceDescriptorHeap[atmo.camera_volume_lut_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0]; // Global linear sampler

    float4 ap_sample = camera_volume.SampleLevel(linear_sampler, float3(screen_uv, w), 0);

    // Apply user scattering strength
    result.inscatter = ap_sample.rgb * scattering_strength * weight;

    // Opacity = ap_sample.a; Transmittance = 1 - Opacity
    // We also modulate opacity by scattering strength for the final transmittance
    float opacity = saturate(ap_sample.a * scattering_strength) * weight;
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

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Sky Atmosphere LUT Sampling Helpers
//!
//! Provides functions to sample precomputed transmittance and sky-view LUTs
//! for physically-based atmospheric scattering.
//!
//! === LUT UV Parameterizations ===
//! Transmittance LUT:
//!   u = (cos_zenith + 0.15) / 1.15  (avoids horizon singularity)
//!   v = sqrt(altitude / atmosphere_height)
//!
//! Sky-View LUT:
//!   u = azimuth / (2 * PI)
//!   v = (cos_zenith + 1) / 2

#ifndef SKY_ATMOSPHERE_SAMPLING_HLSLI
#define SKY_ATMOSPHERE_SAMPLING_HLSLI

#include "Core/Bindless/Generated.BindlessLayout.hlsl"
#include "Renderer/EnvironmentStaticData.hlsli"
#include "Atmosphere/AtmosphereMedium.hlsli"
#include "Common/Math.hlsli"
#include "Atmosphere/AtmosphereConstants.hlsli"


//! Computes transmittance LUT UV from altitude and cos_zenith.
//!
//! @param cos_zenith Cosine of zenith angle (view direction dot up).
//! @param altitude_m Height above planet surface in meters.
//! @param atmosphere_height_m Total atmosphere thickness in meters.
//! @return UV coordinates for transmittance LUT sampling.
float2 GetTransmittanceLutUv(
    float cos_zenith,
    float altitude_m,
    float planet_radius_m,
    float atmosphere_height_m)
{
    float view_height = planet_radius_m + altitude_m;
    float top_radius = planet_radius_m + atmosphere_height_m;
    float H = sqrt(max(0.0, top_radius * top_radius
        - planet_radius_m * planet_radius_m));
    float rho = sqrt(max(0.0, view_height * view_height
        - planet_radius_m * planet_radius_m));

    float discriminant = view_height * view_height
        * (cos_zenith * cos_zenith - 1.0)
        + top_radius * top_radius;
    float d = max(0.0, (-view_height * cos_zenith + sqrt(discriminant)));

    float d_min = top_radius - view_height;
    float d_max = rho + H;
    float x_mu = (d - d_min) / (d_max - d_min);
    float x_r = rho / H;

    return saturate(float2(x_mu, x_r));
}

//! Samples the transmittance LUT.
//!
//! @param lut_slot Bindless SRV index for the transmittance LUT.
//! @param lut_width LUT texture width.
//! @param lut_height LUT texture height.
//! @param cos_zenith Cosine of zenith angle.
//! @param altitude_m Height above planet surface in meters.
//! @param atmosphere_height_m Total atmosphere thickness in meters.
//! @return RGB transmittance.
float3 SampleTransmittanceOpticalDepthLut(
    uint lut_slot,
    float lut_width,
    float lut_height,
    float cos_zenith,
    float altitude_m,
    float planet_radius_m,
    float atmosphere_height_m)
{
    if (lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        // No LUT available, return zero optical depth.
        return float3(0.0, 0.0, 0.0);
    }

    float2 uv = GetTransmittanceLutUv(
        cos_zenith, altitude_m, planet_radius_m, atmosphere_height_m);

    // Apply half-texel offset for proper filtering.
    uv = uv * float2((lut_width - 1.0) / lut_width, (lut_height - 1.0) / lut_height);
    uv += float2(0.5 / lut_width, 0.5 / lut_height);

    // RGB stores optical depth integrals for Rayleigh/Mie/Absorption.
    Texture2D<float4> lut = ResourceDescriptorHeap[lut_slot];
    SamplerState linear_sampler = SamplerDescriptorHeap[0];

    return lut.SampleLevel(linear_sampler, uv, 0).rgb;
}



float3 SampleTransmittanceLut(
    GpuSkyAtmosphereParams atmo,
    uint lut_slot,
    float lut_width,
    float lut_height,
    float cos_zenith,
    float altitude_m,
    float atmosphere_height_m)
{
    if (lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        return float3(1.0, 1.0, 1.0);
    }

    float3 od = SampleTransmittanceOpticalDepthLut(
        lut_slot,
        lut_width,
        lut_height,
        cos_zenith,
        altitude_m,
        atmo.planet_radius_m,
        atmosphere_height_m);

    return TransmittanceFromOpticalDepth(od, atmo);
}

//! Computes sky-view LUT UV from view direction with sun-relative parameterization.
//!
//! This must match the UvToViewDirection function in SkyViewLut_CS.hlsl exactly.
//! The LUT is parameterized with sun at azimuth=0 (U=0.5), so we compute the
//! view direction's azimuth relative to the sun's azimuth.
//!
//! @param view_dir Normalized world-space view direction.
//! @param sun_dir Normalized world-space sun direction.
//! @param planet_radius Planet radius in meters.
//! @param camera_altitude_m Camera altitude above surface in meters.
//! @return UV coordinates for sky-view LUT sampling.
float2 GetSkyViewLutUv(float3 view_dir, float3 sun_dir, float planet_radius, float camera_altitude_m)
{
    // cos_zenith from Z component (Z is up).
    float cos_zenith = view_dir.z;
    float sin_zenith = sqrt(max(0.0, 1.0 - cos_zenith * cos_zenith));

    // Compute view-relative azimuth in the XY plane (Z-up). Note that azimuth is
    // ill-defined at zenith (sin_zenith -> 0), so we later blend U toward 0.5
    // smoothly to avoid any discontinuity.
    // Safety: atan2(0,0) is undefined/platform-dependent.
    // When looking straight up/down, view_dir.xy is zero. Use 0 azimuth.
    float view_azimuth = 0.0;
    if (dot(view_dir.xy, view_dir.xy) > EPSILON_SMALL)
    {
        view_azimuth = atan2(view_dir.y, view_dir.x);
    }

    // Same for sun direction, though usually stable.
    float sun_azimuth = 0.0;
    if (dot(sun_dir.xy, sun_dir.xy) > EPSILON_SMALL)
    {
        sun_azimuth = atan2(sun_dir.y, sun_dir.x);
    }

    float relative_azimuth = view_azimuth - sun_azimuth;

    // Normalize to [0, 2π)
    if (relative_azimuth < 0.0)
        relative_azimuth += TWO_PI;
    if (relative_azimuth >= TWO_PI)
        relative_azimuth -= TWO_PI;

    // Compute horizon angle for this altitude.
    float r = planet_radius + camera_altitude_m;
    float rho = planet_radius / r;
    float cos_horizon = -sqrt(max(0.0, 1.0 - rho * rho));

    // Inverse of the non-linear V mapping used in LUT generation (Reference).
    // Mapping uses Angles, not Cosines.
    float zenith_horizon_angle = acos(cos_horizon);
    float beta = PI - zenith_horizon_angle;
    float view_zenith_angle = acos(clamp(cos_zenith, -1.0, 1.0));

    float v;
    if (view_zenith_angle < zenith_horizon_angle)
    {
        // Sky: map [0, ZenithHorizonAngle] -> [0, 0.5]
        float coord = view_zenith_angle / zenith_horizon_angle;
        coord = 1.0 - coord;
        coord = sqrt(max(0.0, coord));
        coord = 1.0 - coord;
        v = coord * 0.5;
    }
    else
    {
        // Ground: map [ZenithHorizonAngle, PI] -> [0.5, 1]
        float coord = (view_zenith_angle - zenith_horizon_angle) / beta;
        coord = sqrt(max(0.0, coord));
        v = (coord + 1.0) * 0.5;
    }

    // Azimuth U mapping (Reference Squared Distribution)
    // Mapping: u = sqrt(0.5 * (1 - cos(phi)))
    // Range: u in [0, 1] maps to phi in [0, PI] (Symmetric)
    // cos(phi) = cos(relative_azimuth)
    // Note: dot(view_xy, sun_xy) gives cos(phi) directly if vectors are normalized in 2D or 3D horizontal plane.

    // Since we computed relative_azimuth already:
    float cos_phi = cos(relative_azimuth);
    float u = sqrt(max(0.0, 0.5 * (1.0 - cos_phi)));

    return float2(u, v);
}

static inline float2 ApplyHalfTexelOffset(float2 uv, float lut_width, float lut_height)
{
    uv = uv * float2((lut_width - 1.0) / lut_width, (lut_height - 1.0) / lut_height);
    uv += float2(0.5 / lut_width, 0.5 / lut_height);
    return uv;
}

//! Converts a camera altitude (meters) to a fractional slice index.
//!
//! Uses the inverse of the mapping used during LUT generation (centered bins):
//!   Linear (mode 0): t = h / H                -> slice_frac = t * slices - 0.5
//!   Log    (mode 1): t = log2(1 + h / H)      -> slice_frac = t * slices - 0.5
//!
//! The -0.5 accounts for the centered-bin convention where slice i represents
//! altitude at (i + 0.5) / slices. The result is clamped to [0, slices - 1].
//!
//! @param altitude_m       Camera altitude above ground in meters.
//! @param atmosphere_h     Total atmosphere height in meters.
//! @param slices           Number of altitude slices in the LUT.
//! @param mapping_mode     0 = linear, 1 = log.
//! @return Fractional slice index (e.g. 3.7 means lerp 70% from slice 3 to 4).
float AltitudeToSliceFrac(float altitude_m, float atmosphere_h,
                          uint slices, uint mapping_mode)
{
    // Clamp altitude into valid range to avoid out-of-bounds slice.
    float h = clamp(altitude_m, 0.0, atmosphere_h);

    float t;
    if (mapping_mode == 1)
    {
        // Inverse of h = H * (2^t - 1) → t = log2(1 + h / H).
        t = log2(1.0 + h / atmosphere_h);
    }
    else
    {
        // Inverse of h = H * t → t = h / H.
        t = h / atmosphere_h;
    }

    // Convert normalised t back to centered-bin slice index.
    // During generation: t_gen = (slice + 0.5) / slices
    // Inverse: slice_frac = t * slices - 0.5
    float slice_frac = t * float(slices) - 0.5;
    return clamp(slice_frac, 0.0, float(slices) - 1.0);
}

//! Loads a single texel from a Texture2DArray using integer coordinates.
//!
//! Uses Load() instead of SampleLevel() so that slice blending is fully
//! manual (no HW trilinear ambiguity) [P7].
static inline float4 LoadSkyViewTexel(Texture2DArray<float4> lut,
                                       int2 texel, int slice)
{
    return lut.Load(int4(texel, slice, 0));
}

//! Samples a single slice of the sky-view LUT with bilinear filtering.
//!
//! Manually performs bilinear interpolation using four Load() calls so that
//! the slice index is exactly controlled (avoids hardware trilinear across
//! array slices which is undefined for Texture2DArray).
//!
//! @param lut          The sky-view Texture2DArray.
//! @param uv           Normalised UV for the slice (after half-texel offset).
//! @param lut_width    Width of the LUT in texels.
//! @param lut_height   Height of the LUT in texels.
//! @param slice        Integer slice index.
//! @return Bilinearly filtered float4.
static inline float4 SampleSliceBilinear(Texture2DArray<float4> lut,
                                          float2 uv, float lut_width,
                                          float lut_height, int slice)
{
    // Convert UV to texel-space (origin at texel center 0).
    float tx = uv.x * lut_width  - 0.5;
    float ty = uv.y * lut_height - 0.5;

    int x0 = (int)floor(tx);
    int y0 = (int)floor(ty);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx = tx - float(x0);
    float fy = ty - float(y0);

    // Clamp to valid texel range.
    x0 = clamp(x0, 0, (int)lut_width  - 1);
    x1 = clamp(x1, 0, (int)lut_width  - 1);
    y0 = clamp(y0, 0, (int)lut_height - 1);
    y1 = clamp(y1, 0, (int)lut_height - 1);

    float4 s00 = LoadSkyViewTexel(lut, int2(x0, y0), slice);
    float4 s10 = LoadSkyViewTexel(lut, int2(x1, y0), slice);
    float4 s01 = LoadSkyViewTexel(lut, int2(x0, y1), slice);
    float4 s11 = LoadSkyViewTexel(lut, int2(x1, y1), slice);

    return lerp(lerp(s00, s10, fx), lerp(s01, s11, fx), fy);
}

//! Samples the sky-view LUT array with manual two-slice altitude blending.
//!
//! The sky-view LUT is now a Texture2DArray where each slice covers a
//! different altitude band. This function:
//!   1. Computes the fractional slice from camera altitude [P9].
//!   2. Bilinearly samples the two neighboring integer slices.
//!   3. Lerps between them by the fractional part.
//!   4. Applies the zenith azimuth-averaging filter using the same array
//!      sampling path [P8].
//!
//! @param lut_slot         Bindless SRV index for the sky-view LUT array.
//! @param lut_width        LUT texture width (per slice).
//! @param lut_height       LUT texture height (per slice).
//! @param view_dir         Normalized world-space view direction.
//! @param sun_dir          Normalized world-space sun direction.
//! @param planet_radius    Planet radius in meters.
//! @param camera_altitude_m Camera altitude above surface in meters.
//! @param slices           Number of altitude slices in the array.
//! @param alt_mapping_mode Altitude mapping mode (0 = linear, 1 = log).
//! @param atmosphere_height_m Total atmosphere height in meters.
//! @return float4(inscattered_radiance.rgb, transmittance).
float4 SampleSkyViewLut(
    uint lut_slot,
    float lut_width,
    float lut_height,
    float3 view_dir,
    float3 sun_dir,
    float planet_radius,
    float camera_altitude_m,
    uint slices,
    uint alt_mapping_mode,
    float atmosphere_height_m)
{
    if (lut_slot == K_INVALID_BINDLESS_INDEX)
    {
        // No LUT available, return zero inscatter, full transmittance.
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    // Load the Texture2DArray instead of Texture2D [P7].
    Texture2DArray<float4> lut = ResourceDescriptorHeap[lut_slot];

    // Compute fractional slice index from camera altitude.
    float slice_frac = AltitudeToSliceFrac(
        camera_altitude_m, atmosphere_height_m, slices, alt_mapping_mode);

    int slice_lo = (int)floor(slice_frac);
    int slice_hi = min(slice_lo + 1, (int)slices - 1);
    float blend = slice_frac - float(slice_lo);

    float2 uv_base = GetSkyViewLutUv(view_dir, sun_dir, planet_radius, camera_altitude_m);
    float2 uv = ApplyHalfTexelOffset(uv_base, lut_width, lut_height);

    // Sample both neighboring slices and lerp.
    float4 sample_lo = SampleSliceBilinear(lut, uv, lut_width, lut_height, slice_lo);
    float4 sample_hi = SampleSliceBilinear(lut, uv, lut_width, lut_height, slice_hi);
    float4 base_sample = lerp(sample_lo, sample_hi, blend);

    // Zenith azimuth-averaging filter [P8].
    // Near zenith, azimuth becomes ill-defined (view_dir.xy ~ 0). We average
    // 4 azimuth-offset samples to suppress flickering.
    const float cos_zenith = view_dir.z;
    const float sin_zenith = sqrt(max(0.0, 1.0 - cos_zenith * cos_zenith));
    const float zenith_weight = saturate(1.0 - (sin_zenith / kZenithFilterThreshold));

    if (zenith_weight > 0.0)
    {
        // Offsets of 0.00, 0.25, 0.50, 0.75 in U cover the full azimuth ring.
        static const float kAzOffsets[4] = { 0.00, 0.25, 0.50, 0.75 };

        float4 acc = float4(0.0, 0.0, 0.0, 0.0);
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            float2 offset_uv = ApplyHalfTexelOffset(
                float2(frac(uv_base.x + kAzOffsets[i]), uv_base.y),
                lut_width, lut_height);

            // Each azimuth sample also needs the two-slice lerp.
            float4 lo = SampleSliceBilinear(lut, offset_uv, lut_width, lut_height, slice_lo);
            float4 hi = SampleSliceBilinear(lut, offset_uv, lut_width, lut_height, slice_hi);
            acc += lerp(lo, hi, blend);
        }

        float4 zenith_filtered = acc * 0.25;
        return lerp(base_sample, zenith_filtered, zenith_weight);
    }

    return base_sample;
}

//! Computes the sun disk contribution with proper horizon clipping.
//!
//! The sun disk is rendered based on where the view ray intersects the sun.
//! When the sun is partially below the horizon, only the portion of the disk
//! visible above the horizon is rendered - the view direction must be above
//! the horizon to see any sun disk.
//!
//! @param view_dir Normalized world-space view direction.
//! @param sun_dir Normalized direction toward the sun.
//! @param angular_radius_radians Angular radius of the sun disk.
//! @param sun_illuminance Sun illuminance in Lux (color * intensity).
//! @param planet_radius Planet radius in meters.
//! @param camera_altitude_m Camera altitude above surface in meters.
//! @return Sun disk radiance contribution.
float3 ComputeSunDisk(
    float3 view_dir,
    float3 sun_dir,
    float angular_radius_radians,
    float3 sun_illuminance,
    float planet_radius,
    float camera_altitude_m)
{
    // Compute the geometric horizon angle from camera altitude.
    float r = planet_radius + camera_altitude_m;
    float rho = planet_radius / r;
    float cos_horizon = -sqrt(max(0.0, 1.0 - rho * rho));

    // View direction must be above horizon to see anything
    float view_cos_zenith = view_dir.z;
    if (view_cos_zenith < cos_horizon)
    {
        return float3(0.0, 0.0, 0.0);
    }

    // Cosine of angle between view and sun direction.
    float cos_angle = dot(view_dir, sun_dir);
    float cos_sun_radius = cos(angular_radius_radians);

    // Early out if not looking at sun disk
    if (cos_angle < cos_sun_radius - 0.01)
    {
        return float3(0.0, 0.0, 0.0);
    }

    // Sun disk with soft edge (anti-aliasing).
    float edge_softness = kSunDiskEdgeSoftness;
    float disk_factor = smoothstep(
        cos_sun_radius - edge_softness,
        cos_sun_radius + edge_softness,
        cos_angle);

    // The sun_illuminance parameter is the sun's illuminance (Lux). To get the
    // radiance (Nits) for the sun disk, we must divide by the solid angle of
    // the sun disk: Omega = 2 * PI * (1 - cos(angular_radius)).
    float omega_sun = TWO_PI * (1.0 - cos_sun_radius);
    float3 sun_radiance = sun_illuminance / max(omega_sun, kAtmosphereEpsilon);

    // Prevent FP16 overflow (max ~65504).
    // Physical sun radiance can easily exceed this (e.g. 10^9 nits), resulting
    // in Infinity in the texture. Subsequent filtering/convolutions can then
    // produce NaNs (Black) if they multiply Inf by 0.
    // Clamping to a safe max ensuring it's still "very bright" but valid.
    if (any(sun_radiance > kSunRadianceSafeMax))
    {
        sun_radiance = min(sun_radiance, kSunRadianceSafeMax);
    }

    // Now handle partial visibility: if view is above horizon but looking
    // toward a sun that's partially below, we need to fade based on how
    // much of the sun disk is above the horizon line.
    //
    // The "effective" sun position for this pixel is where the view ray
    // would hit the sun disk. Since we're looking at the disk, the relevant
    // question is: does the view direction point above the horizon?
    // We already checked view_cos_zenith >= cos_horizon above.
    //
    // Additionally, fade out the disk as the sun center approaches/crosses
    // the horizon to avoid a harsh cut.
    float sun_cos_zenith = sun_dir.z;

    // How far is sun center above horizon? (positive = above, negative = below)
    float sun_above_horizon = sun_cos_zenith - cos_horizon;

    // Fade based on sun center position relative to horizon.
    // Full brightness when sun center is 1 angular radius above horizon.
    // Zero when sun center is 1 angular radius below horizon.
    float horizon_fade = saturate((sun_above_horizon + angular_radius_radians)
                                   / (2.0 * angular_radius_radians));

    float3 final_sun = sun_radiance * disk_factor * horizon_fade;

    // Safety: prevent NaNs from exploding the output
    if (any(isnan(final_sun)) || any(isinf(final_sun)))
    {
        // Fallback to a safe bright value to indicate error without black hole
         final_sun = min(sun_illuminance, float3(100.0, 100.0, 100.0));
    }

    return final_sun;
}

//! Computes full atmospheric sky color from LUTs.
//!
//! @param atmo Atmosphere parameters from EnvironmentStaticData.
//! @param view_dir Normalized world-space view direction.
//! @param sun_dir Normalized direction toward the sun.
//! @param sun_illuminance Sun illuminance (Lux).
//! @param planet_radius Planet radius in meters.
//! @param camera_altitude_m Camera altitude above surface in meters.
//! @return Final sky radiance.
float3 ComputeAtmosphereSkyColor(
    GpuSkyAtmosphereParams atmo,
    float3 view_dir,
    float3 sun_dir,
    float3 sun_illuminance,
    float planet_radius,
    float camera_altitude_m)
{
    // Sample sky-view LUT for inscattered radiance.
    // The LUT stores physically-based inscatter: transmittance naturally handles
    // sunset/twilight dimming as light travels through longer atmospheric paths.
    float4 sky_sample = SampleSkyViewLut(
        atmo.sky_view_lut_slot,
        atmo.sky_view_lut_width,
        atmo.sky_view_lut_height,
        view_dir,
        sun_dir,
        planet_radius,
        camera_altitude_m,
        atmo.sky_view_lut_slices,
        atmo.sky_view_alt_mapping_mode,
        atmo.atmosphere_height_m);

    float3 inscatter = sky_sample.rgb;
    float transmittance = sky_sample.a;

    // Sky-view LUT now stores absolute radiance (Nits).
    // No need to multiply by sun_illuminance again.
    // inscatter *= sun_illuminance;

    // Optionally add sun disk.
    float3 sun_contribution = float3(0.0, 0.0, 0.0);
    if (atmo.sun_disk_enabled)
    {
        // Sample transmittance toward sun to attenuate sun disk.
        float cos_sun_zenith = sun_dir.z; // Z is up

        float3 sun_transmittance = SampleTransmittanceLut(
            atmo,
            atmo.transmittance_lut_slot,
            atmo.transmittance_lut_width,
            atmo.transmittance_lut_height,
            cos_sun_zenith,
            camera_altitude_m,
            atmo.atmosphere_height_m);

        float3 attenuated_sun_illuminance = sun_illuminance * sun_transmittance;

        sun_contribution = ComputeSunDisk(
            view_dir,
            sun_dir,
            atmo.sun_disk_angular_radius_radians,
            attenuated_sun_illuminance,
            planet_radius,
            camera_altitude_m);

        // Sun disk is seen through the atmosphere transmittance along view.
        sun_contribution *= transmittance;
    }

    return inscatter + sun_contribution;
}

#endif // SKY_ATMOSPHERE_SAMPLING_HLSLI

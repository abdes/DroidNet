//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ATMOSPHERELIGHTINGHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ATMOSPHERELIGHTINGHELPERS_HLSLI

#include "Renderer/EnvironmentHelpers.hlsli"
#include "Renderer/EnvironmentViewHelpers.hlsli"
#include "Atmosphere/AtmosphereSampling.hlsli"

//! Computes sun transmittance for a shaded world-space point.
//!
//! Returns the atmospheric transmittance from the shaded point toward the sun.
//! This accounts for both the sun's elevation and the point's altitude above
//! the virtual planet surface. When atmosphere is disabled
//! (`atmo.enabled == 0`), the result is full transmittance.
//!
//! In Oxygen's coordinate system (+Z up, -Y forward):
//! - world space remains governed by `Oxygen/Core/Constants.h`
//! - the atmosphere helper derives altitude from the radial vector relative to
//!   the virtual planet center instead of assuming a flat +Z-only height
//! - direct sunlight is geometrically occluded once the sun falls below the
//!   local horizon, regardless of LUT availability
//!
//! This helper is shared by forward and deferred lighting so both paths apply
//! the same atmosphere-lighting contract.
//!
//! @param world_pos World-space position of the shaded point.
//! @param atmo Atmosphere parameters from environment data.
//! @param sun_dir Direction toward the sun (normalized).
//! @return RGB transmittance:
//!   - `0` when the sun is below the local horizon
//!   - `1` when no atmosphere or no LUT is available above the horizon
//!   - sampled LUT transmittance otherwise
float3 ComputeSunTransmittance(
    float3 world_pos,
    GpuSkyAtmosphereParams atmo,
    float3 sun_dir)
{
    // No atmosphere = no attenuation.
    if (!atmo.enabled) {
        return float3(1.0, 1.0, 1.0);
    }

    // Derive the local vertical and altitude from the virtual planet center so
    // the result remains valid for elevated points and large-scale worlds.
    const float3 planet_center = GetPlanetCenterWS();
    const float3 to_surface = world_pos - planet_center;
    const float height = length(to_surface);
    const float altitude = max(height - atmo.planet_radius_m, 0.0);
    const float3 local_up = to_surface / max(height, 1e-6);
    const float cos_sun_zenith = dot(local_up, sun_dir);

    // Hard geometric horizon guard: if the sun is below the local horizon,
    // direct illumination must be zero even if the LUT is not ready yet.
    const float cos_horizon = HorizonCosineFromAltitude(atmo.planet_radius_m, altitude);
    if (cos_sun_zenith < cos_horizon) {
        return float3(0.0, 0.0, 0.0);
    }

    // LUT missing/not ready: keep conservative passthrough for above-horizon
    // sun to avoid startup blackouts while still honoring horizon occlusion.
    if (atmo.transmittance_lut_slot == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0, 1.0, 1.0);
    }

    // Sample the transmittance LUT directly. The LUT stores resolved
    // transmittance, so no additional conversion is needed here.
    return SampleTransmittanceLut(
        atmo,
        atmo.transmittance_lut_slot,
        atmo.transmittance_lut_width,
        atmo.transmittance_lut_height,
        cos_sun_zenith,
        altitude,
        atmo.atmosphere_height_m);
}

#endif // OXYGEN_D3D12_SHADERS_RENDERER_ATMOSPHERELIGHTINGHELPERS_HLSLI

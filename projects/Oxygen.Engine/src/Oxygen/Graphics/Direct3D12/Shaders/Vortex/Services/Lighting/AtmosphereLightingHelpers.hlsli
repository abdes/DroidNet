//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_ATMOSPHERELIGHTINGHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_ATMOSPHERELIGHTINGHELPERS_HLSLI

#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/Environment/EnvironmentViewHelpers.hlsli"
#include "Vortex/Services/Environment/ParityTransmittance.hlsli"

// Cross-domain bridge:
// - world_pos and planet center remain authored/published in world-space meters
// - GpuSkyAtmosphereParams is now atmosphere-space km / 1/km
// This helper converts the non-atmosphere consumer's world-space distance into
// the atmosphere-space unit expected by the LUT samplers.
static inline float WorldMetersToAtmosphereKmForLighting(float meters)
{
    return meters * 1.0e-3f;
}

static inline float3 WorldMetersToAtmosphereKmForLighting(float3 meters)
{
    return meters * 1.0e-3f;
}

float3 ComputeSunTransmittance(
    float3 world_pos,
    GpuSkyAtmosphereParams atmo,
    float3 sun_dir)
{
    if (!atmo.enabled) {
        return float3(1.0, 1.0, 1.0);
    }

    const float3 planet_center = GetPlanetCenterWS();
    const float3 to_surface = world_pos - planet_center;
    if (atmo.transmittance_lut_slot == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0, 1.0, 1.0);
    }

    return AnalyticalPlanetOccludedTransmittance(
        WorldMetersToAtmosphereKmForLighting(to_surface),
        sun_dir,
        atmo.transmittance_lut_slot,
        atmo.transmittance_lut_width,
        atmo.transmittance_lut_height,
        atmo.planet_radius_km,
        atmo.atmosphere_height_km);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_ATMOSPHERELIGHTINGHELPERS_HLSLI

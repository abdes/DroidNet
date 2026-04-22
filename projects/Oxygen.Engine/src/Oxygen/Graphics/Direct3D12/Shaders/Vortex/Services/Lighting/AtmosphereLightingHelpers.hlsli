//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_ATMOSPHERELIGHTINGHELPERS_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_ATMOSPHERELIGHTINGHELPERS_HLSLI

#include "Vortex/Contracts/EnvironmentHelpers.hlsli"
#include "Vortex/Contracts/EnvironmentViewHelpers.hlsli"
#include "Vortex/Services/Environment/AtmosphereSampling.hlsli"

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
    const float height = length(to_surface);
    const float altitude = max(height - atmo.planet_radius_m, 0.0);
    const float3 local_up = to_surface / max(height, 1e-6);
    const float cos_sun_zenith = dot(local_up, sun_dir);

    const float cos_horizon = HorizonCosineFromAltitude(atmo.planet_radius_m, altitude);
    if (cos_sun_zenith < cos_horizon) {
        return float3(0.0, 0.0, 0.0);
    }

    if (atmo.transmittance_lut_slot == K_INVALID_BINDLESS_INDEX) {
        return float3(1.0, 1.0, 1.0);
    }

    return SampleTransmittanceLut(
        atmo,
        atmo.transmittance_lut_slot,
        atmo.transmittance_lut_width,
        atmo.transmittance_lut_height,
        cos_sun_zenith,
        altitude,
        atmo.atmosphere_height_m);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_ATMOSPHERELIGHTINGHELPERS_HLSLI

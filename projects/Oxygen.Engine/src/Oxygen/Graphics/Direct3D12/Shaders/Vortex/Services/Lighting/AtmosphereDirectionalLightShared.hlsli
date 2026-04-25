//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_ATMOSPHEREDIRECTIONALLIGHTSHARED_HLSLI
#define OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_ATMOSPHEREDIRECTIONALLIGHTSHARED_HLSLI

#include "Vortex/Contracts/Environment/EnvironmentHelpers.hlsli"
#include "Vortex/Shared/Geometry.hlsli"
#include "Vortex/Services/Lighting/AtmosphereLightingHelpers.hlsli"

static const uint kDirectionalLightAtmosphereModeFlagAuthority = 1u << 0u;
static const uint kDirectionalLightAtmosphereModeFlagPerPixelTransmittance = 1u << 1u;
static const uint kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance = 1u << 2u;

static inline float3 ResolveDirectionalLightAtmosphereTransmittance(
    float3 world_position,
    float3 light_direction_ws,
    float3 baked_transmittance_toward_sun,
    uint atmosphere_mode_flags)
{
    if ((atmosphere_mode_flags & kDirectionalLightAtmosphereModeFlagAuthority) == 0u) {
        return 1.0f.xxx;
    }

    if ((atmosphere_mode_flags & kDirectionalLightAtmosphereModeFlagPerPixelTransmittance) != 0u) {
        EnvironmentStaticData env_data = (EnvironmentStaticData)0;
        if (!LoadEnvironmentStaticData(env_data) || env_data.atmosphere.enabled == 0u) {
            return 1.0f.xxx;
        }

        const float3 safe_light_direction = normalize(
            abs(dot(light_direction_ws, light_direction_ws)) > 1.0e-8f
                ? light_direction_ws
                : float3(0.0f, -1.0f, 0.0f));

        return saturate(ComputeSunTransmittance(
            world_position,
            env_data.atmosphere,
            safe_light_direction));
    }

    if ((atmosphere_mode_flags & kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance) != 0u) {
        return saturate(baked_transmittance_toward_sun);
    }

    return 1.0f.xxx;
}

static inline float3 ResolveDirectionalLightAtmosphereRadiance(
    float3 world_position,
    float3 light_direction_ws,
    float3 baked_transmittance_toward_sun,
    uint atmosphere_mode_flags,
    float3 raw_radiance)
{
    return raw_radiance * ResolveDirectionalLightAtmosphereTransmittance(
        world_position,
        light_direction_ws,
        baked_transmittance_toward_sun,
        atmosphere_mode_flags);
}

#endif // OXYGEN_D3D12_SHADERS_VORTEX_SERVICES_LIGHTING_ATMOSPHEREDIRECTIONALLIGHTSHARED_HLSLI

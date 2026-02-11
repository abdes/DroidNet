//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI

// Mirrors oxygen::engine::EnvironmentDynamicData (sizeof = 208)
// Per-frame environment payload bound as root CBV at b3.

struct LightCullingConfig {
    uint bindless_cluster_grid_slot;
    uint bindless_cluster_index_list_slot;
    uint cluster_dim_x;
    uint cluster_dim_y;
    uint cluster_dim_z;
    uint tile_size_px;
    float z_near;
    float z_far;
    float z_scale;
    float z_bias;
    uint max_lights_per_cluster;
    uint _pad;
};

struct AtmosphereData {
    uint flags;
    float sky_view_lut_slice;
    float planet_to_sun_cos_zenith;
    float aerial_perspective_distance_scale;
    float aerial_scattering_strength;
    uint3 _pad;
    float4 planet_center_ws_pad;
    float4 planet_up_ws_camera_altitude_m;
};

struct SyntheticSunData {
    uint enabled;
    float cos_zenith;
    uint2 _pad;
    float4 direction_ws_illuminance;
    float4 color_rgb_intensity;
};

struct EnvironmentDynamicData
{
    LightCullingConfig light_culling;
    AtmosphereData atmosphere;
    SyntheticSunData sun;
};

ConstantBuffer<EnvironmentDynamicData> EnvironmentDynamicData : register(b3, space0);

//=== Inline Accessors ===----------------------------------------------------//

/**
 * Returns the direction towards the sun.
 */
static inline float3 GetSunDirectionWS()
{
    return EnvironmentDynamicData.sun.direction_ws_illuminance.xyz;
}

/**
 * Returns the sun's illuminance at the top of the atmosphere.
 */
static inline float GetSunIlluminance()
{
    return EnvironmentDynamicData.sun.direction_ws_illuminance.w;
}

/**
 * Returns the sun disk color.
 */
static inline float3 GetSunColorRGB()
{
    return EnvironmentDynamicData.sun.color_rgb_intensity.xyz;
}

/**
 * Returns the sun intensity.
 */
static inline float GetSunIntensity()
{
    return EnvironmentDynamicData.sun.color_rgb_intensity.w;
}

/**
 * Returns the designated sun radiance proxy (color * intensity).
 */
static inline float3 GetSunLuminanceRGB()
{
    // Return RGB illuminance derived from the authored lux scalar.
    // (Historically this used `GetSunIntensity()`, but the engine now treats
    // the sun intensity UI/control as illuminance in lux.)
    return GetSunColorRGB() * GetSunIlluminance();
}

/**
 * Returns the planet center in world-space.
 */
static inline float3 GetPlanetCenterWS()
{
    return EnvironmentDynamicData.atmosphere.planet_center_ws_pad.xyz;
}

// NOTE: GetPlanetRadiusM() has been REMOVED.
// Planet radius is a static parameter - use env_data.atmosphere.planet_radius_m
// from EnvironmentStaticData after loading it with LoadEnvironmentStaticData().

/**
 * Returns the planet up vector in world-space.
 */
static inline float3 GetPlanetUpWS()
{
    return EnvironmentDynamicData.atmosphere.planet_up_ws_camera_altitude_m.xyz;
}

/**
 * Returns the camera altitude relative to the planet's surface (sea-level).
 */
static inline float GetCameraAltitudeM()
{
    return EnvironmentDynamicData.atmosphere.planet_up_ws_camera_altitude_m.w;
}

/**
 * Returns the distance scale for aerial perspective.
 */
static inline float GetAerialPerspectiveDistanceScale()
{
    return EnvironmentDynamicData.atmosphere.aerial_perspective_distance_scale;
}

/**
 * Returns the scattering strength for aerial perspective.
 */
static inline float GetAerialScatteringStrength()
{
    return EnvironmentDynamicData.atmosphere.aerial_scattering_strength;
}

/**
 * Returns the atmosphere sampling flags.
 */
static inline uint GetAtmosphereFlags()
{
    return EnvironmentDynamicData.atmosphere.flags;
}

/**
 * Returns the SkyView LUT slice index for the current view.
 */
static inline float GetSkyViewLutSlice()
{
    return EnvironmentDynamicData.atmosphere.sky_view_lut_slice;
}

/**
 * Returns the cosine of the sun's zenith angle relative to the planet.
 */
static inline float GetPlanetToSunCosZenith()
{
    return EnvironmentDynamicData.atmosphere.planet_to_sun_cos_zenith;
}

/**
 * Returns true if the synthetic sun is enabled.
 */
static inline bool HasSunLight()
{
    return (EnvironmentDynamicData.sun.enabled != 0);
}

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI

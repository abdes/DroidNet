//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI

// Mirrors oxygen::engine::EnvironmentDynamicData (sizeof = 192)
// Per-frame environment payload bound as root CBV at b3.
struct EnvironmentDynamicData
{
    // Exposure
    float exposure;

    // Cluster grid bindless slots (from LightCullingPass)
    uint bindless_cluster_grid_slot;
    uint bindless_cluster_index_list_slot;

    // Padding to complete the first 16-byte register.
    uint _pad0;

    // Cluster grid dimensions
    uint cluster_dim_x;
    uint cluster_dim_y;
    uint cluster_dim_z;
    uint tile_size_px;

    // Z-binning parameters for clustered lighting
    float z_near;
    float z_far;
    float z_scale;
    float z_bias;

    // Per-view aerial perspective controls (SkyAtmosphere).
    float aerial_perspective_distance_scale;
    float aerial_scattering_strength;

    // Atmospheric debug/feature flags (bitfield)
    // bit0: use LUT sampling when available
    // bit1: visualize LUT
    // bit2: force analytic fallback (ignore LUT)
    // bit3: use override sun values
    uint atmosphere_flags;

    // 1 = sun enabled; 0 = fallback to default sun.
    uint sun_enabled;

    // Designated sun light (toward the sun, not incoming radiance).
    // xyz = direction, w = illuminance.
    float4 sun_direction_ws_illuminance;

    // Sun spectral payload.
    // xyz = color_rgb (linear, not premultiplied), w = intensity.
    float4 sun_color_rgb_intensity;

    // Debug override sun for testing (internal only).
    // xyz = direction, w = illuminance.
    float4 override_sun_direction_ws_illuminance;

    // x = override_sun_enabled; remaining lanes reserved for future flags.
    uint4 override_sun_flags;

    // Override sun spectral payload.
    // xyz = color_rgb (linear, not premultiplied), w = intensity.
    float4 override_sun_color_rgb_intensity;

    // Planet/frame context for atmospheric sampling.
    // xyz = planet center, w = padding (unused).
    // Note: planet_radius is in EnvironmentStaticData (static parameter).
    float4 planet_center_ws_pad;

    // xyz = planet up, w = camera altitude (m).
    float4 planet_up_ws_camera_altitude_m;

    // x = sky view LUT slice, y = planet_to_sun_cos_zenith.
    float4 sky_view_lut_slice_cos_zenith;
};

/**
 * Global declaration for the EnvironmentDynamicData root CBV.
 *
 * This payload contains high-frequency environment data like exposure and
 * clustered lighting configuration. It is bound at the engine-reserved root
 * CBV slot b3.
 */
ConstantBuffer<EnvironmentDynamicData> EnvironmentDynamicData : register(b3, space0);

/**
 * Returns whether the designated sun light values are valid.
 */
static inline bool HasSunLight()
{
    return EnvironmentDynamicData.sun_enabled != 0;
}

/**
 * Returns the designated sun direction (world space).
 * Respects the override sun if enabled.
 */
static inline float3 GetSunDirectionWS()
{
    if (EnvironmentDynamicData.override_sun_flags.x != 0) {
        return EnvironmentDynamicData.override_sun_direction_ws_illuminance.xyz;
    }
    return EnvironmentDynamicData.sun_direction_ws_illuminance.xyz;
}

/**
 * Returns the designated sun illuminance.
 * Respects the override sun if enabled.
 */
static inline float GetSunIlluminance()
{
    if (EnvironmentDynamicData.override_sun_flags.x != 0) {
        return EnvironmentDynamicData.override_sun_direction_ws_illuminance.w;
    }
    return EnvironmentDynamicData.sun_direction_ws_illuminance.w;
}

/**
 * Returns the designated sun color (linear RGB).
 * Respects the override sun if enabled.
 */
static inline float3 GetSunColorRGB()
{
    if (EnvironmentDynamicData.override_sun_flags.x != 0) {
        return EnvironmentDynamicData.override_sun_color_rgb_intensity.xyz;
    }
    return EnvironmentDynamicData.sun_color_rgb_intensity.xyz;
}

/**
 * Returns the designated sun intensity.
 * Respects the override sun if enabled.
 */
static inline float GetSunIntensity()
{
    if (EnvironmentDynamicData.override_sun_flags.x != 0) {
        return EnvironmentDynamicData.override_sun_color_rgb_intensity.w;
    }
    return EnvironmentDynamicData.sun_color_rgb_intensity.w;
}

/**
 * Returns the designated sun radiance proxy (color * intensity).
 */
static inline float3 GetSunLuminanceRGB()
{
    return GetSunColorRGB() * GetSunIntensity();
}

/**
 * Returns the planet center (world space).
 */
static inline float3 GetPlanetCenterWS()
{
    return EnvironmentDynamicData.planet_center_ws_pad.xyz;
}

// NOTE: GetPlanetRadiusM() has been REMOVED.
// Planet radius is a static parameter - use env_data.atmosphere.planet_radius_m
// from EnvironmentStaticData after loading it with LoadEnvironmentStaticData().

/**
 * Returns the planet up vector (world space).
 */
static inline float3 GetPlanetUpWS()
{
    return EnvironmentDynamicData.planet_up_ws_camera_altitude_m.xyz;
}

/**
 * Returns the camera altitude relative to the planet in meters.
 */
static inline float GetCameraAltitudeM()
{
    return EnvironmentDynamicData.planet_up_ws_camera_altitude_m.w;
}

/**
 * Returns the sky view LUT slice for the current view.
 */
static inline float GetSkyViewLutSlice()
{
    return EnvironmentDynamicData.sky_view_lut_slice_cos_zenith.x;
}

/**
 * Returns the cosine of the zenith angle between planet up and sun direction.
 */
static inline float GetPlanetToSunCosZenith()
{
    return EnvironmentDynamicData.sky_view_lut_slice_cos_zenith.y;
}

/**
 * Returns whether the debug override sun is enabled.
 */
static inline bool IsOverrideSunEnabled()
{
    return EnvironmentDynamicData.override_sun_flags.x != 0;
}

/**
 * Returns the debug override sun direction (world space).
 */
static inline float3 GetOverrideSunDirectionWS()
{
    return EnvironmentDynamicData.override_sun_direction_ws_illuminance.xyz;
}

/**
 * Returns the debug override sun illuminance.
 */
static inline float GetOverrideSunIlluminance()
{
    return EnvironmentDynamicData.override_sun_direction_ws_illuminance.w;
}

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTDYNAMICDATA_HLSLI

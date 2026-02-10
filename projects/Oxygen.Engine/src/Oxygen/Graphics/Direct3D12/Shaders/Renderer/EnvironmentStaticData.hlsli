//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTSTATICDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTSTATICDATA_HLSLI

// Atmosphere density profile structures (used by GpuSkyAtmosphereParams)
struct AtmosphereDensityLayer
{
    float width_m;
    float exp_term;
    float linear_term;
    float constant_term;
};

struct AtmosphereDensityProfile
{
    AtmosphereDensityLayer layers[2];
};

static const uint FOG_MODEL_EXPONENTIAL_HEIGHT = 0u;
static const uint FOG_MODEL_VOLUMETRIC = 1u;

static const uint SKY_LIGHT_SOURCE_CAPTURED_SCENE = 0u;
static const uint SKY_LIGHT_SOURCE_SPECIFIED_CUBEMAP = 1u;

static const uint SKY_SPHERE_SOURCE_CUBEMAP = 0u;
static const uint SKY_SPHERE_SOURCE_SOLID_COLOR = 1u;

static const uint TONEMAPPER_ACES_FITTED = 0u;
static const uint TONEMAPPER_REINHARD = 1u;
static const uint TONEMAPPER_NONE = 2u;
static const uint TONEMAPPER_FILMIC = 3u;

static const uint EXPOSURE_MODE_MANUAL = 0u;
static const uint EXPOSURE_MODE_AUTO = 1u;

// Mirrors oxygen::engine::GpuFogParams (sizeof = 48)
struct GpuFogParams
{
    float3 single_scattering_albedo_rgb;
    float extinction_sigma_t_per_m;

    float height_falloff_per_m;
    float height_offset_m;
    float start_distance_m;
    float max_opacity;

    float anisotropy_g;
    float _pad0;
    uint model;
    uint enabled;
};

// Mirrors oxygen::engine::GpuSkyAtmosphereParams (sizeof = 192)
struct GpuSkyAtmosphereParams
{
    float planet_radius_m;
    float atmosphere_height_m;
    float multi_scattering_factor;
    float aerial_perspective_distance_scale;

    float3 ground_albedo_rgb;
    float sun_disk_angular_radius_radians;

    float3 rayleigh_scattering_rgb;
    float rayleigh_scale_height_m;

    float3 mie_scattering_rgb;
    float mie_scale_height_m;

    // Precomputed Mie extinction (scattering + absorption).
    float3 mie_extinction_rgb;
    float mie_g;

    float3 absorption_rgb;
    float _pad_absorption;

    AtmosphereDensityProfile absorption_density;

    uint sun_disk_enabled;
    uint enabled;
    uint transmittance_lut_slot;
    uint sky_view_lut_slot;

    uint multi_scat_lut_slot;
    uint camera_volume_lut_slot;
    uint blue_noise_slot;
    float transmittance_lut_width;

    float transmittance_lut_height;
    float sky_view_lut_width;
    float sky_view_lut_height;
    uint sky_view_lut_slices;

    uint sky_view_alt_mapping_mode;
    uint _pad0;
    uint _pad1;
    uint _pad2;
};

// Mirrors oxygen::engine::GpuSkyLightParams (sizeof = 64)
struct GpuSkyLightParams
{
    float3 tint_rgb;
    float radiance_scale;

    float diffuse_intensity;
    float specular_intensity;
    uint source;
    uint enabled;

    uint cubemap_slot;
    uint brdf_lut_slot;
    uint irradiance_map_slot;
    uint prefilter_map_slot;

    uint cubemap_max_mip;
    uint prefilter_max_mip;
    uint ibl_generation;
    uint _pad1;
};

// Mirrors oxygen::engine::GpuSkySphereParams (sizeof = 48)
struct GpuSkySphereParams
{
    float3 solid_color_rgb;
    float intensity;

    float3 tint_rgb;
    float rotation_radians;

    uint source;
    uint enabled;
    uint cubemap_slot;
    uint cubemap_max_mip;
};

// Mirrors oxygen::engine::GpuVolumetricCloudParams (sizeof = 64)
struct GpuVolumetricCloudParams
{
    float3 single_scattering_albedo_rgb;
    float base_altitude_m;

    float3 wind_dir_ws;
    float layer_thickness_m;

    float coverage;
    float extinction_sigma_t_per_m;
    float phase_g;
    float _pad0;

    float wind_speed_mps;
    float shadow_strength;
    uint enabled;
    uint _pad1;
};

// Mirrors oxygen::engine::GpuPostProcessParams (sizeof = 64)
struct GpuPostProcessParams
{
    float exposure_compensation;
    float auto_exposure_min_ev;
    float auto_exposure_max_ev;
    float auto_exposure_speed_up;

    float auto_exposure_speed_down;
    float bloom_intensity;
    float bloom_threshold;
    float saturation;

    float contrast;
    float vignette_intensity;
    uint enabled;
    uint _pad0;

    uint tone_mapper;
    uint exposure_mode;
    uint _pad1;
    uint _pad2;
};

// Mirrors oxygen::engine::EnvironmentStaticData (sizeof = 480)
struct EnvironmentStaticData
{
    GpuFogParams fog;
    GpuSkyAtmosphereParams atmosphere;
    GpuSkyLightParams sky_light;
    GpuSkySphereParams sky_sphere;
    GpuVolumetricCloudParams clouds;
    GpuPostProcessParams post_process;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTSTATICDATA_HLSLI

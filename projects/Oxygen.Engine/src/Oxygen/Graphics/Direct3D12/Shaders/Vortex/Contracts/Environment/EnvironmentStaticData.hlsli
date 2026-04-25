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
    float width_km;
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

static const uint GPU_FOG_FLAG_ENABLED = 1u << 0u;
static const uint GPU_FOG_FLAG_HEIGHT_FOG_ENABLED = 1u << 1u;
static const uint GPU_FOG_FLAG_VOLUMETRIC_FOG_AUTHORED = 1u << 2u;
static const uint GPU_FOG_FLAG_RENDER_IN_MAIN_PASS = 1u << 3u;
static const uint GPU_FOG_FLAG_VISIBLE_IN_REFLECTION_CAPTURES = 1u << 4u;
static const uint GPU_FOG_FLAG_VISIBLE_IN_REAL_TIME_SKY_CAPTURES = 1u << 5u;
static const uint GPU_FOG_FLAG_HOLDOUT = 1u << 6u;
static const uint GPU_FOG_FLAG_DIRECTIONAL_INSCATTERING = 1u << 7u;
static const uint GPU_FOG_FLAG_CUBEMAP_AUTHORED = 1u << 8u;
static const uint GPU_FOG_FLAG_CUBEMAP_USABLE = 1u << 9u;

static const uint GPU_VOLUMETRIC_FOG_FLAG_ENABLED = 1u << 0u;
static const uint GPU_VOLUMETRIC_FOG_FLAG_INTEGRATED_SCATTERING_VALID = 1u << 1u;

// Mirrors oxygen::vortex::GpuFogParams (sizeof = 128)
struct GpuFogParams
{
    float3 fog_inscattering_luminance_rgb;
    float primary_density;

    float primary_height_falloff;
    float primary_height_offset_m;
    float secondary_density;
    float secondary_height_falloff;

    float secondary_height_offset_m;
    float start_distance_m;
    float end_distance_m;
    float cutoff_distance_m;

    float max_opacity;
    float min_transmittance;
    float directional_start_distance_m;
    float directional_exponent;

    float3 directional_inscattering_luminance_rgb;
    float cubemap_angle_radians;

    float3 sky_atmosphere_ambient_contribution_color_scale_rgb;
    float cubemap_fade_inv_range;

    float3 inscattering_texture_tint_rgb;
    float cubemap_fade_bias;

    float cubemap_num_mips;
    uint cubemap_srv;
    uint flags;
    uint model;
};

// Mirrors oxygen::vortex::GpuVolumetricFogParams (sizeof = 80)
struct GpuVolumetricFogParams
{
    float3 albedo_rgb;
    float scattering_distribution;

    float3 emissive_rgb;
    float extinction_scale;

    float distance_m;
    float start_distance_m;
    float near_fade_in_distance_m;
    float static_lighting_scattering_intensity;

    uint integrated_light_scattering_srv;
    uint flags;
    uint grid_width;
    uint grid_height;

    uint grid_depth;
    float depth_slice_length_m;
    float inv_depth_slice_length_m;
    uint _pad0;
};

// Mirrors oxygen::vortex::GpuSkyAtmosphereParams (sizeof = 208)
struct GpuSkyAtmosphereParams
{
    float planet_radius_km;
    float atmosphere_height_km;
    float multi_scattering_factor;
    float aerial_perspective_distance_scale;

    float3 ground_albedo_rgb;
    float sun_disk_angular_radius_radians;

    float3 sun_disk_luminance_scale_rgb;
    float _pad_sun_disk_luminance;

    float3 rayleigh_scattering_per_km_rgb;
    float rayleigh_scale_height_km;

    float3 mie_scattering_per_km_rgb;
    float mie_scale_height_km;

    // Precomputed Mie extinction (scattering + absorption).
    float3 mie_extinction_per_km_rgb;
    float mie_g;

    float3 absorption_per_km_rgb;
    float _pad_absorption;

    AtmosphereDensityProfile absorption_density;

    uint sun_disk_enabled;
    uint enabled;
    uint transmittance_lut_slot;
    uint sky_view_lut_slot;

    uint sky_irradiance_lut_slot;
    uint multi_scat_lut_slot;
    uint camera_volume_lut_slot;
    uint distant_sky_light_lut_slot;

    float transmittance_lut_width;
    float transmittance_lut_height;
    float sky_view_lut_width;
    float sky_view_lut_height;

    float sky_irradiance_lut_width;
    float sky_irradiance_lut_height;
    uint sky_view_lut_slices;
    uint sky_view_alt_mapping_mode;
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

// Mirrors oxygen::vortex::EnvironmentStaticData (sizeof = 656)
struct EnvironmentStaticData
{
    GpuFogParams fog;
    GpuVolumetricFogParams volumetric_fog;
    GpuSkyAtmosphereParams atmosphere;
    GpuSkyLightParams sky_light;
    GpuSkySphereParams sky_sphere;
    GpuVolumetricCloudParams clouds;
    GpuPostProcessParams post_process;
};

#endif  // OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTSTATICDATA_HLSLI

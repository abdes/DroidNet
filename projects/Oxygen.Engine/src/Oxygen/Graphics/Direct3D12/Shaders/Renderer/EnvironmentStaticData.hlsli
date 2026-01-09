//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#ifndef OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTSTATICDATA_HLSLI
#define OXYGEN_D3D12_SHADERS_RENDERER_ENVIRONMENTSTATICDATA_HLSLI

static const uint FOG_MODEL_EXPONENTIAL_HEIGHT = 0u;
static const uint FOG_MODEL_VOLUMETRIC = 1u;

static const uint SKY_LIGHT_SOURCE_CAPTURED_SCENE = 0u;
static const uint SKY_LIGHT_SOURCE_SPECIFIED_CUBEMAP = 1u;

static const uint SKY_SPHERE_SOURCE_CUBEMAP = 0u;
static const uint SKY_SPHERE_SOURCE_SOLID_COLOR = 1u;

static const uint TONEMAPPER_ACES_FITTED = 0u;
static const uint TONEMAPPER_REINHARD = 1u;
static const uint TONEMAPPER_NONE = 2u;

static const uint EXPOSURE_MODE_MANUAL = 0u;
static const uint EXPOSURE_MODE_AUTO = 1u;

// Mirrors oxygen::engine::GpuFogParams (sizeof = 48)
struct GpuFogParams
{
    float3 albedo_rgb;
    float density;

    float height_falloff;
    float height_offset_m;
    float start_distance_m;
    float max_opacity;

    float anisotropy_g;
    float scattering_intensity;
    uint model;
    uint enabled;
};

// Mirrors oxygen::engine::GpuSkyAtmosphereParams (sizeof = 128)
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

    float mie_g;
    float absorption_scale_height_m;
    uint sun_disk_enabled;
    uint enabled;

    float3 absorption_rgb;
    uint transmittance_lut_slot;

    uint sky_view_lut_slot;
    float transmittance_lut_width;
    float transmittance_lut_height;
    float sky_view_lut_width;

    float sky_view_lut_height;
    float _reserved5;
    float _reserved6;
    float _reserved7;
};

// Mirrors oxygen::engine::GpuSkyLightParams (sizeof = 48)
struct GpuSkyLightParams
{
    float3 tint_rgb;
    float intensity;

    float diffuse_intensity;
    float specular_intensity;
    uint source;
    uint enabled;

    uint cubemap_slot;
    uint brdf_lut_slot;
    uint _pad1;
    uint _pad2;
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
    uint _pad0;
};

// Mirrors oxygen::engine::GpuVolumetricCloudParams (sizeof = 64)
struct GpuVolumetricCloudParams
{
    float3 albedo_rgb;
    float base_altitude_m;

    float3 wind_dir_ws;
    float layer_thickness_m;

    float coverage;
    float density;
    float extinction_scale;
    float phase_g;

    float wind_speed_mps;
    float shadow_strength;
    uint enabled;
    uint _pad0;
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

// Mirrors oxygen::engine::EnvironmentStaticData (sizeof = 400)
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

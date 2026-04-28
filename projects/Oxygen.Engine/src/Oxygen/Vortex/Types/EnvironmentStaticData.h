//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Atmosphere.h>

namespace oxygen::vortex {

struct alignas(packing::kShaderDataFieldAlignment) AtmosphereDensityLayerGpu {
  float width_km { 0.0F };
  float exp_term { 0.0F };
  float linear_term { 0.0F };
  float constant_term { 0.0F };
};

struct alignas(packing::kShaderDataFieldAlignment) AtmosphereDensityProfileGpu {
  std::array<AtmosphereDensityLayerGpu, 2> layers {};
};

struct alignas(packing::kShaderDataFieldAlignment) GpuFogParams {
  std::array<float, 3> fog_inscattering_luminance_rgb {
    1.0F,
    1.0F,
    1.0F,
  };
  float primary_density { 0.0F };

  float primary_height_falloff { 0.0F };
  float primary_height_offset_m { 0.0F };
  float secondary_density { 0.0F };
  float secondary_height_falloff { 0.0F };

  float secondary_height_offset_m { 0.0F };
  float start_distance_m { 0.0F };
  float end_distance_m { 0.0F };
  float cutoff_distance_m { 0.0F };

  float max_opacity { 1.0F };
  float min_transmittance { 0.0F };
  float directional_start_distance_m { 0.0F };
  float directional_exponent { 1.0F };

  std::array<float, 3> directional_inscattering_luminance_rgb {
    0.0F,
    0.0F,
    0.0F,
  };
  float cubemap_angle_radians { 0.0F };

  std::array<float, 3> sky_atmosphere_ambient_contribution_color_scale_rgb {
    1.0F,
    1.0F,
    1.0F,
  };
  float cubemap_fade_inv_range { 0.0F };

  std::array<float, 3> inscattering_texture_tint_rgb { 1.0F, 1.0F, 1.0F };
  float cubemap_fade_bias { 0.0F };

  float cubemap_num_mips { 0.0F };
  std::uint32_t cubemap_srv { kInvalidBindlessIndex };
  std::uint32_t flags { 0U };
  std::uint32_t model { 0U };
};

inline constexpr std::uint32_t kGpuFogFlagEnabled = 1U << 0U;
inline constexpr std::uint32_t kGpuFogFlagHeightFogEnabled = 1U << 1U;
inline constexpr std::uint32_t kGpuFogFlagVolumetricFogAuthored = 1U << 2U;
inline constexpr std::uint32_t kGpuFogFlagRenderInMainPass = 1U << 3U;
inline constexpr std::uint32_t kGpuFogFlagVisibleInReflectionCaptures = 1U
  << 4U;
inline constexpr std::uint32_t kGpuFogFlagVisibleInRealTimeSkyCaptures = 1U
  << 5U;
inline constexpr std::uint32_t kGpuFogFlagHoldout = 1U << 6U;
inline constexpr std::uint32_t kGpuFogFlagDirectionalInscattering = 1U << 7U;
inline constexpr std::uint32_t kGpuFogFlagCubemapAuthored = 1U << 8U;
inline constexpr std::uint32_t kGpuFogFlagCubemapUsable = 1U << 9U;

inline constexpr std::uint32_t kGpuVolumetricFogFlagEnabled = 1U << 0U;
inline constexpr std::uint32_t kGpuVolumetricFogFlagIntegratedScatteringValid
  = 1U << 1U;

struct alignas(packing::kShaderDataFieldAlignment) GpuVolumetricFogParams {
  std::array<float, 3> albedo_rgb { 1.0F, 1.0F, 1.0F };
  float scattering_distribution { 0.0F };

  std::array<float, 3> emissive_rgb { 0.0F, 0.0F, 0.0F };
  float extinction_scale { 1.0F };

  float distance_m { 0.0F };
  float start_distance_m { 0.0F };
  float near_fade_in_distance_m { 0.0F };
  float static_lighting_scattering_intensity { 1.0F };

  std::uint32_t integrated_light_scattering_srv { kInvalidBindlessIndex };
  std::uint32_t flags { 0U };
  std::uint32_t grid_width { 0U };
  std::uint32_t grid_height { 0U };

  std::uint32_t grid_depth { 0U };
  float depth_slice_length_m { 0.0F };
  float inv_depth_slice_length_m { 0.0F };
  std::uint32_t pad0 { 0U };

  std::array<float, 3> grid_z_params { 0.0F, 1.0F, 1.0F };
  std::uint32_t pad1 { 0U };
};

struct alignas(packing::kShaderDataFieldAlignment) GpuSkyAtmosphereParams {
  float planet_radius_km { engine::atmos::kDefaultPlanetRadiusKm };
  float atmosphere_height_km { engine::atmos::kDefaultAtmosphereHeightKm };
  float multi_scattering_factor { 1.0F };
  float aerial_perspective_distance_scale { 1.0F };

  std::array<float, 3> ground_albedo_rgb { 0.4F, 0.4F, 0.4F };
  float sun_disk_angular_radius_radians { 0.0F };

  std::array<float, 3> sun_disk_luminance_scale_rgb { 1.0F, 1.0F, 1.0F };
  float pad_sun_disk_luminance { 0.0F };

  std::array<float, 3> rayleigh_scattering_per_km_rgb { 0.0F, 0.0F, 0.0F };
  float rayleigh_scale_height_km {
    engine::atmos::kDefaultRayleighScaleHeightKm
  };

  std::array<float, 3> mie_scattering_per_km_rgb { 0.0F, 0.0F, 0.0F };
  float mie_scale_height_km { engine::atmos::kDefaultMieScaleHeightKm };

  std::array<float, 3> mie_extinction_per_km_rgb { 0.0F, 0.0F, 0.0F };
  float mie_g { 0.8F };

  std::array<float, 3> absorption_per_km_rgb { 0.0F, 0.0F, 0.0F };
  float pad_absorption { 0.0F };

  AtmosphereDensityProfileGpu absorption_density {};

  std::uint32_t sun_disk_enabled { 0U };
  std::uint32_t enabled { 0U };
  std::uint32_t transmittance_lut_slot { kInvalidBindlessIndex };
  std::uint32_t sky_view_lut_slot { kInvalidBindlessIndex };

  std::uint32_t sky_irradiance_lut_slot { kInvalidBindlessIndex };
  std::uint32_t multi_scat_lut_slot { kInvalidBindlessIndex };
  std::uint32_t camera_volume_lut_slot { kInvalidBindlessIndex };
  std::uint32_t distant_sky_light_lut_slot { kInvalidBindlessIndex };

  float transmittance_lut_width { 0.0F };
  float transmittance_lut_height { 0.0F };
  float sky_view_lut_width { 0.0F };
  float sky_view_lut_height { 0.0F };

  float sky_irradiance_lut_width { 0.0F };
  float sky_irradiance_lut_height { 0.0F };
  std::uint32_t sky_view_lut_slices { 0U };
  std::uint32_t sky_view_alt_mapping_mode { 0U };
};

struct alignas(packing::kShaderDataFieldAlignment) GpuSkyLightParams {
  std::array<float, 3> tint_rgb { 1.0F, 1.0F, 1.0F };
  float radiance_scale { 1.0F };

  float diffuse_intensity { 1.0F };
  float specular_intensity { 1.0F };
  std::uint32_t source { 0U };
  std::uint32_t enabled { 0U };

  std::uint32_t cubemap_slot { kInvalidBindlessIndex };
  std::uint32_t brdf_lut_slot { kInvalidBindlessIndex };
  std::uint32_t irradiance_map_slot { kInvalidBindlessIndex };
  std::uint32_t prefilter_map_slot { kInvalidBindlessIndex };

  std::uint32_t cubemap_max_mip { 0U };
  std::uint32_t prefilter_max_mip { 0U };
  std::uint32_t ibl_generation { 0U };
  std::uint32_t diffuse_sh_slot { kInvalidBindlessIndex };
};

struct alignas(packing::kShaderDataFieldAlignment) GpuSkySphereParams {
  std::array<float, 3> solid_color_rgb { 0.0F, 0.0F, 0.0F };
  float intensity { 1.0F };

  std::array<float, 3> tint_rgb { 1.0F, 1.0F, 1.0F };
  float rotation_radians { 0.0F };

  std::uint32_t source { 0U };
  std::uint32_t enabled { 0U };
  std::uint32_t cubemap_slot { 0U };
  std::uint32_t cubemap_max_mip { 0U };
};

struct alignas(packing::kShaderDataFieldAlignment) GpuVolumetricCloudParams {
  std::array<float, 3> single_scattering_albedo_rgb { 0.0F, 0.0F, 0.0F };
  float base_altitude_m { 0.0F };

  std::array<float, 3> wind_dir_ws { 1.0F, 0.0F, 0.0F };
  float layer_thickness_m { 0.0F };

  float coverage { 0.0F };
  float extinction_sigma_t_per_m { 0.0F };
  float phase_g { 0.0F };
  float pad0 { 0.0F };

  float wind_speed_mps { 0.0F };
  float shadow_strength { 0.0F };
  std::uint32_t enabled { 0U };
  std::uint32_t pad1 { 0U };
};

struct alignas(packing::kShaderDataFieldAlignment) GpuPostProcessParams {
  float exposure_compensation { 0.0F };
  float auto_exposure_min_ev { 0.0F };
  float auto_exposure_max_ev { 0.0F };
  float auto_exposure_speed_up { 0.0F };

  float auto_exposure_speed_down { 0.0F };
  float bloom_intensity { 0.0F };
  float bloom_threshold { 0.0F };
  float saturation { 1.0F };

  float contrast { 1.0F };
  float vignette_intensity { 0.0F };
  std::uint32_t enabled { 0U };
  std::uint32_t pad0 { 0U };

  std::uint32_t tone_mapper { 0U };
  std::uint32_t exposure_mode { 0U };
  std::uint32_t pad1 { 0U };
  std::uint32_t pad2 { 0U };
};

struct alignas(packing::kShaderDataFieldAlignment) EnvironmentStaticData {
  GpuFogParams fog {};
  GpuVolumetricFogParams volumetric_fog {};
  GpuSkyAtmosphereParams atmosphere {};
  GpuSkyLightParams sky_light {};
  GpuSkySphereParams sky_sphere {};
  GpuVolumetricCloudParams clouds {};
  GpuPostProcessParams post_process {};
};

static_assert(sizeof(AtmosphereDensityLayerGpu) == 16);
static_assert(sizeof(AtmosphereDensityProfileGpu) == 32);
static_assert(sizeof(GpuFogParams) == 128);
static_assert(sizeof(GpuVolumetricFogParams) == 96);
static_assert(sizeof(GpuSkyAtmosphereParams) == 208);
static_assert(sizeof(GpuSkyLightParams) == 64);
static_assert(offsetof(GpuSkyLightParams, cubemap_slot) == 32);
static_assert(offsetof(GpuSkyLightParams, diffuse_sh_slot) == 60);
static_assert(sizeof(GpuSkySphereParams) == 48);
static_assert(sizeof(GpuVolumetricCloudParams) == 64);
static_assert(sizeof(GpuPostProcessParams) == 64);
static_assert(sizeof(EnvironmentStaticData) == 672);

} // namespace oxygen::vortex

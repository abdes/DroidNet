//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Core/Constants.h>

namespace oxygen::vortex {

struct alignas(packing::kShaderDataFieldAlignment) AtmosphereDensityLayerGpu {
  float width_m { 0.0F };
  float exp_term { 0.0F };
  float linear_term { 0.0F };
  float constant_term { 0.0F };
};

struct alignas(packing::kShaderDataFieldAlignment) AtmosphereDensityProfileGpu {
  std::array<AtmosphereDensityLayerGpu, 2> layers {};
};

struct alignas(packing::kShaderDataFieldAlignment) GpuFogParams {
  std::array<float, 3> single_scattering_albedo_rgb { 1.0F, 1.0F, 1.0F };
  float extinction_sigma_t_per_m { 0.0F };

  float height_falloff_per_m { 0.0F };
  float height_offset_m { 0.0F };
  float start_distance_m { 0.0F };
  float max_opacity { 1.0F };

  float anisotropy_g { 0.0F };
  float pad0 { 0.0F };
  std::uint32_t model { 0U };
  std::uint32_t enabled { 0U };
};

struct alignas(packing::kShaderDataFieldAlignment) GpuSkyAtmosphereParams {
  float planet_radius_m { 6360000.0F };
  float atmosphere_height_m { 100000.0F };
  float multi_scattering_factor { 1.0F };
  float aerial_perspective_distance_scale { 1.0F };

  std::array<float, 3> ground_albedo_rgb { 0.4F, 0.4F, 0.4F };
  float sun_disk_angular_radius_radians { 0.0F };

  std::array<float, 3> rayleigh_scattering_rgb { 0.0F, 0.0F, 0.0F };
  float rayleigh_scale_height_m { 8000.0F };

  std::array<float, 3> mie_scattering_rgb { 0.0F, 0.0F, 0.0F };
  float mie_scale_height_m { 1200.0F };

  std::array<float, 3> mie_extinction_rgb { 0.0F, 0.0F, 0.0F };
  float mie_g { 0.8F };

  std::array<float, 3> absorption_rgb { 0.0F, 0.0F, 0.0F };
  float pad_absorption { 0.0F };

  AtmosphereDensityProfileGpu absorption_density {};

  std::uint32_t sun_disk_enabled { 0U };
  std::uint32_t enabled { 0U };
  std::uint32_t transmittance_lut_slot { 0U };
  std::uint32_t sky_view_lut_slot { 0U };

  std::uint32_t sky_irradiance_lut_slot { 0U };
  std::uint32_t multi_scat_lut_slot { 0U };
  std::uint32_t camera_volume_lut_slot { 0U };
  std::uint32_t blue_noise_slot { 0U };

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

  std::uint32_t cubemap_slot { 0U };
  std::uint32_t brdf_lut_slot { 0U };
  std::uint32_t irradiance_map_slot { 0U };
  std::uint32_t prefilter_map_slot { 0U };

  std::uint32_t cubemap_max_mip { 0U };
  std::uint32_t prefilter_max_mip { 0U };
  std::uint32_t ibl_generation { 0U };
  std::uint32_t pad1 { 0U };
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
  GpuSkyAtmosphereParams atmosphere {};
  GpuSkyLightParams sky_light {};
  GpuSkySphereParams sky_sphere {};
  GpuVolumetricCloudParams clouds {};
  GpuPostProcessParams post_process {};
};

static_assert(sizeof(AtmosphereDensityLayerGpu) == 16);
static_assert(sizeof(AtmosphereDensityProfileGpu) == 32);
static_assert(sizeof(GpuFogParams) == 48);
static_assert(sizeof(GpuSkyAtmosphereParams) == 192);
static_assert(sizeof(GpuSkyLightParams) == 64);
static_assert(sizeof(GpuSkySphereParams) == 48);
static_assert(sizeof(GpuVolumetricCloudParams) == 64);
static_assert(sizeof(GpuPostProcessParams) == 64);
static_assert(sizeof(EnvironmentStaticData) == 480);

} // namespace oxygen::vortex

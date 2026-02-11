//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <type_traits>

#include <glm/vec3.hpp>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Renderer/Passes/ToneMapPass.h>

namespace oxygen::engine {

//! GPU-facing fog model selection.
/*!
 Values are a renderer-side ABI for shaders.
*/
enum class FogModel : uint32_t { // NOLINT(*-enum-size)
  kExponentialHeight = 0U,
  kVolumetric = 1U,
};

//! GPU-facing sky light source selection.
/*!
 Values are a renderer-side ABI for shaders.
*/
enum class SkyLightSource : uint32_t { // NOLINT(*-enum-size)
  kCapturedScene = 0U,
  kSpecifiedCubemap = 1U,
};

//! GPU-facing sky background source selection.
/*!
 Values are a renderer-side ABI for shaders.
*/
enum class SkySphereSource : uint32_t { // NOLINT(*-enum-size)
  kCubemap = 0U,
  kSolidColor = 1U,
};

// We defines and use strong types for bindless slots to avoid accidental
// mixups.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(TypeName)                             \
  struct TypeName {                                                            \
    ShaderVisibleIndex value;                                                  \
    TypeName()                                                                 \
      : TypeName(kInvalidShaderVisibleIndex)                                   \
    {                                                                          \
    }                                                                          \
    explicit constexpr TypeName(const ShaderVisibleIndex v)                    \
      : value(v)                                                               \
    {                                                                          \
    }                                                                          \
    [[nodiscard]] constexpr auto IsValid() const noexcept                      \
    {                                                                          \
      return value != kInvalidShaderVisibleIndex;                              \
    }                                                                          \
    constexpr auto operator<=>(const TypeName&) const = default;               \
  };                                                                           \
  static_assert(sizeof(TypeName) == 4);

OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(TransmittanceLutSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(SkyViewLutSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(SkyIrradianceLutSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(MultiScatLutSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(CameraVolumeLutSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BlueNoiseSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(CubeMapSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(BrdfLutSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(IrradianceMapSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(SpecularLutSlot)
OXYGEN_DEFINE_BINDLESS_SLOT_TYPE(PrefilterMapSlot)

#undef OXYGEN_DEFINE_BINDLESS_SLOT_TYPE

//! GPU-facing fog parameters.
/*!
 Layout mirrors the HLSL struct `GpuFogParams`.

 All values are authored in scene space and consumed by shaders in linear HDR.
*/
struct alignas(packing::kShaderDataFieldAlignment) GpuFogParams {
  //! Single-scattering albedo (linear RGB) in [0, 1].
  glm::vec3 single_scattering_albedo_rgb { 1.0F, 1.0F, 1.0F };

  //! Base extinction coefficient @f$\sigma_t@f$ (m^-1).
  float extinction_sigma_t_per_m { 0.01F };

  //! Exponential height falloff @f$k@f$ (m^-1).
  float height_falloff_per_m { 0.2F };
  float height_offset_m { 0.0F };
  float start_distance_m { 0.0F };
  float max_opacity { 1.0F };

  float anisotropy_g { 0.0F };
  float _pad0 { 0.0F };
  FogModel model { FogModel::kExponentialHeight };
  uint32_t enabled { 0U };
};
static_assert(std::is_standard_layout_v<GpuFogParams>);
static_assert(sizeof(GpuFogParams) % packing::kShaderDataFieldAlignment == 0);
static_assert(sizeof(GpuFogParams) == 48); // NOLINT(*-magic-numbers)

//! GPU-facing sky atmosphere parameters.
/*!
 Layout mirrors the HLSL struct `GpuSkyAtmosphereParams`.

 The renderer is expected to provide the sun direction via scene lighting.
*/
struct alignas(packing::kShaderDataFieldAlignment) GpuSkyAtmosphereParams {
  float planet_radius_m { atmos::kDefaultPlanetRadiusM };
  float atmosphere_height_m { atmos::kDefaultAtmosphereHeightM };
  float multi_scattering_factor { 1.0F };
  float aerial_perspective_distance_scale { 1.0F };

  glm::vec3 ground_albedo_rgb { 0.1F, 0.1F, 0.1F };
  float sun_disk_angular_radius_radians {
    atmos::kDefaultSunDiskAngularRadiusRad
  };

  glm::vec3 rayleigh_scattering_rgb { atmos::kDefaultRayleighScatteringRgb };
  float rayleigh_scale_height_m { atmos::kDefaultRayleighScaleHeightM };

  glm::vec3 mie_scattering_rgb { atmos::kDefaultMieScatteringRgb };
  float mie_scale_height_m { atmos::kDefaultMieScaleHeightM };

  //! Precomputed Mie extinction (scattering + absorption).
  glm::vec3 mie_extinction_rgb { atmos::kDefaultMieExtinctionRgb };
  float mie_g { atmos::kDefaultMieAnisotropyG };

  glm::vec3 absorption_rgb { atmos::kDefaultOzoneAbsorptionRgb };
  float _pad_absorption { 0.0F };

  atmos::DensityProfile absorption_density;

  uint32_t sun_disk_enabled { 1U };
  uint32_t enabled { 0U };
  TransmittanceLutSlot transmittance_lut_slot;
  SkyViewLutSlot sky_view_lut_slot;

  SkyIrradianceLutSlot sky_irradiance_lut_slot;
  MultiScatLutSlot multi_scat_lut_slot;
  CameraVolumeLutSlot camera_volume_lut_slot;
  BlueNoiseSlot blue_noise_slot;

  float transmittance_lut_width { 0.0F };
  float transmittance_lut_height { 0.0F };
  float sky_view_lut_width { 0.0F };
  float sky_view_lut_height { 0.0F };

  float sky_irradiance_lut_width { 0.0F };
  float sky_irradiance_lut_height { 0.0F };
  uint32_t sky_view_lut_slices { 0U };
  uint32_t sky_view_alt_mapping_mode { 0U };
};
static_assert(std::is_standard_layout_v<GpuSkyAtmosphereParams>);
static_assert(
  sizeof(GpuSkyAtmosphereParams) % packing::kShaderDataFieldAlignment == 0);
static_assert(sizeof(GpuSkyAtmosphereParams) == 192); // NOLINT(*-magic-numbers)

//! GPU-facing sky light (IBL) parameters.
/*!
 Layout mirrors the HLSL struct `GpuSkyLightParams`.

 `cubemap_slot` is a shader-visible descriptor slot (bindless SRV). When the
 sky light is disabled or missing, set `enabled = 0` and set `cubemap_slot` to
 `kInvalidDescriptorSlot`.
*/
struct alignas(packing::kShaderDataFieldAlignment) GpuSkyLightParams {
  glm::vec3 tint_rgb { 1.0F, 1.0F, 1.0F };

  //! Scales sampled sky radiance (includes authored intensity multiplier and
  //! any unit-bridging for non-physical sources).
  float radiance_scale { 1.0F };

  float diffuse_intensity { 1.0F };
  float specular_intensity { 1.0F };
  SkyLightSource source { SkyLightSource::kCapturedScene };
  uint32_t enabled { 0U };

  CubeMapSlot cubemap_slot;
  BrdfLutSlot brdf_lut_slot;
  // Added slots for IBL maps
  IrradianceMapSlot irradiance_map_slot;
  PrefilterMapSlot prefilter_map_slot;

  //! Maximum mip index for the sky cubemap slot (0 when unknown).
  std::uint32_t cubemap_max_mip { 0U };

  //! Maximum mip index for the prefilter cubemap slot (0 when unknown).
  std::uint32_t prefilter_max_mip { 0U };

  std::uint32_t ibl_generation { 0U };
  std::uint32_t _pad1 { 0U };
};
static_assert(std::is_standard_layout_v<GpuSkyLightParams>);
static_assert(
  sizeof(GpuSkyLightParams) % packing::kShaderDataFieldAlignment == 0);
static_assert(sizeof(GpuSkyLightParams) == 64); // NOLINT(*-magic-numbers)

//! GPU-facing sky sphere background parameters.
/*!
 Layout mirrors the HLSL struct `GpuSkySphereParams`.

 `cubemap_slot` is a shader-visible descriptor slot (bindless SRV). When the
 sky sphere is disabled or missing, set `enabled = 0`.
*/
struct alignas(packing::kShaderDataFieldAlignment) GpuSkySphereParams {
  glm::vec3 solid_color_rgb { 0.0F, 0.0F, 0.0F };
  float intensity { 1.0F };

  glm::vec3 tint_rgb { 1.0F, 1.0F, 1.0F };
  float rotation_radians { 0.0F };

  SkySphereSource source { SkySphereSource::kCubemap };
  uint32_t enabled { 0U };
  CubeMapSlot cubemap_slot;
  std::uint32_t cubemap_max_mip { 0U };
};
static_assert(std::is_standard_layout_v<GpuSkySphereParams>);
static_assert(
  sizeof(GpuSkySphereParams) % packing::kShaderDataFieldAlignment == 0);
static_assert(sizeof(GpuSkySphereParams) == 48); // NOLINT(*-magic-numbers)

//! GPU-facing volumetric clouds parameters.
/*!
 Layout mirrors the HLSL struct `GpuVolumetricCloudParams`.
*/
struct alignas(packing::kShaderDataFieldAlignment) GpuVolumetricCloudParams {
  //! Single-scattering albedo (linear RGB) in [0, 1].
  glm::vec3 single_scattering_albedo_rgb { 0.9F, 0.9F, 0.9F };
  float base_altitude_m { 1500.0F };

  glm::vec3 wind_dir_ws { 1.0F, 0.0F, 0.0F };
  float layer_thickness_m { 4000.0F };

  float coverage { 0.5F };

  //! Base extinction coefficient @f$\sigma_t@f$ (m^-1).
  float extinction_sigma_t_per_m { 1.0e-3F };

  float phase_g { 0.6F };
  float _pad0 { 0.0F };

  float wind_speed_mps { 10.0F };
  float shadow_strength { 0.8F };
  uint32_t enabled { 0U };
  uint32_t _pad1 { 0U };
};
static_assert(std::is_standard_layout_v<GpuVolumetricCloudParams>);
static_assert(
  sizeof(GpuVolumetricCloudParams) % packing::kShaderDataFieldAlignment == 0);
static_assert(
  sizeof(GpuVolumetricCloudParams) == 64); // NOLINT(*-magic-numbers)

//! GPU-facing post process parameters.
/*!
 Layout mirrors the HLSL struct `GpuPostProcessParams`.
*/
struct alignas(packing::kShaderDataFieldAlignment) GpuPostProcessParams {
  float exposure_compensation { 1.0F };
  float auto_exposure_min_ev { -6.0F };
  float auto_exposure_max_ev { 16.0F };
  float auto_exposure_speed_up { 3.0F };

  float auto_exposure_speed_down { 1.0F };
  float bloom_intensity { 0.0F };
  float bloom_threshold { 1.0F };
  float saturation { 1.0F };

  float contrast { 1.0F };
  float vignette_intensity { 0.0F };
  uint32_t enabled { 0U };
  uint32_t _pad0 { 0U };

  ToneMapper tone_mapper { ToneMapper::kAcesFitted };
  ExposureMode exposure_mode { ExposureMode::kManual };
  uint32_t _pad1 { 0U };
  uint32_t _pad2 { 0U };
};
static_assert(std::is_standard_layout_v<GpuPostProcessParams>);
static_assert(
  sizeof(GpuPostProcessParams) % packing::kShaderDataFieldAlignment == 0);
static_assert(sizeof(GpuPostProcessParams) == 64); // NOLINT(*-magic-numbers)

//! GPU-facing environment payload uploaded as a bindless SRV.
/*!
 This payload contains scene-authored environment parameters that are expected
 to change infrequently ("cold"), and is therefore kept as a single, larger SRV
 payload.

 Layout mirrors the HLSL struct `EnvironmentStaticData`.

 @note The renderer is responsible for mapping scene assets to bindless slots
       (e.g., cubemaps) and for selecting which sky system is active.
*/
struct alignas(packing::kShaderDataFieldAlignment) EnvironmentStaticData {
  GpuFogParams fog;
  GpuSkyAtmosphereParams atmosphere;
  GpuSkyLightParams sky_light;
  GpuSkySphereParams sky_sphere;
  GpuVolumetricCloudParams clouds;
  GpuPostProcessParams post_process;
};
static_assert(std::is_standard_layout_v<EnvironmentStaticData>);
static_assert(
  sizeof(EnvironmentStaticData) % packing::kShaderDataFieldAlignment == 0);
static_assert(sizeof(EnvironmentStaticData) == 480); // NOLINT(*-magic-numbers)

} // namespace oxygen::engine

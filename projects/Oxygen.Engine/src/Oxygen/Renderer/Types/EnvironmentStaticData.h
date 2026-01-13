//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include <Oxygen/Core/Bindless/Types.h>

namespace oxygen::engine {

//! GPU-facing fog model selection.
/*!
 Values are a renderer-side ABI for shaders.
*/
enum class FogModel : uint32_t {
  kExponentialHeight = 0u,
  kVolumetric = 1u,
};

//! GPU-facing sky light source selection.
/*!
 Values are a renderer-side ABI for shaders.
*/
enum class SkyLightSource : uint32_t {
  kCapturedScene = 0u,
  kSpecifiedCubemap = 1u,
};

//! GPU-facing sky background source selection.
/*!
 Values are a renderer-side ABI for shaders.
*/
enum class SkySphereSource : uint32_t {
  kCubemap = 0u,
  kSolidColor = 1u,
};

//! GPU-facing tonemapper selection.
/*!
 Values are a renderer-side ABI for shaders.
*/
enum class ToneMapper : uint32_t {
  kAcesFitted = 0u,
  kReinhard = 1u,
  kNone = 2u,
};

//! GPU-facing exposure behavior.
/*!
 Values are a renderer-side ABI for shaders.
*/
enum class ExposureMode : uint32_t {
  kManual = 0u,
  kAuto = 1u,
};

//! GPU-facing fog parameters.
/*!
 Layout mirrors the HLSL struct `GpuFogParams`.

 All values are authored in scene space and consumed by shaders in linear HDR.
*/
struct alignas(16) GpuFogParams {
  glm::vec3 albedo_rgb { 1.0F, 1.0F, 1.0F };
  float density { 0.01F };

  float height_falloff { 0.2F };
  float height_offset_m { 0.0F };
  float start_distance_m { 0.0F };
  float max_opacity { 1.0F };

  float anisotropy_g { 0.0F };
  float scattering_intensity { 1.0F };
  FogModel model { FogModel::kExponentialHeight };
  uint32_t enabled { 0u };
};
static_assert(
  sizeof(GpuFogParams) % 16 == 0, "GpuFogParams size must be 16-byte aligned");
static_assert(
  sizeof(GpuFogParams) == 48, "GpuFogParams size must match HLSL packing");

struct TransmittanceLutSlot {
  ShaderVisibleIndex value;
  explicit constexpr TransmittanceLutSlot(
    const ShaderVisibleIndex v = kInvalidShaderVisibleIndex)
    : value(v)
  {
  }
  constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const TransmittanceLutSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value.get(); }
};

struct SkyViewLutSlot {
  ShaderVisibleIndex value;
  explicit constexpr SkyViewLutSlot(
    const ShaderVisibleIndex v = kInvalidShaderVisibleIndex)
    : value(v)
  {
  }
  constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const SkyViewLutSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value.get(); }
};

//! GPU-facing sky atmosphere parameters.
/*!
 Layout mirrors the HLSL struct `GpuSkyAtmosphereParams`.

 The renderer is expected to provide the sun direction via scene lighting.
*/
struct alignas(16) GpuSkyAtmosphereParams {
  float planet_radius_m { 6360000.0F };
  float atmosphere_height_m { 80000.0F };
  float multi_scattering_factor { 1.0F };
  float aerial_perspective_distance_scale { 1.0F };

  glm::vec3 ground_albedo_rgb { 0.1F, 0.1F, 0.1F };
  float sun_disk_angular_radius_radians { 0.004675F };

  glm::vec3 rayleigh_scattering_rgb { 5.8e-6F, 13.5e-6F, 33.1e-6F };
  float rayleigh_scale_height_m { 8000.0F };

  glm::vec3 mie_scattering_rgb { 21.0e-6F, 21.0e-6F, 21.0e-6F };
  float mie_scale_height_m { 1200.0F };

  float mie_g { 0.8F };
  float absorption_scale_height_m { 25000.0F };
  uint32_t sun_disk_enabled { 1u };
  uint32_t enabled { 0u };

  glm::vec3 absorption_rgb { 0.65e-6F, 1.881e-6F, 0.085e-6F };
  TransmittanceLutSlot transmittance_lut_slot {};

  std::uint32_t sky_view_lut_slot { SkyViewLutSlot {} };
  float transmittance_lut_width { 0.0F };
  float transmittance_lut_height { 0.0F };
  float sky_view_lut_width { 0.0F };

  float sky_view_lut_height { 0.0F };
  float _reserved5 { 0.0F };
  float _reserved6 { 0.0F };
  float _reserved7 { 0.0F };
};
static_assert(sizeof(GpuSkyAtmosphereParams) % 16 == 0,
  "GpuSkyAtmosphereParams size must be 16-byte aligned");
static_assert(sizeof(GpuSkyAtmosphereParams) == 128,
  "GpuSkyAtmosphereParams size must match HLSL packing");

struct CubeMapSlot {
  ShaderVisibleIndex value;
  explicit constexpr CubeMapSlot(
    const ShaderVisibleIndex v = kInvalidShaderVisibleIndex)
    : value(v)
  {
  }
  constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const CubeMapSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value.get(); }
};

struct BrdfLutSlot {
  ShaderVisibleIndex value;
  explicit constexpr BrdfLutSlot(
    const ShaderVisibleIndex v = kInvalidShaderVisibleIndex)
    : value(v)
  {
  }
  constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const BrdfLutSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value.get(); }
};

struct IrradianceMapSlot {
  ShaderVisibleIndex value;
  explicit constexpr IrradianceMapSlot(
    const ShaderVisibleIndex v = kInvalidShaderVisibleIndex)
    : value(v)
  {
  }
  constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const IrradianceMapSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value.get(); }
};

struct PrefilterMapSlot {
  ShaderVisibleIndex value;
  explicit constexpr PrefilterMapSlot(
    const ShaderVisibleIndex v = kInvalidShaderVisibleIndex)
    : value(v)
  {
  }
  constexpr auto IsValid() const noexcept
  {
    return value != kInvalidShaderVisibleIndex;
  }
  constexpr auto operator<=>(const PrefilterMapSlot&) const = default;
  constexpr operator uint32_t() const noexcept { return value.get(); }
};

//! GPU-facing sky light (IBL) parameters.
/*!
 Layout mirrors the HLSL struct `GpuSkyLightParams`.

 `cubemap_slot` is a shader-visible descriptor slot (bindless SRV). When the
 sky light is disabled or missing, set `enabled = 0` and set `cubemap_slot` to
 `kInvalidDescriptorSlot`.
*/
struct alignas(16) GpuSkyLightParams {
  glm::vec3 tint_rgb { 1.0F, 1.0F, 1.0F };
  float intensity { 1.0F };

  float diffuse_intensity { 1.0F };
  float specular_intensity { 1.0F };
  SkyLightSource source { SkyLightSource::kCapturedScene };
  uint32_t enabled { 0u };

  CubeMapSlot cubemap_slot {};
  BrdfLutSlot brdf_lut_slot {};
  // Added slots for IBL maps
  IrradianceMapSlot irradiance_map_slot {};
  PrefilterMapSlot prefilter_map_slot {};
};
static_assert(sizeof(GpuSkyLightParams) % 16 == 0,
  "GpuSkyLightParams size must be 16-byte aligned");
static_assert(sizeof(GpuSkyLightParams) == 48,
  "GpuSkyLightParams size must match HLSL packing");

//! GPU-facing sky sphere background parameters.
/*!
 Layout mirrors the HLSL struct `GpuSkySphereParams`.

 `cubemap_slot` is a shader-visible descriptor slot (bindless SRV). When the
 sky sphere is disabled or missing, set `enabled = 0`.
*/
struct alignas(16) GpuSkySphereParams {
  glm::vec3 solid_color_rgb { 0.0F, 0.0F, 0.0F };
  float intensity { 1.0F };

  glm::vec3 tint_rgb { 1.0F, 1.0F, 1.0F };
  float rotation_radians { 0.0F };

  SkySphereSource source { SkySphereSource::kCubemap };
  uint32_t enabled { 0u };
  CubeMapSlot cubemap_slot {};
  uint32_t _pad0 { 0u };
};
static_assert(sizeof(GpuSkySphereParams) % 16 == 0,
  "GpuSkySphereParams size must be 16-byte aligned");
static_assert(sizeof(GpuSkySphereParams) == 48,
  "GpuSkySphereParams size must match HLSL packing");

//! GPU-facing volumetric clouds parameters.
/*!
 Layout mirrors the HLSL struct `GpuVolumetricCloudParams`.
*/
struct alignas(16) GpuVolumetricCloudParams {
  glm::vec3 albedo_rgb { 0.9F, 0.9F, 0.9F };
  float base_altitude_m { 1500.0F };

  glm::vec3 wind_dir_ws { 1.0F, 0.0F, 0.0F };
  float layer_thickness_m { 4000.0F };

  float coverage { 0.5F };
  float density { 0.5F };
  float extinction_scale { 1.0F };
  float phase_g { 0.6F };

  float wind_speed_mps { 10.0F };
  float shadow_strength { 0.8F };
  uint32_t enabled { 0u };
  uint32_t _pad0 { 0u };
};
static_assert(sizeof(GpuVolumetricCloudParams) % 16 == 0,
  "GpuVolumetricCloudParams size must be 16-byte aligned");
static_assert(sizeof(GpuVolumetricCloudParams) == 64,
  "GpuVolumetricCloudParams size must match HLSL packing");

//! GPU-facing post process parameters.
/*!
 Layout mirrors the HLSL struct `GpuPostProcessParams`.
*/
struct alignas(16) GpuPostProcessParams {
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
  uint32_t enabled { 0u };
  uint32_t _pad0 { 0u };

  ToneMapper tone_mapper { ToneMapper::kAcesFitted };
  ExposureMode exposure_mode { ExposureMode::kManual };
  uint32_t _pad1 { 0u };
  uint32_t _pad2 { 0u };
};
static_assert(sizeof(GpuPostProcessParams) % 16 == 0,
  "GpuPostProcessParams size must be 16-byte aligned");
static_assert(sizeof(GpuPostProcessParams) == 64,
  "GpuPostProcessParams size must match HLSL packing");

//! GPU-facing environment payload uploaded as a bindless SRV.
/*!
 This payload contains scene-authored environment parameters that are expected
 to change infrequently ("cold"), and is therefore kept as a single, larger SRV
 payload.

 Layout mirrors the HLSL struct `EnvironmentStaticData`.

 @note The renderer is responsible for mapping scene assets to bindless slots
       (e.g., cubemaps) and for selecting which sky system is active.
*/
struct alignas(16) EnvironmentStaticData {
  GpuFogParams fog {};
  GpuSkyAtmosphereParams atmosphere {};
  GpuSkyLightParams sky_light {};
  GpuSkySphereParams sky_sphere {};
  GpuVolumetricCloudParams clouds {};
  GpuPostProcessParams post_process {};
};
static_assert(sizeof(EnvironmentStaticData) % 16 == 0,
  "EnvironmentStaticData size must be 16-byte aligned");
static_assert(sizeof(EnvironmentStaticData) == 400,
  "EnvironmentStaticData size must match HLSL packing");

} // namespace oxygen::engine

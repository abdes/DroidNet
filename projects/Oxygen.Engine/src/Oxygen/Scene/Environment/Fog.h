//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>

namespace oxygen::scene::environment {

//! Fog model selection.
enum class FogModel {
  kExponentialHeight,
  kVolumetric,
};

//! Scene-global fog parameters.
/*!
 Inspired by UE ExponentialHeightFog and common volumetric fog controls.

 When `model == kVolumetric`, the renderer may evaluate a participating media
 volume (typically aligned to the camera frustum or a world grid). For
 `kExponentialHeight`, the renderer can use a cheaper analytic fog evaluation.
*/
class Fog final : public EnvironmentSystem {
  OXYGEN_COMPONENT(Fog)

public:
  //! Constructs fog with conservative defaults.
  Fog() = default;

  //! Virtual destructor.
  ~Fog() override = default;

  OXYGEN_DEFAULT_COPYABLE(Fog)
  OXYGEN_DEFAULT_MOVABLE(Fog)

  //! Sets the fog model.
  auto SetModel(const FogModel model) noexcept -> void
  {
    model_ = model;
    enable_height_fog_ = true;
    enable_volumetric_fog_ = model == FogModel::kVolumetric;
  }

  //! Gets the fog model.
  [[nodiscard]] auto GetModel() const noexcept -> FogModel { return model_; }

  auto SetEnableHeightFog(const bool enabled) noexcept -> void
  {
    enable_height_fog_ = enabled;
    SyncLegacyModelFromFlags();
  }
  [[nodiscard]] auto GetEnableHeightFog() const noexcept -> bool
  {
    return enable_height_fog_;
  }

  auto SetEnableVolumetricFog(const bool enabled) noexcept -> void
  {
    enable_volumetric_fog_ = enabled;
    SyncLegacyModelFromFlags();
  }
  [[nodiscard]] auto GetEnableVolumetricFog() const noexcept -> bool
  {
    return enable_volumetric_fog_;
  }

  auto SetFogDensity(const float density) noexcept -> void
  {
    extinction_sigma_t_per_m_ = density;
  }
  [[nodiscard]] auto GetFogDensity() const noexcept -> float
  {
    return extinction_sigma_t_per_m_;
  }

  //! Sets the base extinction coefficient @f$\sigma_t@f$ (m^-1).
  /*!
   This is the participating media extinction used by the analytic height fog
   evaluation.

   Conceptually, when height fog is enabled the shader evaluates:

   - transmittance @f$T = e^{-\sigma_t d}@f$

   where @f$d@f$ is the view distance in meters.
  */
  auto SetExtinctionSigmaTPerMeter(const float sigma_t_per_m) noexcept -> void
  {
    extinction_sigma_t_per_m_ = sigma_t_per_m;
  }

  //! Gets the base extinction coefficient @f$\sigma_t@f$ (m^-1).
  [[nodiscard]] auto GetExtinctionSigmaTPerMeter() const noexcept -> float
  {
    return extinction_sigma_t_per_m_;
  }

  //! Sets exponential height falloff (m^-1).
  /*!
   The fog extinction varies with height as:
   @f$\sigma_t(h) = \sigma_{t,0} \cdot e^{-k(h-h_0)}@f$
   where @f$k@f$ is this falloff coefficient.
  */
  auto SetHeightFalloffPerMeter(const float falloff_per_m) noexcept -> void
  {
    height_falloff_per_m_ = falloff_per_m;
  }

  //! Gets exponential height falloff (m^-1).
  [[nodiscard]] auto GetHeightFalloffPerMeter() const noexcept -> float
  {
    return height_falloff_per_m_;
  }

  auto SetSecondFogDensity(const float density) noexcept -> void
  {
    second_fog_density_ = density;
  }
  [[nodiscard]] auto GetSecondFogDensity() const noexcept -> float
  {
    return second_fog_density_;
  }

  auto SetSecondFogHeightFalloff(const float value) noexcept -> void
  {
    second_fog_height_falloff_ = value;
  }
  [[nodiscard]] auto GetSecondFogHeightFalloff() const noexcept -> float
  {
    return second_fog_height_falloff_;
  }

  auto SetSecondFogHeightOffset(const float value) noexcept -> void
  {
    second_fog_height_offset_ = value;
  }
  [[nodiscard]] auto GetSecondFogHeightOffset() const noexcept -> float
  {
    return second_fog_height_offset_;
  }

  //! Sets height offset (meters).
  auto SetHeightOffsetMeters(const float meters) noexcept -> void
  {
    height_offset_m_ = meters;
  }

  //! Gets height offset (meters).
  [[nodiscard]] auto GetHeightOffsetMeters() const noexcept -> float
  {
    return height_offset_m_;
  }

  //! Sets start distance (meters).
  auto SetStartDistanceMeters(const float meters) noexcept -> void
  {
    start_distance_m_ = meters;
  }

  //! Gets start distance (meters).
  [[nodiscard]] auto GetStartDistanceMeters() const noexcept -> float
  {
    return start_distance_m_;
  }

  //! Sets maximum opacity in [0, 1].
  auto SetMaxOpacity(const float opacity) noexcept -> void
  {
    max_opacity_ = opacity;
  }

  //! Gets maximum opacity.
  [[nodiscard]] auto GetMaxOpacity() const noexcept -> float
  {
    return max_opacity_;
  }

  auto SetFogInscatteringLuminance(const Vec3& rgb) noexcept -> void
  {
    fog_inscattering_luminance_ = rgb;
  }
  [[nodiscard]] auto GetFogInscatteringLuminance() const noexcept
    -> const Vec3&
  {
    return fog_inscattering_luminance_;
  }

  auto SetSkyAtmosphereAmbientContributionColorScale(
    const Vec3& rgb) noexcept -> void
  {
    sky_atmosphere_ambient_contribution_color_scale_ = rgb;
  }
  [[nodiscard]] auto GetSkyAtmosphereAmbientContributionColorScale() const
    noexcept -> const Vec3&
  {
    return sky_atmosphere_ambient_contribution_color_scale_;
  }

  auto SetInscatteringColorCubemapResource(
    const content::ResourceKey& key) noexcept -> void
  {
    inscattering_color_cubemap_resource_ = key;
  }
  [[nodiscard]] auto GetInscatteringColorCubemapResource() const noexcept
    -> const content::ResourceKey&
  {
    return inscattering_color_cubemap_resource_;
  }

  auto SetInscatteringColorCubemapAngle(const float value) noexcept -> void
  {
    inscattering_color_cubemap_angle_ = value;
  }
  [[nodiscard]] auto GetInscatteringColorCubemapAngle() const noexcept -> float
  {
    return inscattering_color_cubemap_angle_;
  }

  auto SetInscatteringTextureTint(const Vec3& rgb) noexcept -> void
  {
    inscattering_texture_tint_ = rgb;
  }
  [[nodiscard]] auto GetInscatteringTextureTint() const noexcept -> const Vec3&
  {
    return inscattering_texture_tint_;
  }

  auto SetFullyDirectionalInscatteringColorDistance(
    const float value) noexcept -> void
  {
    fully_directional_inscattering_color_distance_ = value;
  }
  [[nodiscard]] auto GetFullyDirectionalInscatteringColorDistance() const
    noexcept -> float
  {
    return fully_directional_inscattering_color_distance_;
  }

  auto SetNonDirectionalInscatteringColorDistance(
    const float value) noexcept -> void
  {
    non_directional_inscattering_color_distance_ = value;
  }
  [[nodiscard]] auto GetNonDirectionalInscatteringColorDistance() const
    noexcept -> float
  {
    return non_directional_inscattering_color_distance_;
  }

  auto SetDirectionalInscatteringLuminance(const Vec3& rgb) noexcept -> void
  {
    directional_inscattering_luminance_ = rgb;
  }
  [[nodiscard]] auto GetDirectionalInscatteringLuminance() const noexcept
    -> const Vec3&
  {
    return directional_inscattering_luminance_;
  }

  auto SetDirectionalInscatteringExponent(const float value) noexcept -> void
  {
    directional_inscattering_exponent_ = value;
  }
  [[nodiscard]] auto GetDirectionalInscatteringExponent() const noexcept
    -> float
  {
    return directional_inscattering_exponent_;
  }

  auto SetDirectionalInscatteringStartDistance(const float value) noexcept
    -> void
  {
    directional_inscattering_start_distance_ = value;
  }
  [[nodiscard]] auto GetDirectionalInscatteringStartDistance() const noexcept
    -> float
  {
    return directional_inscattering_start_distance_;
  }

  auto SetEndDistanceMeters(const float value) noexcept -> void
  {
    end_distance_m_ = value;
  }
  [[nodiscard]] auto GetEndDistanceMeters() const noexcept -> float
  {
    return end_distance_m_;
  }

  auto SetFogCutoffDistanceMeters(const float value) noexcept -> void
  {
    fog_cutoff_distance_m_ = value;
  }
  [[nodiscard]] auto GetFogCutoffDistanceMeters() const noexcept -> float
  {
    return fog_cutoff_distance_m_;
  }

  //! Sets single-scattering albedo (linear RGB) in [0, 1].
  /*!
   This is the ratio @f$\sigma_s / \sigma_t@f$ and controls how much of the
   extinction is due to scattering vs. absorption.

   @note This parameter is used only for the fog inscatter approximation.
  */
  auto SetSingleScatteringAlbedoRgb(const Vec3& rgb) noexcept -> void
  {
    single_scattering_albedo_rgb_ = rgb;
  }

  //! Gets single-scattering albedo (linear RGB).
  [[nodiscard]] auto GetSingleScatteringAlbedoRgb() const noexcept
    -> const Vec3&
  {
    return single_scattering_albedo_rgb_;
  }

  //! Sets anisotropy g in [-1, 1].
  auto SetAnisotropy(const float g) noexcept -> void { anisotropy_g_ = g; }

  //! Gets anisotropy.
  [[nodiscard]] auto GetAnisotropy() const noexcept -> float
  {
    return anisotropy_g_;
  }

  auto SetVolumetricFogScatteringDistribution(const float value) noexcept
    -> void
  {
    volumetric_fog_scattering_distribution_ = value;
  }
  [[nodiscard]] auto GetVolumetricFogScatteringDistribution() const noexcept
    -> float
  {
    return volumetric_fog_scattering_distribution_;
  }

  auto SetVolumetricFogAlbedo(const Vec3& rgb) noexcept -> void
  {
    volumetric_fog_albedo_ = rgb;
  }
  [[nodiscard]] auto GetVolumetricFogAlbedo() const noexcept -> const Vec3&
  {
    return volumetric_fog_albedo_;
  }

  auto SetVolumetricFogEmissive(const Vec3& rgb) noexcept -> void
  {
    volumetric_fog_emissive_ = rgb;
  }
  [[nodiscard]] auto GetVolumetricFogEmissive() const noexcept -> const Vec3&
  {
    return volumetric_fog_emissive_;
  }

  auto SetVolumetricFogExtinctionScale(const float value) noexcept -> void
  {
    volumetric_fog_extinction_scale_ = value;
  }
  [[nodiscard]] auto GetVolumetricFogExtinctionScale() const noexcept -> float
  {
    return volumetric_fog_extinction_scale_;
  }

  auto SetVolumetricFogDistance(const float value) noexcept -> void
  {
    volumetric_fog_distance_ = value;
  }
  [[nodiscard]] auto GetVolumetricFogDistance() const noexcept -> float
  {
    return volumetric_fog_distance_;
  }

  auto SetVolumetricFogStartDistance(const float value) noexcept -> void
  {
    volumetric_fog_start_distance_ = value;
  }
  [[nodiscard]] auto GetVolumetricFogStartDistance() const noexcept -> float
  {
    return volumetric_fog_start_distance_;
  }

  auto SetVolumetricFogNearFadeInDistance(const float value) noexcept -> void
  {
    volumetric_fog_near_fade_in_distance_ = value;
  }
  [[nodiscard]] auto GetVolumetricFogNearFadeInDistance() const noexcept
    -> float
  {
    return volumetric_fog_near_fade_in_distance_;
  }

  auto SetVolumetricFogStaticLightingScatteringIntensity(
    const float value) noexcept -> void
  {
    volumetric_fog_static_lighting_scattering_intensity_ = value;
  }
  [[nodiscard]] auto GetVolumetricFogStaticLightingScatteringIntensity() const
    noexcept -> float
  {
    return volumetric_fog_static_lighting_scattering_intensity_;
  }

  auto SetOverrideLightColorsWithFogInscatteringColors(const bool value)
    noexcept -> void
  {
    override_light_colors_with_fog_inscattering_colors_ = value;
  }
  [[nodiscard]] auto GetOverrideLightColorsWithFogInscatteringColors() const
    noexcept -> bool
  {
    return override_light_colors_with_fog_inscattering_colors_;
  }

  auto SetHoldout(const bool value) noexcept -> void { holdout_ = value; }
  [[nodiscard]] auto GetHoldout() const noexcept -> bool { return holdout_; }

  auto SetRenderInMainPass(const bool value) noexcept -> void
  {
    render_in_main_pass_ = value;
  }
  [[nodiscard]] auto GetRenderInMainPass() const noexcept -> bool
  {
    return render_in_main_pass_;
  }

  auto SetVisibleInReflectionCaptures(const bool value) noexcept -> void
  {
    visible_in_reflection_captures_ = value;
  }
  [[nodiscard]] auto GetVisibleInReflectionCaptures() const noexcept -> bool
  {
    return visible_in_reflection_captures_;
  }

  auto SetVisibleInRealTimeSkyCaptures(const bool value) noexcept -> void
  {
    visible_in_real_time_sky_captures_ = value;
  }
  [[nodiscard]] auto GetVisibleInRealTimeSkyCaptures() const noexcept -> bool
  {
    return visible_in_real_time_sky_captures_;
  }

private:
  auto SyncLegacyModelFromFlags() noexcept -> void
  {
    model_ = enable_volumetric_fog_ ? FogModel::kVolumetric
                                    : FogModel::kExponentialHeight;
  }

  FogModel model_ = FogModel::kExponentialHeight;
  bool enable_height_fog_ = true;
  bool enable_volumetric_fog_ = false;

  float extinction_sigma_t_per_m_ = 0.01F;
  float height_falloff_per_m_ = 0.2F;
  float height_offset_m_ = 0.0F;
  float start_distance_m_ = 0.0F;
  float second_fog_density_ = 0.0F;
  float second_fog_height_falloff_ = 0.0F;
  float second_fog_height_offset_ = 0.0F;

  float max_opacity_ = 1.0F;
  Vec3 single_scattering_albedo_rgb_ { 1.0F, 1.0F, 1.0F };
  Vec3 fog_inscattering_luminance_ { 1.0F, 1.0F, 1.0F };
  Vec3 sky_atmosphere_ambient_contribution_color_scale_ {
    1.0F,
    1.0F,
    1.0F,
  };
  content::ResourceKey inscattering_color_cubemap_resource_ {};
  float inscattering_color_cubemap_angle_ = 0.0F;
  Vec3 inscattering_texture_tint_ { 1.0F, 1.0F, 1.0F };
  float fully_directional_inscattering_color_distance_ = 0.0F;
  float non_directional_inscattering_color_distance_ = 0.0F;
  Vec3 directional_inscattering_luminance_ { 1.0F, 1.0F, 1.0F };
  float directional_inscattering_exponent_ = 0.0F;
  float directional_inscattering_start_distance_ = 0.0F;
  float end_distance_m_ = 0.0F;
  float fog_cutoff_distance_m_ = 0.0F;

  float anisotropy_g_ = 0.0F;
  float volumetric_fog_scattering_distribution_ = 0.0F;
  Vec3 volumetric_fog_albedo_ { 1.0F, 1.0F, 1.0F };
  Vec3 volumetric_fog_emissive_ { 0.0F, 0.0F, 0.0F };
  float volumetric_fog_extinction_scale_ = 1.0F;
  float volumetric_fog_distance_ = 0.0F;
  float volumetric_fog_start_distance_ = 0.0F;
  float volumetric_fog_near_fade_in_distance_ = 0.0F;
  float volumetric_fog_static_lighting_scattering_intensity_ = 1.0F;
  bool override_light_colors_with_fog_inscattering_colors_ = false;
  bool holdout_ = false;
  bool render_in_main_pass_ = true;
  bool visible_in_reflection_captures_ = true;
  bool visible_in_real_time_sky_captures_ = true;
};

} // namespace oxygen::scene::environment

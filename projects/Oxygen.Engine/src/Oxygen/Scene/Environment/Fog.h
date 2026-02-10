//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

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
  auto SetModel(const FogModel model) noexcept -> void { model_ = model; }

  //! Gets the fog model.
  [[nodiscard]] auto GetModel() const noexcept -> FogModel { return model_; }

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

private:
  FogModel model_ = FogModel::kExponentialHeight;

  float extinction_sigma_t_per_m_ = 0.01F;
  float height_falloff_per_m_ = 0.2F;
  float height_offset_m_ = 0.0F;
  float start_distance_m_ = 0.0F;

  float max_opacity_ = 1.0F;
  Vec3 single_scattering_albedo_rgb_ { 1.0F, 1.0F, 1.0F };

  float anisotropy_g_ = 0.0F;
};

} // namespace oxygen::scene::environment

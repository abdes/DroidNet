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

  //! Sets base density (unitless).
  auto SetDensity(const float density) noexcept -> void { density_ = density; }

  //! Gets base density.
  [[nodiscard]] auto GetDensity() const noexcept -> float { return density_; }

  //! Sets height falloff (unitless).
  auto SetHeightFalloff(const float falloff) noexcept -> void
  {
    height_falloff_ = falloff;
  }

  //! Gets height falloff.
  [[nodiscard]] auto GetHeightFalloff() const noexcept -> float
  {
    return height_falloff_;
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

  //! Sets fog albedo (linear RGB).
  auto SetAlbedoRgb(const Vec3& rgb) noexcept -> void { albedo_rgb_ = rgb; }

  //! Gets fog albedo.
  [[nodiscard]] auto GetAlbedoRgb() const noexcept -> const Vec3&
  {
    return albedo_rgb_;
  }

  //! Sets anisotropy g in [-1, 1] (used for volumetric model).
  auto SetAnisotropy(const float g) noexcept -> void { anisotropy_g_ = g; }

  //! Gets anisotropy.
  [[nodiscard]] auto GetAnisotropy() const noexcept -> float
  {
    return anisotropy_g_;
  }

  //! Sets scattering intensity multiplier (used for volumetric model).
  auto SetScatteringIntensity(const float intensity) noexcept -> void
  {
    scattering_intensity_ = intensity;
  }

  //! Gets scattering intensity.
  [[nodiscard]] auto GetScatteringIntensity() const noexcept -> float
  {
    return scattering_intensity_;
  }

private:
  FogModel model_ = FogModel::kExponentialHeight;

  float density_ = 0.01F;
  float height_falloff_ = 0.2F;
  float height_offset_m_ = 0.0F;
  float start_distance_m_ = 0.0F;

  float max_opacity_ = 1.0F;
  Vec3 albedo_rgb_ { 1.0F, 1.0F, 1.0F };

  float anisotropy_g_ = 0.0F;
  float scattering_intensity_ = 1.0F;
};

} // namespace oxygen::scene::environment

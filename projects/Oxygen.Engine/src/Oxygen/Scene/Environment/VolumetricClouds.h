//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene::environment {

//! Volumetric cloud layer parameters.
/*!
 This is an authored parameter set suitable for common real-time volumetric
 cloud implementations (layered noise with wind and lighting).

 The renderer is expected to interpret these fields and map them to its chosen
 cloud technique.
*/
class VolumetricClouds final : public EnvironmentSystem {
  OXYGEN_COMPONENT(VolumetricClouds)

public:
  //! Constructs a default cloud layer.
  VolumetricClouds() = default;

  //! Virtual destructor.
  ~VolumetricClouds() override = default;

  OXYGEN_DEFAULT_COPYABLE(VolumetricClouds)
  OXYGEN_DEFAULT_MOVABLE(VolumetricClouds)

  //! Sets the base altitude of the cloud layer (meters).
  auto SetBaseAltitudeMeters(const float meters) noexcept -> void
  {
    base_altitude_m_ = meters;
  }

  //! Gets the base altitude of the cloud layer (meters).
  OXGN_SCN_NDAPI auto GetBaseAltitudeMeters() const noexcept -> float
  {
    return base_altitude_m_;
  }

  //! Sets the thickness of the cloud layer (meters).
  auto SetLayerThicknessMeters(const float meters) noexcept -> void
  {
    layer_thickness_m_ = meters;
  }

  //! Gets the thickness of the cloud layer (meters).
  OXGN_SCN_NDAPI auto GetLayerThicknessMeters() const noexcept -> float
  {
    return layer_thickness_m_;
  }

  //! Sets coverage in [0, 1].
  auto SetCoverage(const float coverage) noexcept -> void
  {
    coverage_ = coverage;
  }

  //! Gets coverage.
  OXGN_SCN_NDAPI auto GetCoverage() const noexcept -> float
  {
    return coverage_;
  }

  //! Sets density in [0, 1].
  auto SetDensity(const float density) noexcept -> void { density_ = density; }

  //! Gets density.
  OXGN_SCN_NDAPI auto GetDensity() const noexcept -> float { return density_; }

  //! Sets single-scattering albedo (linear RGB).
  auto SetAlbedoRgb(const Vec3& rgb) noexcept -> void { albedo_rgb_ = rgb; }

  //! Gets single-scattering albedo (linear RGB).
  OXGN_SCN_NDAPI auto GetAlbedoRgb() const noexcept -> const Vec3&
  {
    return albedo_rgb_;
  }

  //! Sets extinction scale (unitless multiplier).
  auto SetExtinctionScale(const float scale) noexcept -> void
  {
    extinction_scale_ = scale;
  }

  //! Gets extinction scale.
  OXGN_SCN_NDAPI auto GetExtinctionScale() const noexcept -> float
  {
    return extinction_scale_;
  }

  //! Sets phase anisotropy g in [-1, 1].
  auto SetPhaseAnisotropy(const float g) noexcept -> void { phase_g_ = g; }

  //! Gets phase anisotropy.
  OXGN_SCN_NDAPI auto GetPhaseAnisotropy() const noexcept -> float
  {
    return phase_g_;
  }

  //! Sets wind direction in world space (does not normalize).
  auto SetWindDirectionWs(const Vec3& dir_ws) noexcept -> void
  {
    wind_dir_ws_ = dir_ws;
  }

  //! Gets wind direction in world space.
  OXGN_SCN_NDAPI auto GetWindDirectionWs() const noexcept -> const Vec3&
  {
    return wind_dir_ws_;
  }

  //! Sets wind speed (meters per second).
  auto SetWindSpeedMps(const float mps) noexcept -> void
  {
    wind_speed_mps_ = mps;
  }

  //! Gets wind speed (meters per second).
  OXGN_SCN_NDAPI auto GetWindSpeedMps() const noexcept -> float
  {
    return wind_speed_mps_;
  }

  //! Sets cloud shadow strength in [0, 1].
  auto SetShadowStrength(const float strength) noexcept -> void
  {
    shadow_strength_ = strength;
  }

  //! Gets cloud shadow strength.
  OXGN_SCN_NDAPI auto GetShadowStrength() const noexcept -> float
  {
    return shadow_strength_;
  }

private:
  float base_altitude_m_ = 1500.0F;
  float layer_thickness_m_ = 4000.0F;

  float coverage_ = 0.5F;
  float density_ = 0.5F;

  Vec3 albedo_rgb_ { 0.9F, 0.9F, 0.9F };
  float extinction_scale_ = 1.0F;
  float phase_g_ = 0.6F;

  Vec3 wind_dir_ws_ { 1.0F, 0.0F, 0.0F };
  float wind_speed_mps_ = 10.0F;

  float shadow_strength_ = 0.8F;
};

} // namespace oxygen::scene::environment

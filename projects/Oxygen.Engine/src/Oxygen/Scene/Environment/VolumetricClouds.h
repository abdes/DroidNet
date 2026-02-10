//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>

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
  [[nodiscard]] auto GetBaseAltitudeMeters() const noexcept -> float
  {
    return base_altitude_m_;
  }

  //! Sets the thickness of the cloud layer (meters).
  auto SetLayerThicknessMeters(const float meters) noexcept -> void
  {
    layer_thickness_m_ = meters;
  }

  //! Gets the thickness of the cloud layer (meters).
  [[nodiscard]] auto GetLayerThicknessMeters() const noexcept -> float
  {
    return layer_thickness_m_;
  }

  //! Sets coverage in [0, 1].
  auto SetCoverage(const float coverage) noexcept -> void
  {
    coverage_ = coverage;
  }

  //! Gets coverage.
  [[nodiscard]] auto GetCoverage() const noexcept -> float { return coverage_; }

  //! Sets the base extinction coefficient @f$\sigma_t@f$ (m^-1).
  /*!
   Volumetric cloud rendering is expected to interpret this as the participating
   media extinction used during ray marching.
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

  //! Sets single-scattering albedo (linear RGB) in [0, 1].
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

  //! Sets phase anisotropy g in [-1, 1].
  auto SetPhaseAnisotropy(const float g) noexcept -> void { phase_g_ = g; }

  //! Gets phase anisotropy.
  [[nodiscard]] auto GetPhaseAnisotropy() const noexcept -> float
  {
    return phase_g_;
  }

  //! Sets wind direction in world space (does not normalize).
  auto SetWindDirectionWs(const Vec3& dir_ws) noexcept -> void
  {
    wind_dir_ws_ = dir_ws;
  }

  //! Gets wind direction in world space.
  [[nodiscard]] auto GetWindDirectionWs() const noexcept -> const Vec3&
  {
    return wind_dir_ws_;
  }

  //! Sets wind speed (meters per second).
  auto SetWindSpeedMps(const float mps) noexcept -> void
  {
    wind_speed_mps_ = mps;
  }

  //! Gets wind speed (meters per second).
  [[nodiscard]] auto GetWindSpeedMps() const noexcept -> float
  {
    return wind_speed_mps_;
  }

  //! Sets cloud shadow strength in [0, 1].
  auto SetShadowStrength(const float strength) noexcept -> void
  {
    shadow_strength_ = strength;
  }

  //! Gets cloud shadow strength.
  [[nodiscard]] auto GetShadowStrength() const noexcept -> float
  {
    return shadow_strength_;
  }

private:
  float base_altitude_m_ = 1500.0F;
  float layer_thickness_m_ = 4000.0F;

  float coverage_ = 0.5F;
  float extinction_sigma_t_per_m_ = 1.0e-3F;

  Vec3 single_scattering_albedo_rgb_ { 0.9F, 0.9F, 0.9F };
  float phase_g_ = 0.6F;

  Vec3 wind_dir_ws_ { 1.0F, 0.0F, 0.0F };
  float wind_speed_mps_ = 10.0F;

  float shadow_strength_ = 0.8F;
};

} // namespace oxygen::scene::environment

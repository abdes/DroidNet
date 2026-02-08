//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>

namespace oxygen::scene::environment {

//! Physically-inspired atmospheric scattering sky model.
/*!
 Minimal parameter set inspired by UE5 SkyAtmosphere and common real-time sky
 models. Values are in meters and linear RGB.

 The system is designed to be driven by a scene sun direction (a directional
 light) at render-time; this component only stores authored parameters.
*/
class SkyAtmosphere final : public EnvironmentSystem {
  OXYGEN_COMPONENT(SkyAtmosphere)

public:
  //! Constructs the atmosphere with Earth-like defaults.
  SkyAtmosphere() = default;

  //! Virtual destructor.
  ~SkyAtmosphere() override = default;

  OXYGEN_DEFAULT_COPYABLE(SkyAtmosphere)
  OXYGEN_DEFAULT_MOVABLE(SkyAtmosphere)

  //! Sets the planet radius (meters).
  auto SetPlanetRadiusMeters(const float meters) noexcept -> void
  {
    planet_radius_m_ = meters;
  }

  //! Gets the planet radius (meters).
  [[nodiscard]] auto GetPlanetRadiusMeters() const noexcept -> float
  {
    return planet_radius_m_;
  }

  //! Sets the atmosphere height (meters).
  auto SetAtmosphereHeightMeters(const float meters) noexcept -> void
  {
    atmosphere_height_m_ = meters;
  }

  //! Gets the atmosphere height (meters).
  [[nodiscard]] auto GetAtmosphereHeightMeters() const noexcept -> float
  {
    return atmosphere_height_m_;
  }

  //! Sets ground albedo (linear RGB).
  auto SetGroundAlbedoRgb(const Vec3& rgb) noexcept -> void
  {
    ground_albedo_rgb_ = rgb;
  }

  //! Gets ground albedo (linear RGB).
  [[nodiscard]] auto GetGroundAlbedoRgb() const noexcept -> const Vec3&
  {
    return ground_albedo_rgb_;
  }

  //! Sets Rayleigh scattering coefficient (1 / meter, RGB).
  auto SetRayleighScatteringRgb(const Vec3& rgb) noexcept -> void
  {
    rayleigh_scattering_rgb_ = rgb;
  }

  //! Gets Rayleigh scattering coefficient (1 / meter, RGB).
  [[nodiscard]] auto GetRayleighScatteringRgb() const noexcept -> const Vec3&
  {
    return rayleigh_scattering_rgb_;
  }

  //! Sets Rayleigh scale height (meters).
  auto SetRayleighScaleHeightMeters(const float meters) noexcept -> void
  {
    rayleigh_scale_height_m_ = meters;
  }

  //! Gets Rayleigh scale height (meters).
  [[nodiscard]] auto GetRayleighScaleHeightMeters() const noexcept -> float
  {
    return rayleigh_scale_height_m_;
  }

  //! Sets Mie scattering coefficient (1 / meter, RGB).
  auto SetMieScatteringRgb(const Vec3& rgb) noexcept -> void
  {
    mie_scattering_rgb_ = rgb;
  }

  //! Gets Mie scattering coefficient (1 / meter, RGB).
  [[nodiscard]] auto GetMieScatteringRgb() const noexcept -> const Vec3&
  {
    return mie_scattering_rgb_;
  }

  //! Sets Mie scale height (meters).
  auto SetMieScaleHeightMeters(const float meters) noexcept -> void
  {
    mie_scale_height_m_ = meters;
  }

  //! Gets Mie scale height (meters).
  [[nodiscard]] auto GetMieScaleHeightMeters() const noexcept -> float
  {
    return mie_scale_height_m_;
  }

  //! Sets Mie absorption coefficient (1/meter, RGB).
  /*!
   UE5-style explicit absorption. Mie extinction = scattering + absorption.
   Default corresponds to SSA ≈ 0.9 for Earth-like atmospheres.
  */
  auto SetMieAbsorptionRgb(const Vec3& rgb) noexcept -> void
  {
    mie_absorption_rgb_ = rgb;
  }

  //! Gets Mie absorption coefficient (1/meter, RGB).
  [[nodiscard]] auto GetMieAbsorptionRgb() const noexcept -> const Vec3&
  {
    return mie_absorption_rgb_;
  }

  //! Sets Mie anisotropy g in [-1, 1].
  auto SetMieAnisotropy(const float g) noexcept -> void { mie_g_ = g; }

  //! Gets Mie anisotropy g.
  [[nodiscard]] auto GetMieAnisotropy() const noexcept -> float
  {
    return mie_g_;
  }

  //! Sets absorption coefficient (1 / meter, RGB).
  auto SetAbsorptionRgb(const Vec3& rgb) noexcept -> void
  {
    absorption_rgb_ = rgb;
  }

  //! Gets absorption coefficient (1 / meter, RGB).
  [[nodiscard]] auto GetAbsorptionRgb() const noexcept -> const Vec3&
  {
    return absorption_rgb_;
  }

  //! Sets absorption layer width (meters).
  auto SetAbsorptionLayerWidthMeters(const float meters) noexcept -> void
  {
    absorption_layer_width_m_ = meters;
  }

  //! Gets absorption layer width (meters).
  [[nodiscard]] auto GetAbsorptionLayerWidthMeters() const noexcept -> float
  {
    return absorption_layer_width_m_;
  }

  //! Sets absorption linear term below the layer width.
  auto SetAbsorptionTermBelow(const float term) noexcept -> void
  {
    absorption_term_below_ = term;
  }

  //! Gets absorption linear term below the layer width.
  [[nodiscard]] auto GetAbsorptionTermBelow() const noexcept -> float
  {
    return absorption_term_below_;
  }

  //! Sets absorption linear term above the layer width.
  auto SetAbsorptionTermAbove(const float term) noexcept -> void
  {
    absorption_term_above_ = term;
  }

  //! Gets absorption linear term above the layer width.
  [[nodiscard]] auto GetAbsorptionTermAbove() const noexcept -> float
  {
    return absorption_term_above_;
  }

  //! Sets multi-scattering factor (unitless, typically 0..1).
  auto SetMultiScatteringFactor(const float factor) noexcept -> void
  {
    multi_scattering_factor_ = factor;
  }

  //! Gets multi-scattering factor.
  [[nodiscard]] auto GetMultiScatteringFactor() const noexcept -> float
  {
    return multi_scattering_factor_;
  }

  //! Enables or disables rendering a sun disk in the sky model.
  auto SetSunDiskEnabled(const bool enabled) noexcept -> void
  {
    sun_disk_enabled_ = enabled;
  }

  //! Returns whether the sun disk is enabled.
  [[nodiscard]] auto GetSunDiskEnabled() const noexcept -> bool
  {
    return sun_disk_enabled_;
  }

  //! Sets aerial perspective distance scale (unitless).
  auto SetAerialPerspectiveDistanceScale(const float scale) noexcept -> void
  {
    aerial_perspective_distance_scale_ = scale;
  }

  //! Gets aerial perspective distance scale.
  [[nodiscard]] auto GetAerialPerspectiveDistanceScale() const noexcept -> float
  {
    return aerial_perspective_distance_scale_;
  }

  //! Sets aerial perspective scattering strength (unitless).
  /*!
   Controls the strength of LUT-based aerial perspective applied to scene
   geometry. This is intentionally separate from `multi_scattering_factor`,
   which affects sky scattering in the sky-view LUT.
  */
  auto SetAerialScatteringStrength(const float strength) noexcept -> void
  {
    aerial_scattering_strength_ = strength;
  }

  //! Gets aerial perspective scattering strength.
  [[nodiscard]] auto GetAerialScatteringStrength() const noexcept -> float
  {
    return aerial_scattering_strength_;
  }

private:
  float planet_radius_m_ = 6360000.0F;
  float atmosphere_height_m_ = 80000.0F;

  Vec3 ground_albedo_rgb_ { 0.1F, 0.1F, 0.1F };

  // Earth-like baseline coefficients; treated as authorable parameters.
  Vec3 rayleigh_scattering_rgb_ { 5.8e-6F, 13.5e-6F, 33.1e-6F };
  float rayleigh_scale_height_m_ = 8000.0F;

  Vec3 mie_scattering_rgb_ { 21.0e-6F, 21.0e-6F, 21.0e-6F };
  // Mie absorption (1/meter, RGB). Default gives SSA ≈ 0.9.
  Vec3 mie_absorption_rgb_ { 2.33e-6F, 2.33e-6F, 2.33e-6F };
  float mie_scale_height_m_ = 1200.0F;
  float mie_g_ = 0.8F;

  Vec3 absorption_rgb_ { 0.65e-6F, 1.881e-6F, 0.085e-6F };
  float absorption_layer_width_m_ = 25000.0F;
  float absorption_term_below_ = 1.0F; // Linear ramp 0->1
  float absorption_term_above_ = -1.0F; // Linear ramp 1->0

  float multi_scattering_factor_ = 1.0F;

  bool sun_disk_enabled_ = true;

  float aerial_perspective_distance_scale_ = 1.0F;

  float aerial_scattering_strength_ = 1.0F;
};

} // namespace oxygen::scene::environment

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <optional>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/EnvironmentSystem.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene::environment {

//! Sun authoring mode.
enum class SunSource {
  //! Use authored sun parameters.
  kSynthetic,

  //! Resolve sun parameters from a scene directional light.
  kFromScene,
};

//! Scene-global sun parameters used by environment systems.
/*!
 The Sun component stores authored parameters for the scene's primary sun. The
 renderer resolves the effective sun per view based on this data and the active
 directional light selection rules.

 ### Key Features

 - **Authored Direction**: Supports both world-space vectors and azimuth/
   elevation authoring.
 - **Spectral Control**: Color and intensity in lux, with optional temperature
   conversion to linear RGB.
 - **Light Binding**: Optional reference to a scene directional light for
   FromScene mode.

 ### Usage Patterns

 Use `SetSunSource()` to switch between authored (`kSynthetic`) and
 scene-driven (`kFromScene`) behavior. When authoring direction, either set
 the world-space vector or the azimuth/elevation pair; the component keeps
 them consistent.

 ### Architecture Notes

 The component stores only authored data. Renderer systems resolve the effective
 per-view sun and populate dynamic GPU data.

 @see EnvironmentSystem, SceneEnvironment
*/
class Sun final : public EnvironmentSystem {
  OXYGEN_COMPONENT(Sun)

public:
  //! Constructs the sun component with engine defaults.
  OXGN_SCN_API Sun();

  //! Virtual destructor.
  ~Sun() override = default;

  OXYGEN_DEFAULT_COPYABLE(Sun)
  OXYGEN_DEFAULT_MOVABLE(Sun)

  //! Sets the sun source mode.
  OXGN_SCN_API auto SetSunSource(SunSource source) noexcept -> void;

  //! Gets the sun source mode.
  OXGN_SCN_NDAPI auto GetSunSource() const noexcept -> SunSource;

  //! Sets the world-space direction toward the sun (normalized).
  OXGN_SCN_API auto SetDirectionWs(const Vec3& direction_ws) noexcept -> void;

  //! Gets the world-space direction toward the sun.
  OXGN_SCN_NDAPI auto GetDirectionWs() const noexcept -> const Vec3&;

  //! Sets the azimuth and elevation in degrees.
  OXGN_SCN_API auto SetAzimuthElevationDegrees(
    float azimuth_deg, float elevation_deg) noexcept -> void;

  //! Gets the sun azimuth in degrees.
  OXGN_SCN_NDAPI auto GetAzimuthDegrees() const noexcept -> float;

  //! Gets the sun elevation in degrees.
  OXGN_SCN_NDAPI auto GetElevationDegrees() const noexcept -> float;

  //! Sets the sun color (linear RGB) and clears temperature overrides.
  OXGN_SCN_API auto SetColorRgb(const Vec3& rgb) noexcept -> void;

  //! Gets the cached sun color (linear RGB).
  OXGN_SCN_NDAPI auto GetColorRgb() const noexcept -> const Vec3&;

  //! Sets the sun intensity in lux.
  OXGN_SCN_API auto SetIntensityLux(float intensity_lux) noexcept -> void;

  //! Gets the sun intensity in lux.
  OXGN_SCN_NDAPI auto GetIntensityLux() const noexcept -> float;

  //! Sets the sun disk angular radius in radians.
  OXGN_SCN_API auto SetDiskAngularRadiusRadians(float radians) noexcept -> void;

  //! Gets the sun disk angular radius in radians.
  OXGN_SCN_NDAPI auto GetDiskAngularRadiusRadians() const noexcept -> float;

  //! Sets whether the sun casts shadows when synthesized.
  OXGN_SCN_API auto SetCastsShadows(bool casts_shadows) noexcept -> void;

  //! Gets whether the sun casts shadows when synthesized.
  OXGN_SCN_NDAPI auto CastsShadows() const noexcept -> bool;

  //! Sets the sun light temperature in Kelvin and updates cached color.
  OXGN_SCN_API auto SetLightTemperatureKelvin(float kelvin) noexcept -> void;

  //! Gets the sun light temperature in Kelvin or 0 when unset.
  OXGN_SCN_NDAPI auto GetLightTemperatureKelvin() const noexcept -> float;

  //! Returns whether a light temperature override is set.
  OXGN_SCN_NDAPI auto HasLightTemperature() const noexcept -> bool;

  //! Clears the light temperature override.
  OXGN_SCN_API auto ClearLightTemperature() noexcept -> void;

  //! Sets a reference to a scene directional light node.
  OXGN_SCN_API auto SetLightReference(const scene::SceneNode& node) noexcept
    -> void;

  //! Gets the optional directional light reference.
  OXGN_SCN_NDAPI auto GetLightReference() const noexcept
    -> const std::optional<scene::SceneNode>&;

  //! Clears the directional light reference.
  OXGN_SCN_API auto ClearLightReference() noexcept -> void;

private:
  SunSource sun_source_ = SunSource::kFromScene;
  std::optional<scene::SceneNode> light_reference_ {};

  Vec3 direction_ws_ { 0.0F, 0.866F, 0.5F };
  float azimuth_deg_ = 90.0F;
  float elevation_deg_ = 30.0F;

  Vec3 color_rgb_ { 1.0F, 1.0F, 1.0F };
  // NOTE: LDR pipeline defaults; update once HDR + tone mapping is wired.
  float intensity_lux_ = 10.0F;
  float disk_angular_radius_rad_ = 0.004675F;
  bool casts_shadows_ = true;
  std::optional<float> temperature_kelvin_ = std::nullopt;
};

} // namespace oxygen::scene::environment

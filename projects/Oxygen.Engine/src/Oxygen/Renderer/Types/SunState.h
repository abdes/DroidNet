//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace oxygen::engine {

//! Immutable value object representing the sun's state for atmospheric systems.
/*!
 Encapsulates all sun-related parameters needed by the atmosphere, lighting,
 and LUT generation systems. Provides derived quantities (zenith cosine,
 illuminance) computed once at construction.

 ### Design Rationale

 The sun is fundamental to atmospheric scattering, aerial perspective, and
 sky rendering. Rather than passing individual parameters (direction, color,
 intensity, zenith cosine) piecemeal through multiple interfaces, this value
 object groups all sun state together with clear semantics:

 - **Semantic clarity**: `direction_ws` is always toward the sun (normalized)
 - **Derived values cached**: `cos_zenith`, `illuminance` computed once
 - **Immutable**: Create a new instance when sun changes; no partial updates
 - **GPU-friendly**: Layout designed for easy packing into constant buffers

 ### Coordinate Convention

 Uses Z-up world space:
 - `direction_ws.z` = cos(zenith angle) where zenith is angle from +Z axis
 - Zenith = 0° means sun directly overhead (direction_ws = {0,0,1})
 - Zenith = 90° means sun at horizon (direction_ws.z = 0)

 ### Usage

 ```cpp
 // From scene light
 SunState sun = SunState::FromDirectionAndLight(
     glm::normalize(light_direction),
     light_color,
     light_intensity);

 // From azimuth/elevation (degrees)
 SunState sun = SunState::FromAzimuthElevation(
     45.0F,   // azimuth: 0=+X, 90=+Y
     30.0F,   // elevation: degrees above horizon
     {1,1,1}, // color
     2.0F);   // intensity

 // Pass to systems
 lut_manager->UpdateSunState(sun);
 env_dynamic_manager->SetSunState(view_id, sun);
 ```

 @see SkyAtmosphereLutManager, EnvironmentDynamicDataManager
*/
struct SunState {
  //! Direction toward the sun in world space (normalized, Z-up).
  glm::vec3 direction_ws { 0.0F, 0.866F, 0.5F };

  //! Sun color (linear RGB, not premultiplied by intensity).
  glm::vec3 color_rgb { 1.0F, 1.0F, 1.0F };

  //! Sun intensity multiplier.
  float intensity { 1.0F };

  //! Computed illuminance (intensity * max(color_rgb)).
  float illuminance { 1.0F };

  //! Cosine of zenith angle (direction_ws.z). Cached for atmosphere lookups.
  float cos_zenith { 0.5F };

  //! Whether this sun state is valid/enabled.
  bool enabled { true };

  //=== Constructors ===------------------------------------------------------//

  //! Default constructor: sun at 30° elevation, white, intensity 1.
  constexpr SunState() = default;

  //! Constructs from direction and light parameters.
  /*!
   @param direction Normalized direction toward sun (Z-up world space).
   @param color Linear RGB color (not premultiplied).
   @param inten Intensity multiplier.
   @param is_enabled Whether this sun contributes to rendering.
  */
  static auto FromDirectionAndLight(const glm::vec3& direction,
    const glm::vec3& color, float inten, bool is_enabled = true) -> SunState
  {
    SunState s;
    s.direction_ws = glm::normalize(direction);
    s.color_rgb = color;
    s.intensity = inten;
    s.illuminance = inten * std::max(color.x, std::max(color.y, color.z));
    s.cos_zenith = s.direction_ws.z;
    s.enabled = is_enabled;
    return s;
  }

  //! Constructs from azimuth and elevation angles (degrees).
  /*!
   @param azimuth_deg Horizontal angle in degrees (0=+X, 90=+Y, CCW from above).
   @param elevation_deg Angle above horizon in degrees (0=horizon, 90=zenith).
   @param color Linear RGB color.
   @param inten Intensity multiplier.
   @param is_enabled Whether this sun contributes to rendering.
  */
  static auto FromAzimuthElevation(float azimuth_deg, float elevation_deg,
    const glm::vec3& color, float inten, bool is_enabled = true) -> SunState
  {
    constexpr float kDegToRad = 3.14159265359F / 180.0F;
    const float az = azimuth_deg * kDegToRad;
    const float el = elevation_deg * kDegToRad;

    const float cos_el = std::cos(el);
    const float sin_el = std::sin(el);
    const float cos_az = std::cos(az);
    const float sin_az = std::sin(az);

    // Z-up: elevation rotates from XY plane toward +Z
    const glm::vec3 dir {
      cos_el * cos_az,
      cos_el * sin_az,
      sin_el,
    };

    return FromDirectionAndLight(dir, color, inten, is_enabled);
  }

  //=== Derived Accessors
  //===--------------------------------------------------//

  //! Returns the luminance-weighted color (color * intensity).
  [[nodiscard]] auto GetLuminance() const noexcept -> glm::vec3
  {
    return color_rgb * intensity;
  }

  //! Returns sin(zenith) for atmosphere calculations.
  [[nodiscard]] auto GetSinZenith() const noexcept -> float
  {
    return std::sqrt(std::max(0.0F, 1.0F - cos_zenith * cos_zenith));
  }

  //! Returns elevation angle in radians (0 = horizon, π/2 = overhead).
  [[nodiscard]] auto GetElevationRadians() const noexcept -> float
  {
    return std::asin(std::clamp(cos_zenith, -1.0F, 1.0F));
  }

  //! Returns azimuth angle in radians (0 = +X, π/2 = +Y).
  [[nodiscard]] auto GetAzimuthRadians() const noexcept -> float
  {
    return std::atan2(direction_ws.y, direction_ws.x);
  }

  //=== Comparison ===--------------------------------------------------------//

  //! Equality comparison with epsilon tolerance for floats.
  [[nodiscard]] auto ApproxEquals(
    const SunState& other, float epsilon = 0.001F) const noexcept -> bool
  {
    const auto vec3_approx = [epsilon](const glm::vec3& a, const glm::vec3& b) {
      return std::abs(a.x - b.x) < epsilon && std::abs(a.y - b.y) < epsilon
        && std::abs(a.z - b.z) < epsilon;
    };

    return enabled == other.enabled
      && vec3_approx(direction_ws, other.direction_ws)
      && vec3_approx(color_rgb, other.color_rgb)
      && std::abs(intensity - other.intensity) < epsilon;
  }

  //! Returns true if only elevation changed (azimuth can differ).
  /*!
   Used by LUT manager to determine if regeneration is needed.
   Sun-relative LUT parameterization only cares about elevation.
  */
  [[nodiscard]] auto ElevationDiffers(
    const SunState& other, float epsilon = 0.001F) const noexcept -> bool
  {
    return std::abs(cos_zenith - other.cos_zenith) > epsilon;
  }
};

//! Disabled/invalid sun state constant.
inline constexpr SunState kNoSun = [] {
  SunState s;
  s.enabled = false;
  s.intensity = 0.0F;
  s.illuminance = 0.0F;
  return s;
}();

} // namespace oxygen::engine

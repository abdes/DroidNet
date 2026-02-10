//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Scene/Environment/Sun.h>

namespace oxygen::scene::environment {

namespace {

  //=== Sun helpers
  //===--------------------------------------------------------//

  [[nodiscard]] auto NormalizeDirectionOrFallback(
    const Vec3& direction_ws, const Vec3& fallback) noexcept -> Vec3
  {
    const float length = glm::length(direction_ws);
    if (length <= oxygen::math::EpsilonDirection) {
      return fallback;
    }
    return glm::normalize(direction_ws);
  }

  /*!
   Converts a direction vector to azimuth/elevation degrees.

   @param direction_ws Normalized world-space direction toward the sun.
   @return A pair of degrees: { azimuth, elevation }.

   ### Performance Characteristics

   - Time Complexity: O(1)
   - Memory: None.
   - Optimization: Trigonometric evaluation only.

   @note Azimuth is measured in the X/Y plane with 0° along +X and 90° along
     +Y.
   @see AzimuthElevationToDirection
  */
  [[nodiscard]] auto DirectionToAzimuthElevation(
    const Vec3& direction_ws) noexcept -> std::pair<float, float>
  {
    const Vec3 normalized = glm::normalize(direction_ws);
    const float azimuth_rad = std::atan2(normalized.y, normalized.x);
    const float elevation_rad
      = std::asin(glm::clamp(normalized.z, -1.0F, 1.0F));

    float azimuth_deg = azimuth_rad * oxygen::math::RadToDeg;
    if (azimuth_deg < 0.0F) {
      azimuth_deg = std::fmod(azimuth_deg + 360.0F, 360.0F);
    }

    const float elevation_deg = elevation_rad * oxygen::math::RadToDeg;
    return { azimuth_deg, elevation_deg };
  }

  /*!
   Converts azimuth/elevation degrees to a world-space direction.

   @param azimuth_deg Azimuth in degrees (0° = +X, 90° = +Y).
   @param elevation_deg Elevation in degrees (0° = horizon, 90° = zenith).
   @return Normalized world-space direction toward the sun.

   ### Performance Characteristics

   - Time Complexity: O(1)
   - Memory: None.
   - Optimization: Trigonometric evaluation only.

   @see DirectionToAzimuthElevation
  */
  [[nodiscard]] auto AzimuthElevationToDirection(
    const float azimuth_deg, const float elevation_deg) noexcept -> Vec3
  {
    const float azimuth_rad = azimuth_deg * oxygen::math::DegToRad;
    const float elevation_rad = elevation_deg * oxygen::math::DegToRad;

    const float cos_elevation = glm::cos(elevation_rad);
    const Vec3 direction {
      cos_elevation * glm::cos(azimuth_rad),
      cos_elevation * glm::sin(azimuth_rad),
      glm::sin(elevation_rad),
    };

    return glm::normalize(direction);
  }

  /*!
   Converts a Kelvin temperature to normalized linear RGB.

   @param kelvin Temperature in Kelvin.
   @return Normalized linear RGB with max component set to 1.

   ### Performance Characteristics

   - Time Complexity: O(1)
   - Memory: None.
   - Optimization: Polynomial approximation of blackbody color.

   @note Uses the Tanner Helland approximation for 1000K-40000K.
  */
  [[nodiscard]] auto KelvinToLinearRgb(float kelvin) noexcept -> glm::vec3
  {
    kelvin = glm::clamp(kelvin, 1000.0F, 40000.0F);

    const float temp = kelvin / 100.0F;

    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;

    if (temp <= 66.0F) {
      r = 255.0F;
    } else {
      const float value = temp - 60.0F;
      r = 329.698727446F * std::pow(value, -0.1332047592F);
      r = glm::clamp(r, 0.0F, 255.0F);
    }

    if (temp <= 66.0F) {
      g = 99.4708025861F * std::log(temp) - 161.1195681661F;
      g = glm::clamp(g, 0.0F, 255.0F);
    } else {
      const float value = temp - 60.0F;
      g = 288.1221695283F * std::pow(value, -0.0755148492F);
      g = glm::clamp(g, 0.0F, 255.0F);
    }

    if (temp >= 66.0F) {
      b = 255.0F;
    } else if (temp <= 19.0F) {
      b = 0.0F;
    } else {
      const float value = temp - 10.0F;
      b = 138.5177312231F * std::log(value) - 305.0447927307F;
      b = glm::clamp(b, 0.0F, 255.0F);
    }

    glm::vec3 rgb { r / 255.0F, g / 255.0F, b / 255.0F };

    const float max_component = std::max(std::max(rgb.r, rgb.g), rgb.b);
    if (max_component > 0.0F) {
      rgb /= max_component;
    }

    return rgb;
  }

} // namespace

//=== Sun public API ===-----------------------------------------------------//

/*!
 Constructs the sun component with defaults.

 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Default member initialization only.
*/
Sun::Sun() = default;

/*!
 Sets the sun source mode.

 @param source The sun source to use for resolution.
 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct assignment.
*/
auto Sun::SetSunSource(const SunSource source) noexcept -> void
{
  sun_source_ = source;
}

/*!
 Gets the sun source mode.

 @return The current sun source mode.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::GetSunSource() const noexcept -> SunSource { return sun_source_; }

/*!
 Sets the world-space direction toward the sun.

 @param direction_ws Direction toward the sun in world space.
 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Normalization and trigonometric conversion.

 @note The direction is normalized before being stored.
*/
auto Sun::SetDirectionWs(const Vec3& direction_ws) noexcept -> void
{
  direction_ws_ = NormalizeDirectionOrFallback(direction_ws, direction_ws_);

  const auto azimuth_elevation = DirectionToAzimuthElevation(direction_ws_);
  azimuth_deg_ = azimuth_elevation.first;
  elevation_deg_ = azimuth_elevation.second;
}

/*!
 Gets the world-space direction toward the sun.

 @return Normalized world-space direction.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::GetDirectionWs() const noexcept -> const Vec3&
{
  return direction_ws_;
}

/*!
 Sets the azimuth and elevation in degrees.

 @param azimuth_deg Azimuth in degrees (0° = +X, 90° = +Y).
 @param elevation_deg Elevation in degrees (0° = horizon, 90° = zenith).
 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Trigonometric conversion.
*/
auto Sun::SetAzimuthElevationDegrees(
  const float azimuth_deg, const float elevation_deg) noexcept -> void
{
  azimuth_deg_ = azimuth_deg;
  elevation_deg_ = elevation_deg;
  direction_ws_ = AzimuthElevationToDirection(azimuth_deg_, elevation_deg_);
}

/*!
 Gets the sun azimuth in degrees.

 @return The azimuth angle in degrees.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::GetAzimuthDegrees() const noexcept -> float { return azimuth_deg_; }

/*!
 Gets the sun elevation in degrees.

 @return The elevation angle in degrees.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::GetElevationDegrees() const noexcept -> float
{
  return elevation_deg_;
}

/*!
 Sets the sun color in linear RGB.

 @param rgb Linear RGB color.
 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Clears temperature override for consistency.
*/
auto Sun::SetColorRgb(const Vec3& rgb) noexcept -> void
{
  color_rgb_ = rgb;
  temperature_kelvin_.reset();
}

/*!
 Gets the cached sun color in linear RGB.

 @return Cached linear RGB color.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::GetColorRgb() const noexcept -> const Vec3& { return color_rgb_; }

/*!
 Sets the sun illuminance in lux.

 @param illuminance_lx Sun illuminance in lux.
 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct assignment.
*/
auto Sun::SetIlluminanceLx(const float illuminance_lx) noexcept -> void
{
  illuminance_lx_ = illuminance_lx;
}

/*!
 Gets the sun illuminance in lux.

 @return Sun illuminance in lux.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::GetIlluminanceLx() const noexcept -> float { return illuminance_lx_; }

/*!
 Sets the sun disk angular radius in radians.

 @param radians Angular radius in radians.
 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct assignment.
*/
auto Sun::SetDiskAngularRadiusRadians(const float radians) noexcept -> void
{
  disk_angular_radius_rad_ = radians;
}

/*!
 Gets the sun disk angular radius in radians.

 @return Angular radius in radians.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::GetDiskAngularRadiusRadians() const noexcept -> float
{
  return disk_angular_radius_rad_;
}

/*!
 Sets whether the sun casts shadows when synthesized.

 @param casts_shadows True if the sun casts shadows.
 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct assignment.
*/
auto Sun::SetCastsShadows(const bool casts_shadows) noexcept -> void
{
  casts_shadows_ = casts_shadows;
}

/*!
 Gets whether the sun casts shadows when synthesized.

 @return True if the sun casts shadows.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::CastsShadows() const noexcept -> bool { return casts_shadows_; }

/*!
 Sets the sun temperature in Kelvin and caches the derived color.

 @param kelvin Temperature in Kelvin.
 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Single conversion and cached result.
*/
auto Sun::SetLightTemperatureKelvin(const float kelvin) noexcept -> void
{
  temperature_kelvin_ = kelvin;
  color_rgb_ = KelvinToLinearRgb(kelvin);
}

/*!
 Gets the sun temperature in Kelvin.

 @return The temperature in Kelvin, or 0 when no temperature is set.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::GetLightTemperatureKelvin() const noexcept -> float
{
  return temperature_kelvin_.value_or(0.0F);
}

/*!
 Returns whether a temperature override is set.

 @return True if a temperature override is active.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::HasLightTemperature() const noexcept -> bool
{
  return temperature_kelvin_.has_value();
}

/*!
 Clears the temperature override without changing the cached color.

 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct assignment.
*/
auto Sun::ClearLightTemperature() noexcept -> void
{
  temperature_kelvin_.reset();
}

/*!
 Sets a reference to a scene directional light node.

 @param node Scene node that owns a directional light.
 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct assignment.
*/
auto Sun::SetLightReference(const scene::SceneNode& node) noexcept -> void
{
  light_reference_ = node;
}

/*!
 Gets the optional directional light reference.

 @return Optional reference to a scene node.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct access.
*/
auto Sun::GetLightReference() const noexcept
  -> const std::optional<scene::SceneNode>&
{
  return light_reference_;
}

/*!
 Clears the directional light reference.

 @return None.

 ### Performance Characteristics

 - Time Complexity: O(1)
 - Memory: None.
 - Optimization: Direct assignment.
*/
auto Sun::ClearLightReference() noexcept -> void { light_reference_.reset(); }

} // namespace oxygen::scene::environment

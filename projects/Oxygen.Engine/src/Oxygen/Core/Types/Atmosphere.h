//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <type_traits>

#include <glm/vec3.hpp>

namespace oxygen::engine::atmos {

//! Radius of the planet in meters (Earth ≈ 6360km).
inline constexpr float kDefaultPlanetRadiusM = 6360000.0F;

//! Earth's average radius in meters.
inline constexpr float kEarthRadiusM = 6360000.0F;

//! Height of the atmosphere in meters (Earth ≈ 100km).
inline constexpr float kDefaultAtmosphereHeightM = 100000.0F;

//! Earth's atmosphere height in meters.
inline constexpr float kEarthAtmosphereHeightM = 100000.0F;

//! Default planet up direction (+Z).
inline constexpr glm::vec3 kDefaultPlanetUp { 0.0F, 0.0F, 1.0F };

//! Sun disk angular radius in radians (Earth sun ≈ 0.545 degrees total).
inline constexpr float kDefaultSunDiskAngularRadiusRad = 0.004756022F;

//! Default sun color (linear RGB white).
inline constexpr glm::vec3 kDefaultSunColorRgb { 1.0F, 1.0F, 1.0F };

//! Default sun illuminance in Lux (Earth at noon ≈ 100,000 Lux).
//! By default we use 0.0F to indicate "unset" or "unlighted" if disabled,
//! but the default payload uses 0.0F as a placeholder.
inline constexpr float kDefaultSunIlluminanceLx = 0.0F;

//! Standard baseline sky luminance for non-physical cubemaps (Nits).
inline constexpr float kStandardSkyLuminance = 5000.0F;

//! Default sun elevation in degrees (30.0 degrees).
inline constexpr float kDefaultSunElevationDeg = 30.0F;

//! Default sun direction (Z-up: +Y with 30° elevation).
//! Direction vector is towards the sun (not incoming radiance direction).
inline constexpr glm::vec3 kDefaultSunDirection { 0.0F, 0.8660254F, 0.5F };

//! Default aerial perspective controls.
inline constexpr float kDefaultAerialPerspectiveDistanceScale = 1.0F;
inline constexpr float kDefaultAerialScatteringStrength = 1.0F;

//! Rayleigh scattering coefficients at sea level (Earth-like).
inline constexpr glm::vec3 kDefaultRayleighScatteringRgb { 5.802e-6F,
  13.558e-6F, 33.1e-6F };

//! Rayleigh scale height in meters (Earth ≈ 8km).
inline constexpr float kDefaultRayleighScaleHeightM = 8000.0F;

//! Mie scattering coefficients at sea level (Earth-like).
inline constexpr glm::vec3 kDefaultMieScatteringRgb { 3.996e-6F, 3.996e-6F,
  3.996e-6F };

//! Mie absorption coefficients at sea level (Earth-like).
inline constexpr glm::vec3 kDefaultMieAbsorptionRgb { 4.405e-7F, 4.405e-7F,
  4.405e-7F };

//! Mie extinction (scattering + absorption) at sea level.
inline constexpr glm::vec3 kDefaultMieExtinctionRgb { kDefaultMieScatteringRgb
  + kDefaultMieAbsorptionRgb };

//! Mie scale height in meters (Earth ≈ 1.2km).
inline constexpr float kDefaultMieScaleHeightM = 1200.0F;

//! Mie phase function anisotropy (Earth ≈ 0.8).
inline constexpr float kDefaultMieAnisotropyG = 0.8F;

//! Ozone (Absorption) coefficients at peak density (Earth-like).
inline constexpr glm::vec3 kDefaultOzoneAbsorptionRgb { 0.650e-6F, 1.881e-6F,
  0.085e-6F };

//! Default ozone profile altitude bounds in meters (Earth-like).
inline constexpr float kDefaultOzoneBottomM = 10000.0F;
inline constexpr float kDefaultOzonePeakM = 25000.0F;
inline constexpr float kDefaultOzoneTopM = 40000.0F;

//! Defines a single atmospheric density layer (linear distribution).
/*!
  Typically used for Ozone (absorption) in a 2-layer tent profile.
*/
struct DensityLayer {
  float width_m { 0.0F };
  float exp_term { 0.0F }; // e.g., for exponential layers
  float linear_term { 0.0F };
  float constant_term { 0.0F };

  auto operator<=>(const DensityLayer&) const = default;
};

//! Defines an atmospheric density profile with multiple layers.
/*!
  Aligned with UE5/Hillaire 2020 for piecewise linear density models.
  Up to 2 layers are supported in the core renderer.
*/
struct DensityProfile {
  std::array<DensityLayer, 2> layers {};

  auto operator<=>(const DensityProfile&) const = default;
};

static_assert(std::is_standard_layout_v<DensityLayer>);
static_assert(std::is_standard_layout_v<DensityProfile>);
static_assert(sizeof(DensityLayer) == 16); // NOLINT(*-magic-numbers)
static_assert(sizeof(DensityProfile) == 32); // NOLINT(*-magic-numbers)

//! Creates a 2-layer linear ozone density profile.
/*!
 The profile follows the piecewise linear distribution commonly used in
 real-time sky models:

 - `bottom_m` to `peak_m`: linear increase (0.0 -> 1.0)
 - `peak_m` to `top_m`: linear decrease (1.0 -> 0.0)
 - below `bottom_m` and above `top_m`: density clamps to 0.0
*/
[[nodiscard]] constexpr auto MakeOzoneTwoLayerLinearDensityProfile(
  const float bottom_m, const float peak_m, const float top_m) noexcept
  -> DensityProfile
{
  const float denom_below = peak_m - bottom_m;
  const float denom_above = top_m - peak_m;

  const float slope_below = (denom_below > 0.0F) ? (1.0F / denom_below) : 0.0F;
  const float slope_above = (denom_above > 0.0F) ? (-1.0F / denom_above) : 0.0F;

  return DensityProfile {
    .layers = {
      DensityLayer {
        .width_m = peak_m,
        .exp_term = 0.0F,
        .linear_term = slope_below,
        .constant_term = -bottom_m * slope_below,
      },
      DensityLayer {
        .width_m = 0.0F,
        .exp_term = 0.0F,
        .linear_term = slope_above,
        .constant_term = (denom_above > 0.0F) ? (top_m / denom_above) : 0.0F,
      },
    },
  };
}

//! Default ozone density profile (2-layer tent).
inline constexpr DensityProfile kDefaultOzoneDensityProfile
  = MakeOzoneTwoLayerLinearDensityProfile(
    kDefaultOzoneBottomM, kDefaultOzonePeakM, kDefaultOzoneTopM);

} // namespace oxygen::engine::atmos

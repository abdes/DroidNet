//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <type_traits>

#include <glm/vec3.hpp>

namespace oxygen::engine::atmos {

//! Radius of the planet in meters (Earth ≈ 6360km).
inline constexpr float kDefaultPlanetRadiusM = 6360000.0F;

//! Height of the atmosphere in meters (Earth ≈ 80km).
inline constexpr float kDefaultAtmosphereHeightM = 80000.0F;

//! Sun disk angular radius in radians (Earth sun ≈ 0.5 degrees total, so 0.25
//! deg radius).
inline constexpr float kDefaultSunDiskAngularRadiusRad = 0.004675F;

//! Standard baseline sky luminance for non-physical cubemaps (Nits).
inline constexpr float kStandardSkyLuminance = 5000.0F;

//! Rayleigh scattering coefficients at sea level (Earth-like).
inline constexpr glm::vec3 kDefaultRayleighScatteringRgb { 5.805e-6F,
  13.558e-6F, 33.1e-6F };

//! Rayleigh scale height in meters (Earth ≈ 8km).
inline constexpr float kDefaultRayleighScaleHeightM = 8000.0F;

//! Mie scattering coefficients at sea level (Earth-like).
inline constexpr glm::vec3 kDefaultMieScatteringRgb { 3.996e-6F, 3.996e-6F,
  3.996e-6F };

//! Mie absorption coefficients at sea level (Earth-like).
inline constexpr glm::vec3 kDefaultMieAbsorptionRgb { 4.405e-6F, 4.405e-6F,
  4.405e-6F };

//! Mie extinction (scattering + absorption) at sea level.
inline constexpr glm::vec3 kDefaultMieExtinctionRgb { kDefaultMieScatteringRgb
  + kDefaultMieAbsorptionRgb };

//! Mie scale height in meters (Earth ≈ 1.2km).
inline constexpr float kDefaultMieScaleHeightM = 1200.0F;

//! Mie phase function anisotropy (Earth ≈ 0.8).
inline constexpr float kDefaultMieAnisotropyG = 0.8F;

//! Ozone (Absorption) coefficients at sea level.
inline constexpr glm::vec3 kDefaultOzoneAbsorptionRgb { 0.650e-6F, 1.881e-6F,
  0.085e-6F };

//! Default ozone hump center in meters (Earth ≈ 25km).
inline constexpr float kDefaultOzoneCenterM = 25000.0F;

//! Default ozone hump width in meters (Earth ≈ 15km).
inline constexpr float kDefaultOzoneWidthM = 15000.0F;

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
  DensityLayer layers[2];

  auto operator<=>(const DensityProfile&) const = default;
};

static_assert(std::is_standard_layout_v<DensityLayer>);
static_assert(std::is_standard_layout_v<DensityProfile>);
static_assert(sizeof(DensityLayer) == 16);
static_assert(sizeof(DensityProfile) == 32);

//! Creates a 2-layer tent ozone density profile.
/*!
 The tent peaks at `center_m` with value 1.0, and reaches 0.0 at
 `center_m ± width_m/2`.
*/
[[nodiscard]] constexpr auto MakeOzoneTentDensityProfile(
  const float center_m, const float width_m) noexcept -> DensityProfile
{
  const float half_width_m = width_m * 0.5F;
  const float inv_half_width
    = (half_width_m > 0.0F) ? (1.0F / half_width_m) : 0.0F;

  return DensityProfile {
      .layers = {
        DensityLayer {
          .width_m = center_m,
          .exp_term = 0.0F,
          .linear_term = inv_half_width,
          .constant_term = (half_width_m > 0.0F)
            ? (-(center_m - half_width_m) * inv_half_width)
            : 1.0F,
        },
        DensityLayer {
          .width_m = 0.0F,
          .exp_term = 0.0F,
          .linear_term = -inv_half_width,
          .constant_term = (half_width_m > 0.0F)
            ? ((center_m + half_width_m) * inv_half_width)
            : 1.0F,
        },
      },
    };
}

//! Default ozone density profile (2-layer tent).
inline constexpr DensityProfile kDefaultOzoneDensityProfile
  = MakeOzoneTentDensityProfile(kDefaultOzoneCenterM, kDefaultOzoneWidthM);

} // namespace oxygen::engine::atmos

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>

#include <Oxygen/Core/Constants.h>

namespace oxygen::scene {

//! Enumerates how a light participates in runtime vs baked lighting workflows.
enum class LightMobility : std::uint8_t {
  kRealtime,
  kMixed,
  kBaked,
};

//! Hint for renderer shadow map resolution selection.
enum class ShadowResolutionHint : std::uint8_t {
  kLow,
  kMedium,
  kHigh,
  kUltra,
};

//! Enumerates supported attenuation/falloff models for local lights.
enum class AttenuationModel : std::uint8_t {
  kInverseSquare,
  kLinear,
  kCustomExponent,
};

//! Common shadow tuning knobs shared by all light types.
struct ShadowSettings {
  float bias = 0.0F;
  float normal_bias = 0.0F;
  bool contact_shadows = false;
  ShadowResolutionHint resolution_hint = ShadowResolutionHint::kMedium;
};

//! Authored properties shared by all light types.
/*!
  This structure contains the common parameters for all light types in the
  engine. Intensity values with explicit physical units are stored in specific
  light classes, not here:
  - DirectionalLight: intensity_lux (lm/m²)
  - PointLight/SpotLight: luminous_flux_lm (lm)

  @see DirectionalLight, PointLight, SpotLight
*/
struct CommonLightProperties {
  bool affects_world = true;
  Vec3 color_rgb { 1.0F, 1.0F, 1.0F };
  // intensity REMOVED - now in specific light classes with physical units

  LightMobility mobility = LightMobility::kRealtime;
  bool casts_shadows = false;
  ShadowSettings shadow {};
  float exposure_compensation_ev = 0.0F;
};

inline constexpr std::uint32_t kMaxShadowCascades = 4U;

//! Cascaded shadow map (CSM) configuration for directional lights.
struct CascadedShadowSettings {
  std::uint32_t cascade_count = kMaxShadowCascades;
  std::array<float, kMaxShadowCascades> cascade_distances {};
  float distribution_exponent = 1.0F;
};

} // namespace oxygen::scene

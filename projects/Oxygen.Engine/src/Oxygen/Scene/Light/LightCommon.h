//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
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

inline constexpr float kDefaultShadowBias = 0.0006F;
inline constexpr float kDefaultShadowNormalBias = 0.02F;

//! Common shadow tuning knobs shared by all light types.
struct ShadowSettings {
  float bias = kDefaultShadowBias;
  float normal_bias = kDefaultShadowNormalBias;
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

  //! Exposure compensation in stops (EV).
  //! Scale: logarithmic (base 2).
  //! Variation: +/- 1.0 reflects a doubling/halving of perceived intensity.
  float exposure_compensation_ev = 0.0F;
};

inline constexpr std::uint32_t kMaxShadowCascades = 4U;
inline constexpr std::array<float, kMaxShadowCascades>
  kDefaultDirectionalCascadeDistances { 8.0F, 24.0F, 64.0F, 160.0F };
inline constexpr float kDefaultDirectionalMaxShadowDistance = 160.0F;
inline constexpr float kDefaultDirectionalDistributionExponent = 3.0F;
inline constexpr float kDefaultDirectionalTransitionFraction = 0.1F;
inline constexpr float kDefaultDirectionalDistanceFadeoutFraction = 0.1F;

enum class DirectionalCsmSplitMode : std::uint8_t {
  kGenerated = 0,
  kManualDistances = 1,
};

[[nodiscard]] inline auto IsValidDirectionalCsmSplitMode(
  const DirectionalCsmSplitMode split_mode) noexcept -> bool
{
  switch (split_mode) {
  case DirectionalCsmSplitMode::kGenerated:
  case DirectionalCsmSplitMode::kManualDistances:
    return true;
  default:
    return false;
  }
}

//! Cascaded shadow map (CSM) configuration for directional lights.
struct CascadedShadowSettings {
  std::uint32_t cascade_count = kMaxShadowCascades;
  DirectionalCsmSplitMode split_mode = DirectionalCsmSplitMode::kGenerated;
  float max_shadow_distance = kDefaultDirectionalMaxShadowDistance;
  std::array<float, kMaxShadowCascades> cascade_distances
    = kDefaultDirectionalCascadeDistances;
  float distribution_exponent = kDefaultDirectionalDistributionExponent;
  float transition_fraction = kDefaultDirectionalTransitionFraction;
  float distance_fadeout_fraction = kDefaultDirectionalDistanceFadeoutFraction;
};

[[nodiscard]] inline auto CanonicalizeCascadedShadowSettings(
  CascadedShadowSettings settings) noexcept -> CascadedShadowSettings
{
  settings.cascade_count
    = std::clamp(settings.cascade_count, 1U, kMaxShadowCascades);
  if (!IsValidDirectionalCsmSplitMode(settings.split_mode)) {
    settings.split_mode = DirectionalCsmSplitMode::kGenerated;
  }

  if (!std::isfinite(settings.max_shadow_distance)
    || settings.max_shadow_distance <= 0.0F) {
    settings.max_shadow_distance = kDefaultDirectionalMaxShadowDistance;
  }

  bool has_valid_distances = true;
  float previous_distance = 0.0F;
  for (std::uint32_t i = 0U; i < settings.cascade_count; ++i) {
    const float distance = settings.cascade_distances[i];
    if (!std::isfinite(distance) || distance <= previous_distance) {
      has_valid_distances = false;
      break;
    }
    previous_distance = distance;
  }
  if (!has_valid_distances) {
    settings.cascade_distances = kDefaultDirectionalCascadeDistances;
  }

  if (!std::isfinite(settings.distribution_exponent)
    || settings.distribution_exponent < 1.0F) {
    settings.distribution_exponent = kDefaultDirectionalDistributionExponent;
  }

  if (!std::isfinite(settings.transition_fraction)) {
    settings.transition_fraction = kDefaultDirectionalTransitionFraction;
  }
  settings.transition_fraction
    = std::clamp(settings.transition_fraction, 0.0F, 1.0F);

  if (!std::isfinite(settings.distance_fadeout_fraction)) {
    settings.distance_fadeout_fraction
      = kDefaultDirectionalDistanceFadeoutFraction;
  }
  settings.distance_fadeout_fraction
    = std::clamp(settings.distance_fadeout_fraction, 0.0F, 1.0F);

  return settings;
}

} // namespace oxygen::scene

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include <Oxygen/Scene/Light/LightCommon.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Vortex/Types/LightingIndices.h>

namespace oxygen::vortex {

inline constexpr std::uint32_t kDirectionalLightAtmosphereModeFlagAuthority = 1U
  << 0U;
inline constexpr std::uint32_t
  kDirectionalLightAtmosphereModeFlagPerPixelTransmittance = 1U << 1U;
inline constexpr std::uint32_t
  kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance = 1U << 2U;
inline constexpr std::uint32_t kDirectionalLightShadowFlagCastsShadows = 1U
  << 0U;
inline constexpr std::uint32_t kFrameDirectionalLightMaxCascades = 4U;
inline constexpr std::uint32_t kLocalLightFlagCastsShadows = 1U << 0U;
inline constexpr std::uint32_t kLightFlagContactShadows = 1U << 1U;
inline constexpr std::uint32_t kLightRequestFlags
  = kLocalLightFlagCastsShadows | kLightFlagContactShadows;

enum class LocalLightKind : std::uint8_t {
  kPoint = 0U,
  kSpot = 1U,
};

enum class FrameDirectionalCsmSplitMode : std::uint8_t {
  kGenerated = 0U,
  kManualDistances = 1U,
};

struct FrameDirectionalLightSelection {
  scene::NodeHandle source_node;
  // Vector from the shaded point toward the directional-light source in
  // Oxygen world space (+Z up, -Y forward).
  glm::vec3 direction { 0.0F, -1.0F, 0.0F };
  float source_radius { 0.0F };

  glm::vec3 color { 1.0F, 1.0F, 1.0F };
  float illuminance_lux { 0.0F };
  float exposure_compensation_ev { 0.0F };

  glm::vec3 transmittance_toward_sun_rgb { 1.0F, 1.0F, 1.0F };

  std::uint32_t atmosphere_light_slot { kInvalidAtmosphereLightIndex.get() };
  std::uint32_t atmosphere_mode_flags { 0U };
  std::uint32_t shadow_flags { 0U };

  std::uint32_t cascade_count { 0U };

  FrameDirectionalCsmSplitMode cascade_split_mode {
    FrameDirectionalCsmSplitMode::kGenerated,
  };
  float max_shadow_distance { 160.0F };
  std::array<float, kFrameDirectionalLightMaxCascades> cascade_distances {
    8.0F,
    24.0F,
    64.0F,
    160.0F,
  };
  float distribution_exponent { 3.0F };

  float transition_fraction { 0.1F };
  float distance_fadeout_fraction { 0.1F };
  float shadow_bias { 0.0F };
  float shadow_normal_bias { 0.02F };
  scene::ShadowResolutionHint shadow_resolution_hint {
    scene::ShadowResolutionHint::kMedium,
  };
};

struct FrameLocalLightSelection {
  LocalLightKind kind { LocalLightKind::kPoint };

  glm::vec3 position { 0.0F };
  float range { 0.0F };

  glm::vec3 color { 1.0F, 1.0F, 1.0F };
  float luminous_flux_lm { 0.0F };
  float exposure_compensation_ev { 0.0F };

  glm::vec3 direction { 0.0F, -1.0F, 0.0F };

  float inner_cone_half_angle_radians {
    scene::SpotLight::kDefaultInnerConeAngle
  };
  float outer_cone_half_angle_radians {
    scene::SpotLight::kDefaultOuterConeAngle
  };
  float source_radius { 0.0F };
  std::uint32_t flags { 0U };

  float shadow_bias { 0.0F };
  float shadow_normal_bias { 0.02F };
  scene::ShadowResolutionHint shadow_resolution_hint {
    scene::ShadowResolutionHint::kMedium,
  };
  std::uint32_t _padding0 { 0U };
};

struct FrameLightSelection {
  std::vector<FrameDirectionalLightSelection> directional_lights;
  std::vector<FrameLocalLightSelection> local_lights;
  std::uint64_t selection_epoch { 0U };
  std::uint64_t scene_generation { 0U };

  [[nodiscard]] auto directional_light_count() const noexcept -> std::uint32_t
  {
    return static_cast<std::uint32_t>(directional_lights.size());
  }

  [[nodiscard]] auto local_light_count() const noexcept -> std::uint32_t
  {
    return static_cast<std::uint32_t>(local_lights.size());
  }

  [[nodiscard]] auto empty() const noexcept -> bool
  {
    return directional_lights.empty() && local_lights.empty();
  }
};

} // namespace oxygen::vortex

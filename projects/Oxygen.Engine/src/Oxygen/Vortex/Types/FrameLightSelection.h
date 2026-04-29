//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

namespace oxygen::vortex {

inline constexpr std::uint32_t
  kDirectionalLightAtmosphereModeFlagAuthority = 1U << 0U;
inline constexpr std::uint32_t
  kDirectionalLightAtmosphereModeFlagPerPixelTransmittance = 1U << 1U;
inline constexpr std::uint32_t
  kDirectionalLightAtmosphereModeFlagHasBakedGroundTransmittance = 1U << 2U;
inline constexpr std::uint32_t kDirectionalLightShadowFlagCastsShadows
  = 1U << 0U;
inline constexpr std::uint32_t kFrameDirectionalLightMaxCascades = 4U;
inline constexpr std::uint32_t kLocalLightFlagCastsShadows = 1U << 0U;

enum class LocalLightKind : std::uint32_t {
  kPoint = 0U,
  kSpot = 1U,
};

enum class FrameDirectionalCsmSplitMode : std::uint32_t {
  kGenerated = 0U,
  kManualDistances = 1U,
};

struct FrameDirectionalLightSelection {
  // Vector from the shaded point toward the directional-light source in
  // Oxygen world space (+Z up, -Y forward).
  glm::vec3 direction { 0.0F, -1.0F, 0.0F };
  float source_radius { 0.0F };

  glm::vec3 color { 1.0F, 1.0F, 1.0F };
  float illuminance_lux { 0.0F };

  glm::vec3 transmittance_toward_sun_rgb { 1.0F, 1.0F, 1.0F };
  float diffuse_scale { 1.0F };

  float specular_scale { 1.0F };
  std::uint32_t atmosphere_light_slot { 0xFFFFFFFFU };
  std::uint32_t atmosphere_mode_flags { 0U };
  std::uint32_t shadow_flags { 0U };

  std::uint32_t light_function_atlas_index { 0xFFFFFFFFU };
  std::uint32_t cascade_count { 0U };
  std::uint32_t light_flags { 0U };

  FrameDirectionalCsmSplitMode cascade_split_mode {
    FrameDirectionalCsmSplitMode::kGenerated
  };
  float max_shadow_distance { 160.0F };
  std::array<float, kFrameDirectionalLightMaxCascades> cascade_distances {
    8.0F, 24.0F, 64.0F, 160.0F
  };
  float distribution_exponent { 3.0F };

  float transition_fraction { 0.1F };
  float distance_fadeout_fraction { 0.1F };
  float shadow_bias { 0.0F };
  float shadow_normal_bias { 0.02F };
  std::uint32_t shadow_resolution_hint { 1U };
};

struct FrameLocalLightSelection {
  LocalLightKind kind { LocalLightKind::kPoint };

  glm::vec3 position { 0.0F };
  float range { 0.0F };

  glm::vec3 color { 1.0F, 1.0F, 1.0F };
  float intensity { 0.0F };

  glm::vec3 direction { 0.0F, -1.0F, 0.0F };
  float decay_exponent { 2.0F };

  float inner_cone_cos { 1.0F };
  float outer_cone_cos { 0.0F };
  float source_radius { 0.0F };
  std::uint32_t flags { 0U };

  float shadow_bias { 0.0F };
  float shadow_normal_bias { 0.02F };
  std::uint32_t shadow_resolution_hint { 1U };
  std::uint32_t _padding0 { 0U };
};

struct FrameLightSelection {
  std::optional<FrameDirectionalLightSelection> directional_light {};
  std::vector<FrameLocalLightSelection> local_lights {};
  std::uint64_t selection_epoch { 0U };

  [[nodiscard]] auto directional_light_count() const noexcept -> std::uint32_t
  {
    return directional_light.has_value() ? 1U : 0U;
  }

  [[nodiscard]] auto local_light_count() const noexcept -> std::uint32_t
  {
    return static_cast<std::uint32_t>(local_lights.size());
  }

  [[nodiscard]] auto empty() const noexcept -> bool
  {
    return !directional_light.has_value() && local_lights.empty();
  }
};

} // namespace oxygen::vortex

//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

namespace oxygen::vortex {

enum class LocalLightKind : std::uint32_t {
  kPoint = 0U,
  kSpot = 1U,
};

struct FrameDirectionalLightSelection {
  // Vector from the shaded point toward the directional-light source in
  // Oxygen world space (+Z up, -Y forward).
  glm::vec3 direction { 0.0F, -1.0F, 0.0F };
  float source_radius { 0.0F };

  glm::vec3 color { 1.0F, 1.0F, 1.0F };
  float illuminance_lux { 0.0F };

  float specular_scale { 1.0F };
  float diffuse_scale { 1.0F };
  std::uint32_t shadow_flags { 0U };
  std::uint32_t light_function_atlas_index { 0xFFFFFFFFU };

  std::uint32_t cascade_count { 0U };
  std::uint32_t light_flags { 0U };
  std::uint32_t reserved0 { 0U };
  std::uint32_t reserved1 { 0U };
  std::uint32_t reserved2 { 0U };
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

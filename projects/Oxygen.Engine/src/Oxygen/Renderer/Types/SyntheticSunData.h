//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

#include <Oxygen/Core/Types/Atmosphere.h>

namespace oxygen::engine {

//! Primary directional light (Sun) state for atmospheric and lighting effects.
struct SyntheticSunData {
  static constexpr size_t kSize = 48;

  uint32_t enabled { 0U };
  float cos_zenith { 0.0F };
  std::array<uint32_t, 2> _pad { 0, 0 };
  glm::vec4 direction_ws_illuminance { atmos::kDefaultSunDirection,
    atmos::kDefaultSunIlluminanceLx };
  glm::vec4 color_rgb_intensity { atmos::kDefaultSunColorRgb,
    atmos::kDefaultSunIlluminanceLx };

  [[nodiscard]] static auto FromDirectionAndLight(const glm::vec3& direction,
    const glm::vec3& color, float illuminance_lx, bool is_enabled = true)
    -> SyntheticSunData
  {
    SyntheticSunData s;
    s.enabled = is_enabled ? 1U : 0U;
    const glm::vec3 dir = glm::normalize(direction);
    s.direction_ws_illuminance = { dir, illuminance_lx };
    s.color_rgb_intensity = { color, illuminance_lx };
    s.cos_zenith = dir.z;
    return s;
  }

  [[nodiscard]] auto GetDirection() const noexcept -> glm::vec3
  {
    return { direction_ws_illuminance.x, direction_ws_illuminance.y,
      direction_ws_illuminance.z };
  }

  [[nodiscard]] auto GetColor() const noexcept -> glm::vec3
  {
    return { color_rgb_intensity.x, color_rgb_intensity.y,
      color_rgb_intensity.z };
  }

  [[nodiscard]] auto GetIlluminance() const noexcept -> float
  {
    return direction_ws_illuminance.w;
  }

  [[nodiscard]] auto GetSinZenith() const noexcept -> float
  {
    return std::sqrt(std::max(0.0F, 1.0F - (cos_zenith * cos_zenith)));
  }

  [[nodiscard]] auto GetElevationRadians() const noexcept -> float
  {
    return std::asin(std::clamp(cos_zenith, -1.0F, 1.0F));
  }

  [[nodiscard]] auto GetAzimuthRadians() const noexcept -> float
  {
    return std::atan2(direction_ws_illuminance.y, direction_ws_illuminance.x);
  }

  [[nodiscard]] auto ApproxEquals(
    const SyntheticSunData& other, float epsilon) const noexcept -> bool
  {
    const auto vec4_approx = [epsilon](const glm::vec4& a, const glm::vec4& b) {
      return std::abs(a.x - b.x) < epsilon && std::abs(a.y - b.y) < epsilon
        && std::abs(a.z - b.z) < epsilon && std::abs(a.w - b.w) < epsilon;
    };

    return enabled == other.enabled
      && vec4_approx(direction_ws_illuminance, other.direction_ws_illuminance)
      && vec4_approx(color_rgb_intensity, other.color_rgb_intensity);
  }

  [[nodiscard]] auto ElevationDiffers(
    const SyntheticSunData& other, float epsilon) const noexcept -> bool
  {
    return std::abs(cos_zenith - other.cos_zenith) > epsilon;
  }
};
static_assert(sizeof(SyntheticSunData) == SyntheticSunData::kSize);

inline constexpr SyntheticSunData kNoSun = [] {
  SyntheticSunData s;
  s.enabled = 0U;
  s.direction_ws_illuminance.w = 0.0F;
  s.color_rgb_intensity.w = 0.0F;
  return s;
}();

} // namespace oxygen::engine

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

#include <glm/vec4.hpp>

#include <Oxygen/Core/Types/Atmosphere.h>
#include <Oxygen/Renderer/Types/LightCullingConfig.h>
#include <Oxygen/Renderer/Types/SceneConstants.h>

namespace oxygen::engine {

//! Atmospheric scattering and planet context data.
struct AtmosphereData {
  static constexpr size_t kSize = 64;

  uint32_t flags { 0U };
  float sky_view_lut_slice { 0.0F };
  float planet_to_sun_cos_zenith { 0.0F };
  float aerial_perspective_distance_scale {
    atmos::kDefaultAerialPerspectiveDistanceScale
  };
  float aerial_scattering_strength { atmos::kDefaultAerialScatteringStrength };
  std::array<uint32_t, 3> _pad { 0, 0, 0 }; // Padding to align vec4 members
  glm::vec4 planet_center_ws_pad { 0.0F, 0.0F, -atmos::kDefaultPlanetRadiusM,
    0.0F };
  glm::vec4 planet_up_ws_camera_altitude_m { atmos::kDefaultPlanetUp, 0.0F };
};
static_assert(sizeof(AtmosphereData) == AtmosphereData::kSize);

//! Primary directional light (Sun) state for atmospheric effects.
struct SyntheticSunData {
  static constexpr size_t kSize = 48;

  uint32_t enabled { 0U };
  float cos_zenith { 0.0F };
  std::array<uint32_t, 2> _pad { 0, 0 };
  glm::vec4 direction_ws_illuminance { atmos::kDefaultSunDirection,
    atmos::kDefaultSunIlluminanceLx };
  glm::vec4 color_rgb_intensity { atmos::kDefaultSunColorRgb,
    atmos::kDefaultSunIlluminanceLx };

  //=== Utilities ===---------------------------------------------------------//

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

//! Disabled/invalid sun state constant.
inline constexpr SyntheticSunData kNoSun = [] {
  SyntheticSunData s;
  s.enabled = 0U;
  s.direction_ws_illuminance.w = 0.0F;
  s.color_rgb_intensity.w = 0.0F;
  return s;
}();

/**
//! EnvironmentDynamicData holds per-frame, per-view dynamic environment state
//! such as light culling configuration, atmosphere context, and sun state.
//! It is updated every frame and bound to the GPU as a Root Constant Buffer
//! View (CBV).

 @warning This struct must remain 16-byte aligned for D3D12 root CBV bindings.
 @see LightCullingConfig, LightCullingPass
*/
struct alignas(packing::kShaderDataFieldAlignment) EnvironmentDynamicData {
  LightCullingConfig light_culling;
  AtmosphereData atmosphere;
  SyntheticSunData sun;
};

namespace layout {
  inline constexpr size_t kEnvironmentDynamicDataSize = 160;
  inline constexpr size_t kClusterDimXOffset
    = offsetof(EnvironmentDynamicData, light_culling.cluster_dim_x);
  inline constexpr size_t kSunDirectionBlockOffset
    = offsetof(EnvironmentDynamicData, sun.direction_ws_illuminance);
} // namespace layout

static_assert(
  alignof(EnvironmentDynamicData) == packing::kShaderDataFieldAlignment,
  "EnvironmentDynamicData must stay aligned for root CBV");
static_assert(
  sizeof(EnvironmentDynamicData) % packing::kShaderDataFieldAlignment == 0,
  "EnvironmentDynamicData size must be aligned");
static_assert(
  sizeof(EnvironmentDynamicData) == layout::kEnvironmentDynamicDataSize,
  "EnvironmentDynamicData size must match HLSL cbuffer packing");

static_assert(offsetof(EnvironmentDynamicData, light_culling.cluster_dim_x)
    == layout::kClusterDimXOffset,
  "EnvironmentDynamicData layout mismatch: cluster_dim_x offset");
static_assert(offsetof(EnvironmentDynamicData, sun.direction_ws_illuminance)
    == layout::kSunDirectionBlockOffset,
  "EnvironmentDynamicData layout mismatch: sun block offset");

} // namespace oxygen::engine

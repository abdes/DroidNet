//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <optional>

#include <glm/geometric.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Renderer/Internal/SunResolver.h>

namespace oxygen::engine::internal {

namespace {

  [[nodiscard]] auto IsSunTagged(const DirectionalLightBasic& light) noexcept
    -> bool
  {
    const auto flags = static_cast<DirectionalLightFlags>(light.flags);
    return (flags & DirectionalLightFlags::kSunLight)
      != DirectionalLightFlags::kNone;
  }

  [[nodiscard]] auto ResolveUniqueSunTaggedDirectionalLight(
    std::span<const DirectionalLightBasic> directional_lights)
    -> std::optional<DirectionalLightBasic>
  {
    std::optional<DirectionalLightBasic> sun_light {};
    std::size_t tagged_count = 0;
    for (const auto& light : directional_lights) {
      if (!IsSunTagged(light)) {
        continue;
      }

      ++tagged_count;
      if (tagged_count == 1U) {
        sun_light = light;
      }
    }

    if (tagged_count == 0U) {
      return std::nullopt;
    }
    if (tagged_count > 1U) {
      LOG_F(ERROR,
        "SunResolver: invalid scene lighting configuration: {} directional "
        "lights are tagged as sun; renderer requires exactly one sun-tagged "
        "directional light",
        tagged_count);
      return std::nullopt;
    }
    return sun_light;
  }

  [[nodiscard]] auto BuildResolvedSunFromDirectionalLight(
    const DirectionalLightBasic& light) noexcept -> SyntheticSunData
  {
    const float direction_len_sq
      = glm::dot(light.direction_ws, light.direction_ws);
    if (direction_len_sq <= oxygen::math::EpsilonDirection) {
      LOG_F(ERROR,
        "SunResolver: sun-tagged directional light has invalid zero-length "
        "direction; sun resolution disabled for this view");
      return kNoSun;
    }

    return SyntheticSunData::FromDirectionAndLight(
      -glm::normalize(light.direction_ws), light.color_rgb, light.intensity_lux,
      true);
  }

} // namespace

auto FindSunTaggedDirectionalLight(
  std::span<const DirectionalLightBasic> directional_lights)
  -> std::optional<DirectionalLightBasic>
{
  return ResolveUniqueSunTaggedDirectionalLight(directional_lights);
}

auto ResolvedSunMatchesDirectionalLight(const SyntheticSunData& resolved_sun,
  const DirectionalLightBasic& light, const float direction_epsilon,
  const float illuminance_relative_epsilon) noexcept -> bool
{
  if (resolved_sun.enabled == 0U) {
    return false;
  }

  const float light_dir_len_sq
    = glm::dot(light.direction_ws, light.direction_ws);
  if (light_dir_len_sq <= oxygen::math::EpsilonDirection) {
    return false;
  }

  const glm::vec3 resolved_direction
    = glm::normalize(resolved_sun.GetDirection());
  const glm::vec3 expected_direction_to_sun
    = -glm::normalize(light.direction_ws);
  const bool direction_matches
    = glm::length(resolved_direction - expected_direction_to_sun)
    <= direction_epsilon;

  const float expected_illuminance = light.intensity_lux;
  const float resolved_illuminance = resolved_sun.GetIlluminance();
  const float illuminance_tolerance = (std::max)(1.0F,
    std::abs(expected_illuminance) * illuminance_relative_epsilon);
  const bool illuminance_matches
    = std::abs(resolved_illuminance - expected_illuminance)
    <= illuminance_tolerance;

  return direction_matches && illuminance_matches;
}

auto ResolveSunForView(scene::Scene& scene,
  std::span<const DirectionalLightBasic> directional_lights) -> SyntheticSunData
{
  static_cast<void>(scene);

  const auto sun_light
    = ResolveUniqueSunTaggedDirectionalLight(directional_lights);
  if (!sun_light.has_value()) {
    return kNoSun;
  }

  return BuildResolvedSunFromDirectionalLight(*sun_light);
}

} // namespace oxygen::engine::internal

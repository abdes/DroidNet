//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstdint>
#include <optional>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Renderer/Internal/SunResolver.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::engine::internal {

namespace {

  struct SunLightSelection {
    glm::vec3 direction_to_sun { 0.0F, 0.866F, 0.5F };
    glm::vec3 color_rgb { 1.0F, 1.0F, 1.0F };
    float intensity { 0.0F };
    float illuminance { 0.0F };
    bool valid { false };
  };

  auto SelectSunLight(std::span<const DirectionalLightBasic> dir)
    -> SunLightSelection
  {
    SunLightSelection first_flagged;
    SunLightSelection first_any;

    for (const auto& light : dir) {
      const bool is_sun = (light.flags
                            & static_cast<std::uint32_t>(
                              oxygen::engine::DirectionalLightFlags::kSunLight))
        != 0U;

      const float peak_rgb = (std::max)(light.color_rgb.x,
        (std::max)(light.color_rgb.y, light.color_rgb.z));
      const float illum = light.intensity_lux * peak_rgb;

      SunLightSelection candidate {
        .direction_to_sun = -glm::normalize(light.direction_ws),
        .color_rgb = light.color_rgb,
        .intensity = light.intensity_lux,
        .illuminance = illum,
        .valid = true,
      };

      if (is_sun && !first_flagged.valid) {
        first_flagged = candidate;
      }
      if (!first_any.valid) {
        first_any = candidate;
      }
    }

    if (first_flagged.valid) {
      return first_flagged;
    }
    return first_any;
  }

  [[nodiscard]] auto ResolveSunFromSelection(const SunLightSelection& selection,
    const glm::vec3* color_override) noexcept -> SyntheticSunData
  {
    if (!selection.valid
      || glm::dot(selection.direction_to_sun, selection.direction_to_sun)
        <= 0.0F) {
      return kNoSun;
    }

    const glm::vec3 color
      = color_override != nullptr ? *color_override : selection.color_rgb;
    return SyntheticSunData::FromDirectionAndLight(
      selection.direction_to_sun, color, selection.intensity, true);
  }

  [[nodiscard]] auto ComputeDirectionToSun(
    const scene::SceneNode& node) noexcept -> std::optional<glm::vec3>
  {
    auto transform = node.GetTransform();
    const auto rotation_opt = transform.GetWorldRotation();
    if (!rotation_opt) {
      return std::nullopt;
    }

    const glm::vec3 light_direction_ws
      = (*rotation_opt) * oxygen::space::move::Forward;
    const float len_sq = glm::dot(light_direction_ws, light_direction_ws);
    if (len_sq <= oxygen::math::EpsilonDirection) {
      return std::nullopt;
    }

    return -glm::normalize(light_direction_ws);
  }

} // namespace

auto ResolveSunForView(scene::Scene& scene,
  std::span<const DirectionalLightBasic> directional_lights) -> SyntheticSunData
{
  if (auto env = scene.GetEnvironment()) {
    if (auto sun = env->TryGetSystem<scene::environment::Sun>(); sun) {
      if (!sun->IsEnabled()) {
        return kNoSun;
      }
      if (sun->GetSunSource() == scene::environment::SunSource::kSynthetic) {
        return SyntheticSunData::FromDirectionAndLight(sun->GetDirectionWs(),
          sun->GetColorRgb(), sun->GetIlluminanceLx(), true);
      }

      const auto& light_reference = sun->GetLightReference();
      if (light_reference) {
        auto node = *light_reference;
        if (node.IsAlive()) {
          auto light_opt = node.GetLightAs<scene::DirectionalLight>();
          if (!light_opt) {
            sun->ClearLightReference();
            return SyntheticSunData::FromDirectionAndLight(
              sun->GetDirectionWs(), sun->GetColorRgb(),
              sun->GetIlluminanceLx(), true);
          }

          const auto direction_opt = ComputeDirectionToSun(node);
          if (!direction_opt) {
            return SyntheticSunData::FromDirectionAndLight(
              sun->GetDirectionWs(), sun->GetColorRgb(),
              sun->GetIlluminanceLx(), true);
          }

          const auto& light = light_opt->get();
          const glm::vec3 color = sun->HasLightTemperature()
            ? sun->GetColorRgb()
            : light.Common().color_rgb;
          return SyntheticSunData::FromDirectionAndLight(
            *direction_opt, color, light.GetIntensityLux(), true);
        }
      }

      const glm::vec3* color_override
        = sun->HasLightTemperature() ? &sun->GetColorRgb() : nullptr;
      return ResolveSunFromSelection(
        SelectSunLight(directional_lights), color_override);
    }
  }

  return ResolveSunFromSelection(SelectSunLight(directional_lights), nullptr);
}

} // namespace oxygen::engine::internal

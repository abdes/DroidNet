//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

#include <glm/vec3.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex {

struct alignas(packing::kShaderDataFieldAlignment) DirectionalLightForwardData {
  glm::vec3 direction { 0.0F, -1.0F, 0.0F };
  float source_radius { 0.0F };

  glm::vec3 color { 1.0F, 1.0F, 1.0F };
  float illuminance_lux { 0.0F };

  glm::vec3 transmittance_toward_sun_rgb { 1.0F, 1.0F, 1.0F };
  float diffuse_scale { 1.0F };

  float specular_scale { 1.0F };
  std::uint32_t shadow_flags { 0U };
  std::uint32_t light_function_atlas_index { 0xFFFFFFFFU };
  std::uint32_t cascade_count { 0U };
  std::uint32_t light_flags { 0U };
  std::uint32_t atmosphere_light_slot { 0xFFFFFFFFU };
  std::uint32_t atmosphere_mode_flags { 0U };
  std::uint32_t reserved0 { 0U };

  [[nodiscard]] static auto FromSelection(
    const FrameDirectionalLightSelection& selection) noexcept
    -> DirectionalLightForwardData
  {
    return {
      .direction = selection.direction,
      .source_radius = selection.source_radius,
      .color = selection.color,
      .illuminance_lux = selection.illuminance_lux,
      .transmittance_toward_sun_rgb = selection.transmittance_toward_sun_rgb,
      .diffuse_scale = selection.diffuse_scale,
      .specular_scale = selection.specular_scale,
      .shadow_flags = selection.shadow_flags,
      .light_function_atlas_index = selection.light_function_atlas_index,
      .cascade_count = selection.cascade_count,
      .light_flags = selection.light_flags,
      .atmosphere_light_slot = selection.atmosphere_light_slot,
      .atmosphere_mode_flags = selection.atmosphere_mode_flags,
      .reserved0 = selection.reserved0,
    };
  }
};

static_assert(
  alignof(DirectionalLightForwardData) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(DirectionalLightForwardData) % packing::kShaderDataFieldAlignment
  == 0);

} // namespace oxygen::vortex

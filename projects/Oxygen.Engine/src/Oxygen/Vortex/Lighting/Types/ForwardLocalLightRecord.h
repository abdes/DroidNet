//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <glm/vec4.hpp>

#include <Oxygen/Core/Constants.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>

namespace oxygen::vortex {

struct alignas(packing::kShaderDataFieldAlignment) ForwardLocalLightRecord {
  glm::vec4 position_and_inv_radius { 0.0F };
  glm::vec4 color_id_falloff_and_ray_bias { 0.0F };
  glm::vec4 direction_and_extra_data { 0.0F };
  glm::vec4 spot_angles_and_source_radius { 0.0F };
  glm::vec4 tangent_ies_and_specular_scale { 0.0F };
  glm::vec4 rect_data_and_linkage { 0.0F };

  [[nodiscard]] static auto FromSelection(
    const FrameLocalLightSelection& selection) noexcept
    -> ForwardLocalLightRecord
  {
    const auto inv_radius = selection.range > 0.0F ? 1.0F / selection.range : 0.0F;
    return {
      .position_and_inv_radius = glm::vec4(selection.position, inv_radius),
      .color_id_falloff_and_ray_bias
      = glm::vec4(selection.color, selection.intensity),
      .direction_and_extra_data
      = glm::vec4(selection.direction, selection.decay_exponent),
      .spot_angles_and_source_radius = glm::vec4(selection.inner_cone_cos,
        selection.outer_cone_cos, 0.0F, selection.source_radius),
      .tangent_ies_and_specular_scale = glm::vec4(0.0F, 0.0F, 0.0F, 1.0F),
      .rect_data_and_linkage = glm::vec4(
        static_cast<float>(selection.kind), static_cast<float>(selection.flags),
        selection.range, 0.0F),
    };
  }
};

static_assert(
  alignof(ForwardLocalLightRecord) == packing::kShaderDataFieldAlignment);
static_assert(
  sizeof(ForwardLocalLightRecord) % packing::kShaderDataFieldAlignment == 0);

} // namespace oxygen::vortex
